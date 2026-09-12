#!/usr/bin/env python3
import csv,json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import maxima_efficient_design as e
import run_maxima_switch_ablation as a
import maxima_campaign_bank as bank
import run_maxima_ablation_campaign as c
class EfficientTest(unittest.TestCase):
 def test_all_flags_routed_and_farming_children_retained(self):
  self.assertEqual(set(e.MODES),set(a.ALL_SWITCHES))
  self.assertEqual(len(e.route(list(bank.MAJOR),'FS')),6)
  self.assertNotIn('tactics.enabled',e.route(list(a.WAVES[1]),'FS'))
  for k in a.ALL_SWITCHES:
   if k.startswith('farming.'):self.assertTrue(e.route([k],'FS') or e.route([k],'CP'))
 def test_fixed_budget(self):
  total=0
  for wave in (1,2,3):
   keys=list(a.WAVES[wave])
   if wave==1:keys+=sorted(bank.MAJOR-set(keys))
   fs=e.route(keys,'FS')
   if wave>1:fs=[k for k in fs if k not in bank.MAJOR]
   total+=len(fs)*140*4*4+len(fs)*70*4
  self.assertEqual(total,25200)
 def test_fresh_wave2_freeze_runs_major_screen_without_rerunning_wave1(self):
  with tempfile.TemporaryDirectory() as t:
   control=object.__new__(c.Campaign);control.output=Path(t)
   q=control.output/'qualification-ablation';q.mkdir();(q/'summary.json').write_text('{"rejected_checkpoints": []}')
   control.lock={'first_wave':2,'last_authorized_wave':3}
   control.state={'completed_stages':[]};control.verify=lambda:None
   control.discovery_banks=lambda fmt:[];control.conditional=lambda *args:None
   control.confirmation=lambda *args:None;control.save=lambda **kwargs:None;control.report=lambda:None
   stages=[]
   control.run_rows=lambda name,rows,keys:stages.append((name,keys))
   with patch.object(bank,'build',return_value=[]):control.run()
   self.assertTrue(all(name.startswith(('wave2-','wave3-')) for name,keys in stages))
   self.assertTrue(all('economy.large_economy_adaptation_enabled' in keys for name,keys in stages if name.startswith('wave2-')))
 def test_cumulative_preserves_pilot_even_when_new_banks_sort_first(self):
  def row(i):return dict(checkpoint_id=str(i),switch='x',source_block=str(i),map='m')
  old=[row(100),row(101)];r=e.cumulative_rows(old,[row(0),row(1),row(100)],['x'],3)
  self.assertEqual(r[:2],old);self.assertEqual(len(r),3);self.assertEqual(r[-1],row(0))
 def test_no_duplicate_states_or_extra_blocks(self):
  rows=[dict(checkpoint_id=str(i),switch='x',source_block=str(i//2),map='m') for i in range(10)]
  r=e.cumulative_rows(rows[:2],rows,['x'],3)
  self.assertEqual(len(r),6);self.assertEqual(len({x['checkpoint_id'] for x in r}),6)
 def test_zero_variance_is_not_false_certainty(self):
  b=e.bound([0.]*40,'economy_advantage');self.assertLess(b[0],0);self.assertGreater(b[1],0)
  self.assertLess(e.bound([0.]*320,'economy_advantage')[1],b[1])
 def test_bounds_reject_invalid_values(self):
  with self.assertRaises(ValueError):e.bound([3.]*40,'economy_advantage')
  with self.assertRaises(ValueError):e.bound([float('nan')]*40,'victory_score')
  self.assertIsNone(e.bound([0.]*31,'victory_score'))
 def test_budget_exhaustion_is_not_equivalence(self):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)
   with (p/'paired.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=['switch','source_block','difference','metric','mpid']);w.writeheader()
    for i in range(40):w.writerow(dict(switch='x',source_block=i,difference=(-1)**i,metric='economy_advantage',mpid=.03))
   d=e.decisions(p,['x','missing'],40)
   self.assertEqual(d['x']['status'],'unresolved: required sample exceeds budget')
   self.assertEqual(d['missing']['status'],'insufficient qualified opportunities')
 def test_cache_requires_qualified_case_and_matching_settings(self):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);src=p/'prior';src.mkdir();(src/'logs').mkdir()
   (src/'paired.csv').write_text('checkpoint_id\nx\n')
   old=dict(checkpoint_id='x',source_block='b',switch='farming.enabled',path='/save',horizon=20000,metric='economy_advantage',mpid=.03,focal_player=0,focal_team=0)
   for arm in ('on','off'):
    for rep in range(2):(src/'logs'/f'x-{arm}-{rep}.log.gz').write_bytes(b'archive')
   entry={'summary':str(src/'summary.json'),'manifest':str(src/'manifest.json')}
   with patch.object(a,'read_manifest',return_value=[old]):
    self.assertEqual(e.seed_cache(p,p/'new',[dict(old,horizon=30000)],[entry]),0)
    self.assertEqual(e.seed_cache(p,p/'new',[old],[entry]),4)
    self.assertEqual(e.seed_cache(p,p/'new',[old],[entry]),0)
    (src/'paired.csv').write_text('checkpoint_id\n')
    self.assertEqual(e.seed_cache(p,p/'other',[old],[entry]),0)
 def test_registry_has_no_accidental_cp_for_from_start_only(self):
  self.assertEqual(e.route(['farming.enabled','recon.enabled'],'CP'),[])
  self.assertEqual(e.route(['colonization.enabled'],'CP'),['colonization.enabled'])
 def test_context_expansion_stops_at_pilot_sized_budget_and_keeps_nested_samples(self):
  with tempfile.TemporaryDirectory() as t:
   control=object.__new__(c.Campaign);control.output=Path(t)
   control.state={'completed_stages':[]};control.adoption=None;control.report=lambda:None
   calls=[]
   def run(name,rows,keys):
    calls.append((name,list(rows)));d=control.output/name;d.mkdir()
    with (d/'paired.csv').open('w') as f:
     w=csv.DictWriter(f,fieldnames=['switch','source_block','difference','metric','mpid']);w.writeheader()
     for r in rows:w.writerow(dict(switch=r['switch'],source_block=r['source_block'],difference=0,metric='victory_score',mpid=3))
    return {'summaries':[dict(switch='tactics.enabled',ci_low=0,ci_high=0,adequately_powered=True)]}
   control.run_rows=run
   control.expansion_banks=lambda *args: self.fail('Existing eligible bank suffices')
   rows=[dict(checkpoint_id=str(i),source_block=str(i),switch='tactics.enabled',map='m',horizon=20000) for i in range(100)]
   with patch.object(bank,'build',return_value=rows):result=control.conditional(2,'ffa4',[Path(t)],['tactics.enabled'])
   self.assertEqual([len(r) for _,r in calls],[40,80])
   self.assertTrue(set(r['checkpoint_id'] for r in calls[0][1])<=set(r['checkpoint_id'] for r in calls[1][1]))
   self.assertEqual(result['summaries'][0]['verdict'],'unresolved: fixed pilot-sized budget reached')
if __name__=='__main__':unittest.main()
