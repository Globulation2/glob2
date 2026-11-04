import hashlib,json,struct,tempfile,unittest,gzip,os,subprocess,sys
from pathlib import Path
from tools.tournaments.claim_capture import STRATA,HEADER,MAGIC
from tools.tournaments.claim_capture_scalable import TOPK_DESIGN,validate_scalable_manifest,read_scalable_captures

def empty():
 return dict(version=4,top_k=16,design=TOPK_DESIGN,complete=True,censored=False,context={},encoded_pending_limit=80*2**20,input_pending_limit=128*2**20,final_limit=2560*2**20,transient_limit=24*2**20,peak_input_reservations=0,peak_input_payload=0,input_admission_exclusions=0,input_ineligible_exclusions=0,strata=[dict(id=i,name=n,eligible=0,last_ordinal=0,retained=0,records=[],censored=False,excluded_eligible_admission=0,excluded_eligible_sample_cap=0,excluded_eligible_queue_cap=0,excluded_input_bytes=0,reasons=[0,0,0],unknown_pool=0,current_priority_ties=0,weight_numerator=None,weight_denominator=None) for i,n in enumerate(STRATA)])
def populated(n=23):
 m=empty();k=min(16,n);s=m['strata'][0];s.update(eligible=n,last_ordinal=n,retained=k,reasons=[n,0,0],weight_numerator=n,weight_denominator=k,records=[dict(slot=i,file=f'event-00-{i}.bin',priority=10+i,ordinal=i+1,captured_ordinal=i+1,replacements=1,file_sha256='a'*64) for i in range(k)]);return m
