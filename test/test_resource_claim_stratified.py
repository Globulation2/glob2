"""V2 capture contract tests; V1 fixtures remain independently unchanged."""
import copy
import hashlib
import gzip
import os
import subprocess
import sys
import json
import struct
import tempfile
from pathlib import Path
import unittest
from tools.tournaments.claim_capture import STRATA, STRATIFIED_DESIGN, validate_stratified_manifest, read_stratified_captures, HEADER, CANDIDATE, MAGIC


def empty_manifest():
    return dict(version=2, design=STRATIFIED_DESIGN, complete=True, censored=False,
                context={}, encoded_pending_limit=80*1024**2,input_pending_limit=128*1024**2,
                final_limit=160*1024**2,transient_limit=24*1024**2,
                peak_input_reservations=0,peak_input_payload=0,input_admission_exclusions=0,
                input_ineligible_exclusions=0,strata=[dict(id=i,name=name,file=f'event-{i:02d}.bin',
                    censored=False,eligible=0,reasons=[0,0,0],unknown_pool=0,
                    minimum_priority_ties=0,excluded_input_bytes=0,
                    excluded_eligible_admission=0,excluded_eligible_sample_cap=0,
                    excluded_eligible_queue_cap=0,priority=2**64-1,ordinal=0,
                    captured_ordinal=0,replacements=0,file_sha256='')
                    for i,name in enumerate(STRATA)])


class StratifiedCaptureTests(unittest.TestCase):
    def test_empty_all_strata_explicit(self):
        slots=validate_stratified_manifest(empty_manifest())
        self.assertEqual(len(slots),16)
        self.assertTrue(all(s['eligible']==0 for s in slots))

    def test_missing_or_permuted_stratum_rejected(self):
        value=empty_manifest();value['strata'].pop()
        with self.assertRaises(ValueError):validate_stratified_manifest(value)
        value=empty_manifest();value['strata'][0],value['strata'][1]=value['strata'][1],value['strata'][0]
        with self.assertRaises(ValueError):validate_stratified_manifest(value)

    def test_denominators_and_input_censoring(self):
        value=empty_manifest();slot=value['strata'][10]
        slot.update(eligible=2,reasons=[1,0,1],ordinal=3,captured_ordinal=3,replacements=1,file_sha256='a'*64)
        self.assertEqual(validate_stratified_manifest(value)[10]['eligible'],2)
        for update in ({'excluded_input_bytes':1},{'captured_ordinal':2},{'reasons':[1,0,0]},
                       {'unknown_pool':3},{'file':'../event.bin'}):
            bad=copy.deepcopy(value);bad['strata'][10].update(update)
            with self.assertRaises(ValueError):validate_stratified_manifest(bad)

    def test_roundtrip_v1_record_in_v2_envelope(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);manifest=empty_manifest()
            provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c'}
            manifest['context']=provenance|{'game_id':'g'}
            metadata=dict(width=2,height=2,origin=0,nearest=1,game_id='g',ordinal=12,
                          priority=10,deep_design=STRATIFIED_DESIGN,deep_stratum=10,provenance=provenance)
            encoded=json.dumps(metadata).encode()
            data=HEADER.pack(MAGIC,1,len(encoded),4,1)+encoded+struct.pack('<4I',1,1,1,1)+CANDIDATE.pack(1,0xffffffff,3,0,3,7)
            data+=hashlib.sha256(data).digest()
            (root/'event-10.bin').write_bytes(data)
            manifest['strata'][10].update(eligible=3,reasons=[2,1,0],ordinal=12,captured_ordinal=12,
                                          priority=10,replacements=1,file_sha256=hashlib.sha256(data).hexdigest())
            (root/'manifest.json').write_text(json.dumps(manifest))
            captures=list(read_stratified_captures(root,manifest,expected_provenance=provenance))
            self.assertEqual(len(captures),16)
            self.assertEqual(captures[10][2][2][0][-1],7)
            self.assertIsNone(captures[0][2])
            # Run the actual collector from a standalone zip, without checkout,
            # site packages or NumPy. This catches worker-packaging regressions.
            from tools.tournaments.transport import worker_archive
            attempt=root/'attempt';output=attempt/'output';output.mkdir(parents=True)
            deep=output/'resource-claims-deep';deep.mkdir()
            for name in ('event-10.bin','manifest.json'):(deep/name).write_bytes((root/name).read_bytes())
            schema={'kind':'schema','version':1,'patterns':['fixture'],'policies':['first_available'],
                    'sample_every':1000,'max_samples':10000,'max_output_bytes':1048576,
                    'outcome_columns':['pattern','policy','source','access','probe','weighted_cost','probes','new_destination_reads','fallback','access_weighted_cost']}
            with gzip.open(output/'resource-claims-shadow.jsonl.gz','wt') as stream:
                for row in (schema,{'kind':'decision','ordinal':12,'patterns':[[0,0,1,1,0,3,1,1,False,7]]},
                            {'kind':'summary','complete':True,'submitted':1,'completed':1}):stream.write(json.dumps(row)+'\n')
            result={'schema_version':1,'status':'completed','resolved':{'material_destinations_enabled':True},
                    'resource_claims_shadow':{'schema_version':1,'enforced':False,'submitted':1,'completed':1}}
            (output/'result.json').write_text(json.dumps(result))
            job={'config':{'material_destinations':True,'resource_claims_shadow':{'sample_every':1000,'max_samples':10000,'max_bytes':1048576},
                           'resource_claims_deep_context':manifest['context'],'resource_claims_deep_stratified':True}}
            (root/'job.json').write_text(json.dumps(job));archive=root/'worker.pyz';worker_archive(archive)
            code="import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from tournaments.jobs import EngineJob;assert EngineJob('game').collect(json.loads(Path(sys.argv[2]).read_text()),sys.argv[3])['status']=='completed';assert 'numpy' not in sys.modules"
            env=dict(os.environ);env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
            process=subprocess.run([sys.executable,'-I','-S','-c',code,str(archive),str(root/'job.json'),str(attempt)],cwd=root,env=env,capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stderr)
            # Restore only the strict capture inventory for the tamper check.
            import shutil
            shutil.rmtree(attempt);archive.unlink();(root/'job.json').unlink()
            (root/'event-00.bin').write_bytes(data)
            with self.assertRaisesRegex(ValueError,'inventory'):
                list(read_stratified_captures(root,manifest,expected_provenance=provenance))

    def test_bound_attestations(self):
        for key,value in (('encoded_pending_limit',1),('peak_input_reservations',129*1024**2),
                          ('peak_input_payload',1),('input_admission_exclusions',1)):
            bad=empty_manifest();bad[key]=value
            with self.assertRaises(ValueError):validate_stratified_manifest(bad)

    def test_v2_does_not_accept_legacy_envelope(self):
        value=empty_manifest();value['version']=1
        with self.assertRaises(ValueError):validate_stratified_manifest(value)


if __name__=='__main__':unittest.main()
