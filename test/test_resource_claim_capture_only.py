import copy,io,json,unittest
from test_resource_claim_scalable import empty
from tools.tournaments.claim_capture_scalable import validate_scalable_manifest,CAPTURE_ONLY_SCOPE
from tools.tournaments.resource_claims import trace_rows,read_game,COLUMNS
from tools.tournaments.jobs import EngineJob

class CaptureOnlyTests(unittest.TestCase):
 def trace(self,on=True):
  schema=dict(kind='schema',version=1,outcome_columns=list(COLUMNS),patterns=['fixture'],policies=['first_available'])
  footer=dict(kind='summary',complete=True,submitted=0,completed=0)
  if on:
   schema.update(capture_only_version=1,decision_work_scope=CAPTURE_ONLY_SCOPE)
   footer.update(capture_only_version=1,capture_only_skipped_nonretained=23)
  return [schema,footer]
 def stream(self,rows):return io.StringIO(''.join(json.dumps(r)+'\n' for r in rows))
 def test_scope(self):
  m=empty();validate_scalable_manifest(m);m['work_scope']=CAPTURE_ONLY_SCOPE;validate_scalable_manifest(m)
  for bad in ('future',None,False):
   m['work_scope']=bad
   with self.assertRaises(ValueError):validate_scalable_manifest(m)
 def test_trace_legacy_and_capture(self):
  for on in (False,True):self.assertEqual(len(list(trace_rows(self.stream(self.trace(on))))),2)
  for mutate in (lambda r:r[0].update(decision_work_scope='future'),lambda r:r[1].update(capture_only_version=0),lambda r:r[1].update(capture_only_skipped_nonretained=-1)):
   r=self.trace();mutate(r)
   with self.assertRaises(ValueError):list(trace_rows(self.stream(r)))
 def test_ordinary_analysis_rejects_selected_population(self):
  with self.assertRaisesRegex(ValueError,'replacement-selected'):read_game(self.stream(self.trace()),{})
 def test_command_gate_and_forwarding(self):
  j={'config':{'resource_claims_capture_only':True,'resource_claims_deep_top_k':16,'resource_claims_deep_stratified':True,'resource_claims_deep_context':{'source_revision':'s'}},'outputs':{},'seeds':{}}
  b={'directory':'/tmp','executable':'glob2','source_revision':'s','capabilities':{'resource_claims_deep_stratified_version':2,'resource_claims_deep_topk_version':4,'resource_claims_deep_topk_max':16}}
  with self.assertRaisesRegex(ValueError,'capture-only'):EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'})
  b['capabilities']['resource_claims_capture_only_version']=1
  self.assertIn('--resource-claims-capture-only',EngineJob('game').command(j,b,'/tmp/attempt',{'map':'fixture.map'}))
if __name__=='__main__':unittest.main()
