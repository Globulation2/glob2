#!/usr/bin/env python3
"""Wire corruption and final-reservoir attestation checks."""
import hashlib
import gzip
import os
import subprocess
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.resource_claim_deep import HEADER, CANDIDATE, MAGIC, DESIGN, read_event


class DeepWire(unittest.TestCase):
    def fixture(self, directory):
        provenance={'source_revision':'source','bundle_id':'bundle','cohort_id':'cohort',
                    'metadata_mode':'published-xy','censored':False}
        metadata={'width':2,'height':2,'origin':0,'nearest':1,'game_id':'game',
                  'event_id':'event','ordinal':12,'priority':2**63+9,'optimum':3,'provenance':provenance}
        encoded=json.dumps(metadata).encode()
        data=HEADER.pack(MAGIC,1,len(encoded),4,1)+encoded+struct.pack('<4I',1,1,1,1)+CANDIDATE.pack(1,0xffffffff,3,0,3,7)
        data+=hashlib.sha256(data).digest()
        path=Path(directory)/'event.bin';path.write_bytes(data)
        manifest={'version':1,'complete':True,'censored':False,'design':DESIGN,'eligible':25,
                  'ordinal':12,'captured_ordinal':12,'replacements':1,'excluded_eligible_admission':0,
                  'excluded_eligible_sample_cap':0,'excluded_eligible_queue_cap':0,'priority':metadata['priority'],'file_sha256':hashlib.sha256(data).hexdigest()}
        return path,manifest,provenance

    def test_roundtrip_and_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            path,manifest,provenance=self.fixture(directory)
            event,metadata=read_event(path,manifest,expected_provenance=provenance)
            self.assertEqual(event.candidates[1].access_distance,7)
            self.assertEqual(event.optimum,3)
            self.assertEqual(metadata['priority'],2**63+9)
            with self.assertRaisesRegex(ValueError,'provenance'):
                read_event(path,manifest,expected_provenance={'bundle_id':'wrong'})

    def test_collector_runs_from_zip_without_checkout_or_site_packages(self):
        from tools.tournaments.transport import worker_archive
        with tempfile.TemporaryDirectory(prefix='claim-wire-isolated-') as directory:
            root=Path(directory);output=root/'attempt/output';deep=output/'resource-claims-deep';deep.mkdir(parents=True)
            path,manifest,provenance=self.fixture(deep)
            context={k:provenance[k] for k in ('source_revision','bundle_id','cohort_id')}|{'game_id':'game'}
            manifest['context']=context;(deep/'manifest.json').write_text(json.dumps(manifest))
            schema={'kind':'schema','version':1,'patterns':['fixture'],'policies':['first_available'],
                'sample_every':1000,'max_samples':10000,'max_output_bytes':1048576,
                'outcome_columns':['pattern','policy','source','access','probe','weighted_cost','probes','new_destination_reads','fallback','access_weighted_cost']}
            with gzip.open(output/'resource-claims-shadow.jsonl.gz','wt') as f:
                for row in (schema,{'kind':'decision','ordinal':12,'patterns':[[0,0,1,1,0,3,1,1,False,7]]},
                            {'kind':'summary','complete':True,'submitted':1,'completed':1}):f.write(json.dumps(row)+'\n')
            result={'schema_version':1,'status':'completed','resolved':{'material_destinations_enabled':True},
                'resource_claims_shadow':{'schema_version':1,'enforced':False,'submitted':1,'completed':1}}
            (output/'result.json').write_text(json.dumps(result))
            job={'config':{'material_destinations':True,'resource_claims_shadow':{'sample_every':1000,'max_samples':10000,'max_bytes':1048576},'resource_claims_deep_context':context}}
            (root/'job.json').write_text(json.dumps(job));archive=root/'worker.pyz';worker_archive(archive)
            code="import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from tournaments.jobs import EngineJob;assert EngineJob('game').collect(json.loads(Path(sys.argv[2]).read_text()),sys.argv[3])['status']=='completed';assert 'numpy' not in sys.modules;assert not any(k=='tools' or k.startswith('tools.') for k in sys.modules)"
            env=dict(os.environ);env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
            process=subprocess.run([sys.executable,'-I','-S','-c',code,str(archive),str(root/'job.json'),str(root/'attempt')],cwd=root,env=env,capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stderr)

    def test_partial_censored_wrong_min_and_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            path,manifest,provenance=self.fixture(directory)
            for change in ({'complete':False},{'censored':True},{'eligible':0},{'ordinal':13},{'captured_ordinal':11},
                           {'excluded_eligible_admission':1,'excluded_eligible_queue_cap':1},{'excluded_eligible_sample_cap':1},
                           {'replacements':0},{'replacements':26}):
                with self.assertRaises(ValueError):read_event(path,manifest|change,expected_provenance=provenance)
            data=bytearray(path.read_bytes());data[-40]^=1;path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'digest'):read_event(path,manifest,expected_provenance=provenance)

    def test_rehashed_bad_flags_and_trailing_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path,manifest,provenance=self.fixture(directory)
            data=bytearray(path.read_bytes());struct.pack_into('<I',data,len(data)-32-CANDIDATE.size+8,4)
            data[-32:]=hashlib.sha256(data[:-32]).digest();path.write_bytes(data)
            manifest['file_sha256']=hashlib.sha256(data).hexdigest()
            with self.assertRaisesRegex(ValueError,'flags'):read_event(path,manifest,expected_provenance=provenance)
            data=data[:-32]+b'x';data+=hashlib.sha256(data).digest();path.write_bytes(data)
            manifest['file_sha256']=hashlib.sha256(data).hexdigest()
            with self.assertRaisesRegex(ValueError,'length'):read_event(path,manifest,expected_provenance=provenance)


if __name__=='__main__':unittest.main()
