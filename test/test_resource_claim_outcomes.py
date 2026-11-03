import copy,io,json,unittest,subprocess,sys,tempfile
from pathlib import Path
from tools.tournaments.transport import worker_archive
from tools.tournaments.claim_outcomes import COUNTERS
from tools.tournaments.resource_claims import COLUMNS,trace_rows,read_game

def fixture():
 schema={'kind':'schema','version':1,'patterns':['p'],'policies':['first_available'],'outcome_columns':list(COLUMNS),'max_samples':4,'decision_outcome_version':1}
 decision={'kind':'decision','ordinal':1,'owner':65537,'pre_task':2**40,'tick':10,'material':1,'phase':'movement','nearest':3,'claims_enforced':True,'active_policy':'experimental','patterns':[[0,0,4,4,4,10,1,1,False,10]],'optimal':{'distance':10},'movement':0,'source_count':2,'width':4,'height':4}
 outcome={'kind':'decision_outcome','schema_version':1,'ordinal':1,'owner':65537,'pre_task':2**40,'tick':10,'material':1,'phase':'movement','pre_source':None,'pre_access':None,'attempted':True,'selection_succeeded':True,'not_attempted_reason':None,'selected_task':2**40+1,'selected_source':4,'selected_access':4,'claim_change':'acquired','selected_is_nearest_access':False,'route_result':'pending','fallback':'none','fallback_reason':'none','final_task':2**40+1,'final_has_claim':True}
 table=[[[0]*len(COUNTERS) for _ in range(2)] for _ in range(12)]
 for k in ('eligible','attempted','acquired','linked_rows','route_pending'):table[1][0][COUNTERS.index(k)]=1
 summary={'kind':'summary','complete':True,'submitted':1,'completed':1,'excluded_sample_limit':0,'excluded_queue_limit':0,'decision_outcomes':{'version':1,'phase_names':['movement','pickup'],'counter_names':list(COUNTERS),'material_phase_counters':table,'rows':1,'submitted_without_outcome':0}}
 return [schema,decision,outcome,summary]
def stream(rows):return io.StringIO(''.join(json.dumps(r)+'\n' for r in rows))
class OutcomeTests(unittest.TestCase):
 def test_either_order_and_separate_counts(self):
  rows=fixture()
  for seq in (rows,[rows[0],rows[2],rows[1],rows[3]]):
   self.assertEqual(len(list(trace_rows(stream(seq)))),4)
   game=read_game(stream(seq),{});self.assertEqual(game['diagnostics']['events'],1);self.assertEqual(game['decision_outcomes']['rows'],1)
 def test_missing_is_explicit(self):
  rows=fixture();rows.pop(2);footer=rows[-1]['decision_outcomes'];footer['rows']=0;footer['submitted_without_outcome']=1;footer['material_phase_counters'][1][0][COUNTERS.index('linked_rows')]=0
  game=read_game(stream(rows),{});self.assertEqual(game['decision_outcomes']['coverage']['submitted_without_outcome'],1)
 def test_shadow_missing_is_ineligible_not_active_missing(self):
  rows=fixture();rows.pop(2);rows[1]['claims_enforced']=False;rows[1]['active_policy']='disabled'
  footer=rows[-1]['decision_outcomes'];footer['rows']=0;footer['submitted_without_outcome']=1;footer['material_phase_counters']=[[[0]*len(COUNTERS) for _ in range(2)] for _ in range(12)]
  result=read_game(stream(rows),{})['decision_outcomes']
  self.assertEqual(result['active_submitted_without_outcome'],0);self.assertEqual(result['inactive_shadow_unobserved_decisions'],1)
 def test_bound_and_counter_mismatch(self):
  rows=fixture();rows[0]['max_samples']=0
  with self.assertRaises(ValueError):list(trace_rows(stream(rows)))
  rows=fixture();rows[-1]['decision_outcomes']['material_phase_counters'][1][0][COUNTERS.index('linked_rows')]=0
  with self.assertRaises(ValueError):list(trace_rows(stream(rows)))
 def test_cooldown_wait_is_independent_of_attempted(self):
  rows=fixture();out=rows[2];out.update(selection_succeeded=False,selected_task=0,selected_source=None,selected_access=None,claim_change='no_claim',selected_is_nearest_access=None,route_result='not_attempted',fallback_reason='cooldown',final_has_claim=False)
  counters=rows[-1]['decision_outcomes']['material_phase_counters'][1][0]
  for key,value in [('acquired',0),('route_pending',0),('no_candidate',1),('cooldown_wait',1)]:counters[COUNTERS.index(key)]=value
  rows[0]['max_samples']=2**32-1
  self.assertEqual(len(list(trace_rows(stream(rows)))),4)
  self.assertEqual(counters[COUNTERS.index('cooldown_not_attempted')],0)
 def test_bad_links_and_contract_rejected(self):
  for key,value in [('owner',4),('pre_task',1),('tick',11),('material',2),('phase','pickup'),('selected_is_nearest_access',True),('claim_change','held_same'),('route_result','guess')]:
   rows=fixture();rows[2][key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):list(trace_rows(stream(rows)))
  rows=fixture();rows.insert(2,copy.deepcopy(rows[2]))
  with self.assertRaises(ValueError):list(trace_rows(stream(rows)))
 def test_unadvertised_and_bad_footer_rejected(self):
  rows=fixture();del rows[0]['decision_outcome_version']
  with self.assertRaises(ValueError):list(trace_rows(stream(rows)))
  rows=fixture();rows[-1]['decision_outcomes']['submitted_without_outcome']=1
  with self.assertRaises(ValueError):list(trace_rows(stream(rows)))
 def test_isolated_worker_archive_includes_validator(self):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);archive=root/'worker.pyz';worker_archive(archive)
   trace=root/'trace.jsonl';trace.write_text(stream(fixture()).getvalue())
   script="import sys;sys.path.insert(0,sys.argv[1]);from tournaments.resource_claims import trace_rows;assert len(list(trace_rows(open(sys.argv[2]))))==4"
   subprocess.run([sys.executable,'-I','-S','-c',script,str(archive),str(trace)],cwd=root,check=True,capture_output=True)
 def test_old_trace_has_no_new_envelope(self):
  rows=fixture();del rows[0]['decision_outcome_version'];del rows[-1]['decision_outcomes'];rows.pop(2)
  self.assertNotIn('decision_outcomes',read_game(stream(rows),{}))
if __name__=='__main__':unittest.main()
