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

    def test_unified_legacy_label_declaration_keeps_actual_adaptive16_policy(self):
        import csv,copy
        from tools import adaptive_gradient_dataset as data,adaptive_gradient_collection as collector
        from tools.tournaments.jobs import resource_gradient_collection_outputs
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);bundles=root/'builds';bundles.mkdir()
            legacy=dict(collector.CONTRACT,refresh_threshold_milli=128000)
            linux=self.bundle(bundles,'linux',{'adaptive_gradient_contract_sha256':d.canonical_hash(collector.CONTRACT),'refresh_threshold_milli':16000})
            plan,maps=self.plan(root,1);cohort=self.cohort();cohort.update(role='candidate',contract=legacy);cohort['policy'].update(intervals=[],threshold_milli=16000)
            manifest=d.make_experiment(plan,maps,[linux],cohort);atomic_json(root/'experiment.json',manifest)
            job=manifest['jobs'][0];self.assertEqual(job['labels']['resource_gradient']['cohort']['contract'],legacy)
            self.assertEqual(job['config']['resource_gradient_collection']['contract'],collector.CONTRACT_ID)
            artifacts=[]
            for name,required in resource_gradient_collection_outputs(job['config']['resource_gradient_collection']).items():
                path=root/name;columns=sorted(required)
                with path.open('w') as stream:
                    writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader()
                    if name=='gradient-episodes.csv':
                        row=dict.fromkeys(columns,0);row.update(complete=1,capture_known=1,movement_observed=1,publication_delay=8,material=1,key=1)
                        if 'overlap_support_reason' in row:row['overlap_support_reason']='natural_ground'
                        if 'overlap_supported' in row:row['overlap_supported']=1
                        writer.writerow(row)
                meta=store_artifact(path,root/'artifacts');meta['path']=name;artifacts.append(meta)
            record=dict(id='accepted',host='test',package_id='test-package',accepted=True,category='success',job=job,result=dict(status='completed',ticks=900,adaptive_gradient_policy=dict(collector_contract=collector.CONTRACT_ID,threshold_milli_direction_cost=16000)),artifacts=artifacts)
            (root/'results').mkdir();(root/'attempts').mkdir();atomic_json(root/'results'/f"{job['id']}.json",record);atomic_json(root/'attempts/accepted.json',record)
            output=d.export(root,root/'export');sealed=json.loads(Path(output['manifests'][0]).read_text());self.assertEqual(sealed['cohort'],cohort)
            row=data.load(output['manifests'],expected_contract=collector.CONTRACT)[0];self.assertEqual(row['collection_threshold_milli'],16000);self.assertEqual(row['source_contract_sha256'],d.canonical_hash(legacy))
            bad_result=copy.deepcopy(record);bad_result['result']['adaptive_gradient_policy']['threshold_milli_direction_cost']=128000
            atomic_json(root/'results'/f"{job['id']}.json",bad_result)
            with self.assertRaisesRegex(ValueError,'Runtime threshold differs'):d.export(root,root/'wrong-runtime-export')
            atomic_json(root/'results'/f"{job['id']}.json",record)
            mismatch=copy.deepcopy(linux);mismatch['build_options']['refresh_threshold_milli']=128000
            with self.assertRaisesRegex(ValueError,'threshold differs'):d.make_experiment(plan,maps,[mismatch],cohort)
            wrong=copy.deepcopy(linux);wrong['build_options']['adaptive_gradient_contract_sha256']='f'*64
            with self.assertRaisesRegex(ValueError,'pin unified'):d.make_experiment(plan,maps,[wrong],cohort)
            altered=copy.deepcopy(cohort);altered['contract']['future_offsets']=[8,31]
            with self.assertRaises(ValueError):d.make_experiment(plan,maps,[linux],altered)

    def test_historical_v3_is_read_only_and_exports_without_rewriting_contract(self):
        import csv,copy
        from tools import adaptive_gradient_collection as collector,adaptive_gradient_export as exporter
        from tools.tournaments.jobs import resource_gradient_collection_outputs
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);builds=root/'builds';builds.mkdir()
            legacy=dict(collector.LEGACY_CONTRACT,refresh_threshold_milli=128000)
            bundle=self.bundle(builds,'linux',{'adaptive_gradient_contract_sha256':d.canonical_hash(legacy),'refresh_threshold_milli':16000})
            plan,maps=self.plan(root,1);cohort=self.cohort();cohort.update(role='candidate',contract=legacy);cohort['policy'].update(intervals=[],threshold_milli=16000)
            with self.assertRaisesRegex(ValueError,'read-only'):
                d.make_experiment(plan,maps,[bundle],cohort)
            # A historical receipt fixture, not a new job produced by prepare.
            collection={'contract':collector.LEGACY_CONTRACT_ID};source=maps[0];group=source['group'];spec=source['spec']
            labels=dict(resource_gradient=dict(logical_group=group,actual_spec=spec,cohort=cohort,map_sha256=source['sha256'],model_sha256='c'*64,header_sha256='d'*64,collection_seed=None))
            job=d.job('game',bundle['id'],inputs={'map':{'sha256':source['sha256'],'path':'map.map.gz'}},seeds={'game':spec['game_seed']},config=dict(players=spec['ais'],alliances=spec['alliances'],ticks=900,resource_gradient_collection=collection),outputs={'required':list(resource_gradient_collection_outputs(collection)),'result':'full'},labels=labels,job_id=group)
            manifest=d.validate_experiment(dict(schema_version=1,id='historical-receipt-fixture',jobs=[job],settings={}))
            atomic_json(root/'experiment.json',manifest);artifacts=[]
            for name,required in resource_gradient_collection_outputs(collection).items():
                path=root/name;columns=sorted(required)
                with path.open('w') as stream:
                    writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader()
                    if name=='gradient-episodes.csv':
                        row=dict.fromkeys(columns,0);row.update(complete=1,capture_known=1,movement_observed=1,publication_delay=8,material=1,key=1,overlap_support_reason='natural_ground',overlap_supported=1);writer.writerow(row)
                artifact=store_artifact(path,root/'artifacts');artifact['path']=name;artifacts.append(artifact)
            record=dict(id='historical-accepted',host='test',package_id='historical-package',accepted=True,category='success',job=job,result=dict(status='completed',ticks=900,adaptive_gradient_policy=dict(collector_contract=collector.LEGACY_CONTRACT_ID,threshold_milli_direction_cost=16000)),artifacts=artifacts)
            (root/'results').mkdir();(root/'attempts').mkdir();atomic_json(root/'results'/f'{group}.json',record);atomic_json(root/'attempts/historical-accepted.json',record)
            result=exporter.export(root,root/'terminal-export');self.assertEqual(result['counts'],{'completed':1})
            rows=collector.load(result['completed_manifests']);self.assertEqual(len(rows),1)
            self.assertEqual(rows[0]['source_contract_sha256'],d.canonical_hash(legacy));self.assertEqual(rows[0]['collection_threshold_milli'],16000)
            sealed=json.loads(Path(result['completed_manifests'][0]).read_text());self.assertEqual(sealed['cohort']['contract'],legacy)

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

    def test_diagnostic_timeout_is_explicit_and_frozen_without_changing_tick_cap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);linux=self.bundle(root,'linux');plan,maps=self.plan(root,1)
            ordinary=d.make_experiment(plan,maps,[linux],self.cohort())
            longer=d.make_experiment(plan,maps,[linux],self.cohort(),timeout_seconds=43200)
            self.assertEqual(longer['jobs'][0]['config']['ticks'],90000)
            self.assertEqual(longer['jobs'][0]['limits']['timeout_seconds'],43200)
            self.assertNotEqual(ordinary['id'],longer['id'])
            for invalid in [0,-1,True,1.5]:
                with self.assertRaisesRegex(ValueError,'Positive game timeout'):
                    d.make_experiment(plan,maps,[linux],self.cohort(),timeout_seconds=invalid)

    def test_predeclared_execution_pool_preserves_all_planned_games(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);general=self.bundle(root,'linux');large=self.bundle(root,'linux',{'execution_pool':'large-maps'})
            plan,maps=self.plan(root,2);maps[0]['build']=large['id'];maps[1]['build']=general['id']
            manifest=d.make_experiment(plan,maps,[general,large],self.cohort())
            self.assertEqual([j['build'] for j in manifest['jobs']],[large['id'],general['id']])
            self.assertEqual({j['id'] for j in manifest['jobs']},{r['group'] for r in maps})
            maps[0]['build']='e'*64
            with self.assertRaisesRegex(ValueError,'unknown supplied build'):
                d.make_experiment(plan,maps,[general,large],self.cohort())

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
