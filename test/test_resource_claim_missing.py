import unittest,tempfile,json,hashlib,struct,copy
from pathlib import Path
from tools.tournaments.claim_capture_missing import *
class MissingCaptureTests(unittest.TestCase):
 def fixture(self,path,reason='missing_scalar'):
  labels=None if reason!='invalid_xy' else [NO_TILE]*64
  metadata={'design':DESIGN,'stratum':0 if labels else 4,'priority':42,'ordinal':9,'owner':1,'pre_task':1,'tick':1,'width':8,'height':8,'origin':0,'nearest':None,'field_present':reason!='missing_scalar','label_present':labels is not None,'reason':reason,'phase':'movement','material':1,'optimum':None,'game_id':'g','provenance':{'source_revision':'r'}}
  text=json.dumps(metadata).encode();body=HEADER.pack(b'G2CLMG01',1,len(text),64,len(labels or []),1)+text
  if labels:body+=struct.pack('<64I',*labels)
  body+=CANDIDATE.pack(3,NO_TILE,3,0,2**64-1,2**64-1);body+=hashlib.sha256(body).digest()
  slot=metadata['stratum'];(path/f'event-0{slot}.bin').write_bytes(body)
  slots=[{'id':i,'eligible':0,'priority':2**64-1,'ordinal':0,'captured_ordinal':0,'reasons':[0,0,0],'ties':0,'excluded_sample_cap':0,'excluded_queue_cap':0,'excluded_input_bytes':0,'replacements':0,'censored':False,'file':None,'file_sha256':None} for i in range(8)]
  r={'invalid_xy':0,'missing_scalar':1,'missing_xy_plane':2}[reason];slots[slot].update(eligible=1,priority=42,ordinal=9,captured_ordinal=9,reasons=[int(i==r) for i in range(3)],replacements=1,file=f'event-0{slot}.bin',file_sha256=hashlib.sha256(body).hexdigest())
  m={'version':1,'design':DESIGN,'complete':True,'censored':False,'context':{'game_id':'g'},'sample_every':256,'calls':9,'sampled':1,'excluded_mixed':0,'valid_destination':0,'admitted':1,'completed':1,'obsolete_completed':0,'oracle_elapsed_ns':1,'strata':slots,'input_pending_limit':64*1024**2,'peak_input_reservations':1,'peak_input_payload':1,'pending_limit':2,'encoded_pending_limit':20*1024**2,'record_limit':MAX_RECORD,'final_limit':80*1024**2,'transient_limit':24*1024**2,'free_reserve':8*1024**3,'max_admissions':32768}
  (path/'manifest.json').write_text(json.dumps(m));return m
 def test_absent_and_invalid_planes_are_distinct_without_fake_anchor(self):
  for reason in ('missing_scalar','missing_xy_plane','invalid_xy'):
   with self.subTest(reason=reason),tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);m=self.fixture(p,reason);rows=list(read_missing_captures(p,m,expected_provenance={'source_revision':'r'}));record=next(c for _,_,c in rows if c)
    self.assertIsNone(record[0]['nearest']);self.assertIsNone(record[0]['optimum']);self.assertEqual(record[1] is None,reason!='invalid_xy');self.assertEqual(record[2][0][0],3)
 def test_censor_denominator_and_reason_mismatches_reject(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);m=self.fixture(p)
   for key,value in [('sampled',2),('completed',0),('censored',True),('input_pending_limit',1)]:
    bad=copy.deepcopy(m);bad[key]=value
    with self.assertRaises(ValueError):list(read_missing_captures(p,bad,expected_provenance={}))
   bad=copy.deepcopy(m);bad['strata'][4]['reasons']=[1,0,0]
   with self.assertRaises(ValueError):validate_missing_manifest(bad)
 def test_completed_obsolete_minima_conservation(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);m=self.fixture(p);m.update(sampled=2,admitted=2,completed=2,obsolete_completed=1);m['strata'][4].update(eligible=2,reasons=[0,2,0])
   validate_missing_manifest(m)
   for changes in ({'obsolete_completed':0},{'admitted':3,'completed':3}):
    bad=copy.deepcopy(m);bad.update(changes)
    with self.assertRaises(ValueError):validate_missing_manifest(bad)
 def test_typed_metadata_cannot_be_hidden_by_valid_digest(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);m=self.fixture(p);f=p/'event-04.bin';original=f.read_bytes();header=list(HEADER.unpack_from(original));start=HEADER.size;meta=json.loads(original[start:start+header[2]]);tail=original[start+header[2]:-32]
   for key,value in [('material',None),('material',-1),('material',True),('owner','1'),('tick',2**32),('pre_task',False)]:
    bad=copy.deepcopy(meta);bad[key]=value;text=json.dumps(bad).encode();h=header[:];h[2]=len(text);body=HEADER.pack(*h)+text+tail;body+=hashlib.sha256(body).digest();f.write_bytes(body);manifest=copy.deepcopy(m);manifest['strata'][4]['file_sha256']=hashlib.sha256(body).hexdigest()
    with self.assertRaises(ValueError):list(read_missing_captures(p,manifest,expected_provenance={}))
 def test_cli_capability_and_explicit_sample32_forwarding(self):
  from tools.tournaments.jobs import EngineJob
  j={'config':{'resource_claims_deep_missing':True,'resource_claims_missing_sample_every':32,'resource_claims_deep_context':{'source_revision':'s'}},'outputs':{},'seeds':{}}
  b={'directory':'/tmp','executable':'glob2','source_revision':'s','capabilities':{}}
  with self.assertRaisesRegex(ValueError,'missing'):EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'})
  b['capabilities']['resource_claims_deep_missing_version']=1
  args=EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'});self.assertIn('--resource-claims-deep-missing',args);self.assertEqual(args[args.index('--resource-claims-missing-sample-every')+1],'32')
  del j['config']['resource_claims_missing_sample_every'];args=EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'});self.assertEqual(args[args.index('--resource-claims-missing-sample-every')+1],'256')
 def test_tamper_and_extra_file_reject(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);m=self.fixture(p);f=p/'event-04.bin';b=f.read_bytes();f.write_bytes(b[:-1]+bytes([b[-1]^1]))
   with self.assertRaises(ValueError):list(read_missing_captures(p,m,expected_provenance={}))
   f.write_bytes(b);(p/'extra').write_text('x')
   with self.assertRaises(ValueError):list(read_missing_captures(p,m,expected_provenance={}))
if __name__=='__main__':unittest.main()