class TopKTests(unittest.TestCase):
 def test_variable_k_and_rejection(self):
  for k in (1,4,16):
   m=empty();m.update(top_k=k,final_limit=16*k*10*2**20);self.assertEqual(validate_scalable_manifest(m),[[]]*16)
  for k in (0,17,True,"16"):
   m=empty();m["top_k"]=k
   with self.assertRaises(ValueError):validate_scalable_manifest(m)
 def test_dict_still_bounds_disk_manifest(self):
  with tempfile.TemporaryDirectory() as directory:
   (Path(directory)/'manifest.json').write_bytes(b' '*262145)
   with self.assertRaisesRegex(ValueError,'oversized'):list(read_scalable_captures(directory,empty(),expected_provenance={}))
 def test_cardinality(self):
  self.assertEqual(validate_scalable_manifest(empty()),[[]]*16)
  for n in (1,2,3,4,7,16,23):self.assertEqual(len(validate_scalable_manifest(populated(n))[0]),min(16,n))
 def test_no_salvage(self):
  m=populated();m['strata'][0]['records'].pop();m['strata'][0]['weight_denominator']=3
  with self.assertRaises(ValueError):validate_scalable_manifest(m)
  for key in ('excluded_input_bytes','excluded_eligible_queue_cap','excluded_eligible_sample_cap'):
   m=populated();m['strata'][0][key]=1
   with self.assertRaises(ValueError):validate_scalable_manifest(m)
 def test_bad_selected_set(self):
  for mutate in (lambda s:s['records'][1].update(ordinal=1,captured_ordinal=1),lambda s:s['records'][0].update(captured_ordinal=99),lambda s:s.update(weight_numerator=True),lambda s:s.update(retained=3),lambda s:s.update(last_ordinal=1),lambda s:s['records'][0].update(file='../bad.bin')):
   m=populated();mutate(m['strata'][0])
   with self.assertRaises(ValueError):validate_scalable_manifest(m)
 def test_envelope_limits(self):
  for changes in ({'version':2},{'final_limit':160*2**20},{'peak_input_payload':1},{'input_admission_exclusions':1}):
   m=empty();m.update(changes)
   with self.assertRaises(ValueError):validate_scalable_manifest(m)
 def test_four_wires(self):
  with tempfile.TemporaryDirectory() as directory:
   p=Path(directory);m=populated();provenance={'source_revision':'s','bundle_id':'b','cohort_id':'c'}
   for e in m['strata'][0]['records']:
    meta=dict(width=2,height=2,origin=0,nearest=1,ordinal=e['ordinal'],priority=e['priority'],deep_design=TOPK_DESIGN,deep_stratum=0,deep_slot=e['slot'],provenance=provenance);text=json.dumps(meta).encode();data=HEADER.pack(MAGIC,1,len(text),4,0)+text+struct.pack('<4I',1,1,1,1);data+=hashlib.sha256(data).digest();e['file_sha256']=hashlib.sha256(data).hexdigest();(p/e['file']).write_bytes(data)
   (p/'manifest.json').write_text(json.dumps(m));rows=list(read_scalable_captures(p,m,expected_provenance=provenance));self.assertEqual(sum(r[3] is not None for r in rows),16);self.assertEqual(len(rows),31)
   (p/'event-00-3.bin').unlink()
   with self.assertRaisesRegex(ValueError,'inventory'):list(read_scalable_captures(p,m,expected_provenance=provenance))
 def test_isolated_zip_collector(self):
  from tools.tournaments.transport import worker_archive
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);attempt=root/'attempt';output=attempt/'output';deep=output/'resource-claims-deep';deep.mkdir(parents=True)
   m=populated(16);context={'source_revision':'s','bundle_id':'b','cohort_id':'c','game_id':'g'};m['context']=context
   for e in m['strata'][0]['records']:
    meta=dict(width=2,height=2,origin=0,nearest=1,game_id='g',ordinal=e['ordinal'],priority=e['priority'],deep_design=TOPK_DESIGN,deep_stratum=0,deep_slot=e['slot'],provenance={k:context[k] for k in ('source_revision','bundle_id','cohort_id')})
    encoded=json.dumps(meta).encode();data=HEADER.pack(MAGIC,1,len(encoded),4,0)+encoded+struct.pack('<4I',1,1,1,1);data+=hashlib.sha256(data).digest();e['file_sha256']=hashlib.sha256(data).hexdigest();(deep/e['file']).write_bytes(data)
   (deep/'manifest.json').write_text(json.dumps(m))
   schema={'kind':'schema','version':1,'patterns':['fixture'],'policies':['first_available'],'sample_every':1000,'max_samples':10000,'max_output_bytes':1048576,'outcome_columns':['pattern','policy','source','access','probe','weighted_cost','probes','new_destination_reads','fallback','access_weighted_cost']}
   with gzip.open(output/'resource-claims-shadow.jsonl.gz','wt') as stream:
    for row in [schema]+[{'kind':'decision','ordinal':i,'patterns':[[0,0,1,1,0,3,1,1,False,7]]} for i in range(1,5)]+[{'kind':'summary','complete':True,'submitted':4,'completed':4}]:stream.write(json.dumps(row)+'\n')
   (output/'result.json').write_text(json.dumps({'schema_version':1,'status':'completed','resolved':{'material_destinations_enabled':True},'resource_claims_shadow':{'schema_version':1,'enforced':False,'submitted':4,'completed':4}}))
   job={'config':{'material_destinations':True,'resource_claims_shadow':{'sample_every':1000,'max_samples':10000,'max_bytes':1048576},'resource_claims_deep_context':context,'resource_claims_deep_stratified':True,'resource_claims_deep_top_k':16}}
   (root/'job.json').write_text(json.dumps(job));archive=root/'worker.pyz';worker_archive(archive)
   code="import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from tournaments.jobs import EngineJob;assert EngineJob('game').collect(json.loads(Path(sys.argv[2]).read_text()),sys.argv[3])['status']=='completed';assert 'numpy' not in sys.modules"
   env=dict(os.environ);env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
   process=subprocess.run([sys.executable,'-I','-S','-c',code,str(archive),str(root/'job.json'),str(attempt)],cwd=root,env=env,capture_output=True,text=True)
   self.assertEqual(process.returncode,0,process.stderr)
 def test_cli_capability_gate(self):
  from tools.tournaments.jobs import EngineJob
  j={'config':{'resource_claims_deep_top_k':16,'resource_claims_deep_stratified':True,'resource_claims_deep_context':{'source_revision':'s'}},'outputs':{},'seeds':{}}
  b={'directory':'/tmp','executable':'glob2','source_revision':'s','capabilities':{'resource_claims_deep_stratified_version':2}}
  with self.assertRaisesRegex(ValueError,'V4'):EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'})
  b['capabilities']['resource_claims_deep_topk_version']=4
  b['capabilities']['resource_claims_deep_topk_max']=16
  args=EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'})
  self.assertIn('--resource-claims-deep-top-k',args)
  self.assertEqual(args[args.index('--resource-claims-deep-top-k')+1],'16')
if __name__=='__main__':unittest.main()
