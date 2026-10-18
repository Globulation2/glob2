# SPDX-License-Identifier: GPL-3.0-or-later
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from tools import resource_gradient_distributed as d
from tools.resource_gradient_dataset import load
from tools.tournaments.common import atomic_json, digest, file_hash, store_artifact


class DistributedTests(unittest.TestCase):
    def cohort(self):
        return dict(id='permanent-core',role='exploration',header_sha256='d'*64,policy={'name':'seeded-field-intervals','intervals':[0,32,512],'seed':9,'model_sha256':'c'*64},
            contract={'features':list(d.FEATURES),'label':'movement-direction-extra-cost','future_offsets':[8,31],
                      'benefit_unit':'direction-cost','request_type':'movement'})

    def bundle(self,root,platform,extra_options=None):
        folder=root/platform;folder.mkdir();binary=folder/'glob2';binary.write_bytes(platform.encode());binary.chmod(0o755)
        value=dict(schema_version=1,cli_version=2,source_revision='a'*40,dirty_identity=None,build_options={'model_sha256':'c'*64,'header_sha256':'d'*64,**(extra_options or {})},
            platform={'os':platform,'arch':'arm64' if platform=='darwin' else 'x86_64'},executable='glob2',
            capabilities={'schema_version':1,'commands':['game'],'sim_version':{'revision':81}},
            files=[{'path':'glob2','bytes':binary.stat().st_size,'sha256':file_hash(binary),'mode':0o755}])
        value['id']=digest(value);final=root/value['id'];folder.rename(final);atomic_json(final/'bundle.json',value)
        return value|{'directory':str(final)}

    def plan(self,root,n=32):
        specs=[];maps=[]
        for i in range(n):
            spec=dict(format='duel' if i%2==0 else 'ffa',players=2,generator='river',width=128,height=128,
                alliances=[1,2],ais=['Nicowar','Nicowar'],map_seed=i,game_seed=100+i)
            path=root/f'map{i}.gz';path.write_bytes(gzip.compress(f'EXACT_MAP_{i}'.encode(),mtime=0))
            specs.append(spec);maps.append(dict(group=d.digest(spec)[:16],spec=spec,path=str(path),sha256=file_hash(path)))
        return {'games':specs},maps

    def test_fixed_stratified_partition_uses_exact_maps_and_mac_threads(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);linux=self.bundle(root,'linux');mac=self.bundle(root,'darwin');plan,maps=self.plan(root)
            manifest=d.make_experiment(plan,maps,[linux,mac],self.cohort(),{linux['id']:7,mac['id']:1})
            self.assertEqual(len({j['id'] for j in manifest['jobs']}),32)
            for fmt in ('duel','ffa'):
                jobs=[j for j in manifest['jobs'] if j['labels']['resource_gradient']['actual_spec']['format']==fmt]
                self.assertEqual(sum(j['build']==linux['id'] for j in jobs),14)
                self.assertEqual(sum(j['build']==mac['id'] for j in jobs),2)
            for job,source in zip(manifest['jobs'],maps):
                self.assertEqual(job['inputs']['map']['sha256'],source['sha256'])
                self.assertEqual(job['inputs']['map']['path'],'map.map.gz')
                from tools.tournaments.common import copy_decoded
                decoded=root/(job['id']+'.map.gz')
                copy_decoded(Path(source['path']),decoded,job['inputs']['map'])
                self.assertEqual(decoded.read_bytes(),Path(source['path']).read_bytes())
                self.assertEqual(job['config']['resource_gradient_collection']['seed'],
                    d.exploration_seed_for_game(9,job['id']))
                if job['build']==mac['id']:self.assertEqual(job['config']['resource_gradient_collection']['threads'],1)
                else:self.assertNotIn('threads',job['config']['resource_gradient_collection'])
            self.assertEqual(len({j['config']['resource_gradient_collection']['seed'] for j in manifest['jobs']}),32)
            self.assertEqual(manifest,d.make_experiment(plan,maps,[mac,linux],self.cohort(),{linux['id']:7,mac['id']:1}))

    def test_build_and_map_drift_fail_before_submission(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);linux=self.bundle(root,'linux');mac=self.bundle(root,'darwin');plan,maps=self.plan(root,2)
            mac['capabilities']['sim_version']={'revision':82}
            with self.assertRaisesRegex(ValueError,'simulation versions'):d.make_experiment(plan,maps,[linux,mac],self.cohort())
            maps[0]['spec']={**maps[0]['spec'],'game_seed':999}
            with self.assertRaisesRegex(ValueError,'gameplay identity'):d.make_experiment(plan,maps,[linux],self.cohort())
            maps[0]['spec']=plan['games'][0];Path(maps[0]['path']).write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'bytes changed'):d.make_experiment(plan,maps,[linux],self.cohort())

    def test_augmented_contract_requires_explicit_instrumented_build(self):
        from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);linux=self.bundle(root,'linux');plan,maps=self.plan(root,1)
            cohort=self.cohort();cohort['contract']['contradictions']=CONTRADICTION_CONTRACT
            with self.assertRaisesRegex(ValueError,'does not pin'):
                d.make_experiment(plan,maps,[linux],cohort)
            linux['build_options']['contradiction_contract_sha256']=d.canonical_hash(CONTRADICTION_CONTRACT)
            manifest=d.make_experiment(plan,maps,[linux],cohort)
            self.assertEqual(manifest['jobs'][0]['labels']['resource_gradient']['cohort'],cohort)
            cohort['contract']['contradictions']={**CONTRADICTION_CONTRACT,'version':999}
            with self.assertRaisesRegex(ValueError,'Unknown contradiction'):
                d.make_experiment(plan,maps,[linux],cohort)

    def test_export_rejects_old_columns_for_augmented_contract(self):
        from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir()
            linux=self.bundle(bundles,'linux',{'contradiction_contract_sha256':d.canonical_hash(CONTRADICTION_CONTRACT)})
            plan,maps=self.plan(root,1);cohort=self.cohort();cohort['contract']['contradictions']=CONTRADICTION_CONTRACT
            manifest=d.make_experiment(plan,maps,[linux],cohort);atomic_json(root/'experiment.json',manifest)
            self.result(root,manifest['jobs'][0],linux)
            with self.assertRaisesRegex(ValueError,'missing contradiction episode columns'):
                d.export(root,root/'export')

    def result(self,root,job,bundle,instrumented=False):
        artifacts=[]
        for name in job['outputs']['required']:
            path=root/name
            if name=='gradient-episodes.csv':
                header='complete,defer_extra,refresh_extra,queries,movement_observed,'+','.join(d.FEATURES)
                row='1,40,0,3,1,256,8,16,0,0,0,0'
                if instrumented:
                    from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT as contract
                    header+=','+','.join([*contract['counters'],contract['capture_known'],*contract['future_stall_columns']])
                    row+=',0,1,1,0,0,3,1,1,0'
                path.write_text(header+'\n'+row+'\n')
            elif name=='gradient-contradictions.csv':
                path.write_text('epoch,key,empty_goal,persistent_progress_blocked,unit_progress_blocked,sidestep_used,reachable_no_move,movement_queries\n0,4,0,1,1,0,0,3\n')
            else:path.write_text('header\n')
            meta=store_artifact(path,root/'artifacts');meta['path']=name;artifacts.append(meta)
        value=dict(id='acceptedattempt',host='linux-worker',package_id='package',accepted=True,category='success',job=job,
                   result={'status':'completed','ticks':900},artifacts=artifacts)
        (root/'results').mkdir(exist_ok=True);atomic_json(root/'results'/f"{job['id']}.json",value)
        (root/'attempts').mkdir(exist_ok=True);atomic_json(root/'attempts'/'accepted.json',value)
        return value

    def test_augmented_export_retains_counter_and_stall_contract(self):
        from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir()
            linux=self.bundle(bundles,'linux',{'contradiction_contract_sha256':d.canonical_hash(CONTRADICTION_CONTRACT)})
            plan,maps=self.plan(root,1);cohort=self.cohort();cohort['contract']['contradictions']=CONTRADICTION_CONTRACT
            manifest=d.make_experiment(plan,maps,[linux],cohort);atomic_json(root/'experiment.json',manifest)
            self.result(root,manifest['jobs'][0],linux,instrumented=True)
            status=d.export(root,root/'export');rows=load(status['manifests'])
            self.assertEqual(rows[0]['since_capture_movement_queries'],3)
            self.assertEqual(rows[0]['defer_stalls'],1)
            sealed=json.loads(Path(status['manifests'][0]).read_text())
            self.assertIn('gradient-contradictions.csv',sealed['records'][0]['provenance']['artifacts'])

    def test_export_preserves_episode_receipt_and_shared_cohort(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir();linux=self.bundle(bundles,'linux');plan,maps=self.plan(root,2)
            manifest=d.make_experiment(plan,maps,[linux],self.cohort());atomic_json(root/'experiment.json',manifest)
            one=self.result(root,manifest['jobs'][0],linux)
            with self.assertRaisesRegex(ValueError,'incomplete'):d.export(root,root/'export')
            two=self.result(root,manifest['jobs'][1],linux)
            status=d.export(root,root/'export');self.assertEqual(status['completed'],2);self.assertFalse(status['missing'])
            rows=load(status['manifests']);self.assertEqual(len(rows),2);self.assertEqual({r['cohort'] for r in rows},{'permanent-core'})
            sealed=json.loads(Path(status['manifests'][0]).read_text())
            for record in sealed['records']:
                receipt=next(a for a in one['artifacts'] if a['path']=='gradient-episodes.csv')
                self.assertEqual(record['episode_sha256'],receipt['sha256'])
                self.assertEqual(record['provenance']['host'],'linux-worker')
            self.assertEqual(d.export(root,root/'export'),status)

    def test_export_rejects_corrupted_transport_and_missing_required_label(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir();linux=self.bundle(bundles,'linux');plan,maps=self.plan(root,1)
            manifest=d.make_experiment(plan,maps,[linux],self.cohort());atomic_json(root/'experiment.json',manifest)
            record=self.result(root,manifest['jobs'][0],linux)
            artifact=record['artifacts'][0];path=root/'artifacts'/artifact['sha256'];original=path.read_bytes();path.write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError,'checksum mismatch'):d.export(root,root/'export')
            path.write_bytes(original);record['artifacts']=record['artifacts'][1:]
            atomic_json(root/'results'/f"{record['job']['id']}.json",record)
            with self.assertRaisesRegex(ValueError,'required artifact'):d.export(root,root/'export')

    def test_explicit_frozen_model_header_required_and_must_match_bundles(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);linux=self.bundle(root,'linux');plan,maps=self.plan(root,1)
            cohort=self.cohort();del cohort['header_sha256']
            with self.assertRaisesRegex(ValueError,'explicit'):d.make_experiment(plan,maps,[linux],cohort)
            cohort=self.cohort();del cohort['policy']['model_sha256']
            with self.assertRaisesRegex(ValueError,'explicit'):d.make_experiment(plan,maps,[linux],cohort)
            linux['build_options']['model_sha256']='e'*64
            with self.assertRaisesRegex(ValueError,'identity differs'):d.make_experiment(plan,maps,[linux],self.cohort())

    def test_export_verifies_decoded_identity_and_size_receipts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir();linux=self.bundle(bundles,'linux');plan,maps=self.plan(root,1)
            manifest=d.make_experiment(plan,maps,[linux],self.cohort());atomic_json(root/'experiment.json',manifest)
            record=self.result(root,manifest['jobs'][0],linux);result=root/'results'/f"{record['job']['id']}.json"
            original=dict(record['artifacts'][0]);record['artifacts'][0]['raw_sha256']='f'*64;atomic_json(result,record)
            with self.assertRaisesRegex(ValueError,'raw receipt identity'):d.export(root,root/'export')
            record['artifacts'][0]=dict(original,raw_bytes=original['raw_bytes']-1);atomic_json(result,record)
            with self.assertRaisesRegex(ValueError,'raw_bytes'):d.export(root,root/'export')
            record['artifacts'][0]=dict(original,raw_bytes=original['raw_bytes']+1);atomic_json(result,record)
            with self.assertRaisesRegex(ValueError,'raw receipt identity'):d.export(root,root/'export')

    def test_terminal_failure_is_not_counted_as_completed_training_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir();linux=self.bundle(bundles,'linux');plan,maps=self.plan(root,1)
            manifest=d.make_experiment(plan,maps,[linux],self.cohort());atomic_json(root/'experiment.json',manifest)
            record=self.result(root,manifest['jobs'][0],linux);record['category']='invalid_request'
            atomic_json(root/'results'/f"{record['job']['id']}.json",record)
            with self.assertRaisesRegex(ValueError,'terminal failures'):d.export(root,root/'export')
            status=d.export(root,root/'export',True);self.assertEqual(status['completed'],0);self.assertEqual(len(status['failed']),1)


if __name__=='__main__':unittest.main()
