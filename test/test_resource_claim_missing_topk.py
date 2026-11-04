import unittest,tempfile,json,hashlib,struct,copy,gzip,subprocess,sys,os
from pathlib import Path
from tools.tournaments.claim_capture_missing_topk import *
import test_resource_claim_missing as legacy

class TopKTests(unittest.TestCase):
 def fixture(self,p,k=64):
  old=legacy.MissingCaptureTests().fixture(p);f=p/'event-04.bin';v=f.read_bytes();h=HEADER.unpack_from(v);base=json.loads(v[HEADER.size:HEADER.size+h[2]]);f.unlink();strata=[]
  for i in range(8):
   n=k+2;records=[];reason='invalid_xy'if i<4 else'missing_scalar'if i%2 else'missing_xy_plane';reasonid={'invalid_xy':0,'missing_scalar':1,'missing_xy_plane':2}[reason]
   for slot in range(k):
    ordinal=i*n+slot+1;meta=dict(base,design=DESIGN,stratum=i,priority=slot,ordinal=ordinal,physical_slot=slot,top_k=k,reason=reason,field_present=reason!='missing_scalar',label_present=i<4,material=1 if i%4<2 else 2,phase='pickup'if i%2 else'movement');text=json.dumps(meta).encode();labels=[NO_TILE]*64 if i<4 else[];body=HEADER.pack(b'G2CLMG01',1,len(text),64,len(labels),1)+text+struct.pack(f'<{len(labels)}I',*labels)+CANDIDATE.pack(3,NO_TILE,3,0,U64,U64);body+=hashlib.sha256(body).digest();name=f'event-{i:02d}-{slot}.bin';(p/name).write_bytes(body);records.append(dict(slot=slot,priority=slot,ordinal=ordinal,captured_ordinal=ordinal,replacements=1,file=name,file_sha256=hashlib.sha256(body).hexdigest()))
   strata.append(dict(id=i,eligible=n,last_ordinal=(i+1)*n,retained=k,reasons=[n*int(j==reasonid)for j in range(3)],ties=0,excluded_sample_cap=0,excluded_queue_cap=0,excluded_input_bytes=0,censored=False,weight_numerator=n,weight_denominator=k,records=records))
  old['context']['source_revision']='r';old.update(version=2,design=DESIGN,top_k=k,pending_limit=4,encoded_pending_limit=40*1024**2,strata=strata,calls=8*(k+2),sampled=8*(k+2),admitted=8*k,completed=8*k,final_limit=8*k*MAX_RECORD,sample_every=32);(p/'manifest.json').write_text(json.dumps(old));return old
 def test_all_categories_planes_capacities(self):
  for k in (1,2,16,64):
   with self.subTest(k=k),tempfile.TemporaryDirectory()as tmp:
    p=Path(tmp);m=self.fixture(p,k);rows=list(read_missing_topk_captures(p,m,expected_provenance={'source_revision':'r'}));self.assertEqual(len(rows),8*k)
    for i,s,r,c in rows:self.assertEqual(len(c[1]or[]),64 if i<4 else 0);self.assertEqual((s['weight_numerator'],s['weight_denominator']),(k+2,k))
    if k==64:self.assertGreater((p/'manifest.json').stat().st_size,65536)
 def test_partial_censored_weights_identity_reject(self):
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);m=self.fixture(p,2)
   changes=[lambda x:x.update(top_k=True),lambda x:x.update(version=True),lambda x:x.update(completed=0),lambda x:x.update(censored=True),lambda x:x['strata'][0].update(weight_denominator=1),lambda x:x['strata'][0].update(excluded_input_bytes=1),lambda x:x['strata'][0]['records'].pop(),lambda x:x['strata'][0]['records'][0].update(captured_ordinal=0),lambda x:x['strata'][0]['records'][0].update(file='event-00-00.bin'),lambda x:x['strata'][0]['records'][0].update(slot=True)]
   for change in changes:
    bad=copy.deepcopy(m);change(bad)
    with self.assertRaises(ValueError):list(read_missing_topk_captures(p,bad,expected_provenance={}))
 def test_obsolete_and_tampering(self):
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);m=self.fixture(p,2);m.update(admitted=17,completed=17,obsolete_completed=1);validate_missing_topk_manifest(m);bad=copy.deepcopy(m);bad['obsolete_completed']=0
   with self.assertRaises(ValueError):validate_missing_topk_manifest(bad)
   r=m['strata'][0]['records'][0];f=p/r['file'];v=f.read_bytes();f.write_bytes(v[:-1]+bytes([v[-1]^1]))
   with self.assertRaises(ValueError):list(read_missing_topk_captures(p,m,expected_provenance={}))
 def test_wire_identity_even_with_valid_sha(self):
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);m=self.fixture(p,2);r=m['strata'][0]['records'][0];f=p/r['file'];v=f.read_bytes();h=list(HEADER.unpack_from(v));base=json.loads(v[HEADER.size:HEADER.size+h[2]]);tail=v[HEADER.size+h[2]:-32]
   for key,value in [('physical_slot',1),('physical_slot',True),('priority',False),('top_k',1),('top_k',True),('design','independent-minhash-missing-destination-v1')]:
    meta=dict(base);meta[key]=value;text=json.dumps(meta).encode();hh=h[:];hh[2]=len(text);body=HEADER.pack(*hh)+text+tail;body+=hashlib.sha256(body).digest();f.write_bytes(body);bad=copy.deepcopy(m);bad['strata'][0]['records'][0]['file_sha256']=hashlib.sha256(body).hexdigest()
    with self.assertRaises(ValueError):list(read_missing_topk_captures(p,bad,expected_provenance={}))
 def test_numeric_capability_forwarding(self):
  from tools.tournaments.jobs import EngineJob
  j={'config':{'resource_claims_deep_missing':True,'resource_claims_missing_top_k':64,'resource_claims_missing_sample_every':32,'resource_claims_deep_context':{'source_revision':'s'}},'outputs':{},'seeds':{}};b={'directory':'/tmp','executable':'glob2','source_revision':'s','capabilities':{'resource_claims_deep_missing_version':1}}
  with self.assertRaisesRegex(ValueError,'top-k'):EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'})
  b['capabilities'].update(resource_claims_deep_missing_version=2,resource_claims_deep_missing_max=64);args=EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'});self.assertEqual(args[args.index('--resource-claims-missing-top-k')+1],'64')
 def test_isolated_zip_collects_v4_and_v2_missing512records_independently(self):
  from tools.tournaments.transport import worker_archive
  from test_resource_claim_scalable import empty
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);output=root/'attempt/output';missing=output/'resource-claims-missing';missing.mkdir(parents=True);deep=output/'resource-claims-deep';deep.mkdir()
   context={'source_revision':'r','game_id':'g'};mm=self.fixture(missing);mm['context']=context;(missing/'manifest.json').write_text(json.dumps(mm));dm=empty();dm['context']=context;(deep/'manifest.json').write_text(json.dumps(dm))
   schema={'kind':'schema','version':1,'patterns':['fixture'],'policies':['first_available'],'sample_every':1000,'max_samples':10000,'max_output_bytes':1048576,'outcome_columns':['pattern','policy','source','access','probe','weighted_cost','probes','new_destination_reads','fallback','access_weighted_cost']}
   with gzip.open(output/'resource-claims-shadow.jsonl.gz','wt') as stream:
    for row in [schema,{'kind':'summary','complete':True,'submitted':0,'completed':0}]:stream.write(json.dumps(row)+'\n')
   (output/'result.json').write_text(json.dumps({'schema_version':1,'status':'completed','resolved':{'material_destinations_enabled':True},'resource_claims_shadow':{'schema_version':1,'enforced':False,'submitted':0,'completed':0}}))
   job={'config':{'material_destinations':True,'resource_claims_shadow':{'sample_every':1000,'max_samples':10000,'max_bytes':1048576},'resource_claims_deep_context':context,'resource_claims_deep_stratified':True,'resource_claims_deep_top_k':16,'resource_claims_deep_missing':True,'resource_claims_missing_top_k':64,'resource_claims_missing_sample_every':32}}
   (root/'job.json').write_text(json.dumps(job));archive=root/'worker.pyz';worker_archive(archive)
   code="import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from tournaments.jobs import EngineJob;assert EngineJob('game').collect(json.loads(Path(sys.argv[2]).read_text()),sys.argv[3])['status']=='completed';assert 'numpy' not in sys.modules"
   env=dict(os.environ);env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None);run=subprocess.run([sys.executable,'-I','-S','-c',code,str(archive),str(root/'job.json'),str(root/'attempt')],cwd=root,env=env,capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
 def test_empty_stream_still_requires_manifest_provenance(self):
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);m=self.fixture(p,1)
   for f in p.glob('*.bin'):f.unlink()
   for s in m['strata']:s.update(eligible=0,last_ordinal=0,retained=0,reasons=[0,0,0],weight_numerator=None,weight_denominator=None,records=[])
   m.update(calls=0,sampled=0,admitted=0,completed=0)
   self.assertEqual(len(list(read_missing_topk_captures(p,m,expected_provenance={'source_revision':'r'}))),8)
   for ctx in [None,{}, {'game_id':'g','source_revision':'wrong'}, {'game_id':False,'source_revision':'r'}]:
    bad=copy.deepcopy(m);bad['context']=ctx
    with self.assertRaises(ValueError):list(read_missing_topk_captures(p,bad,expected_provenance={'source_revision':'r'}))
if __name__=='__main__':unittest.main()
