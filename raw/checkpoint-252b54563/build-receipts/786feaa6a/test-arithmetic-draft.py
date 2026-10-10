import sys,types,unittest
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'));source=(root/'test/gpu_offload_analysis.py').read_text();patch=(root/'artifacts/gpu-offload/candidates/786feaa6a/arithmetic-primary-draft.patch').read_text().splitlines(True)
blocks=[];old=[];new=[]
for line in patch:
 if line.startswith('@@'):
  if old:blocks.append((''.join(old),''.join(new)))
  old=[];new=[]
 elif line.startswith(('---','+++')):continue
 elif line.startswith(' '):old.append(line[1:]);new.append(line[1:])
 elif line.startswith('-'):old.append(line[1:])
 elif line.startswith('+'):new.append(line[1:])
if old:blocks.append((''.join(old),''.join(new)))
for old,new in blocks:assert old in source;source=source.replace(old,new,1)
module=types.ModuleType('gpu_offload_analysis');exec(compile(source,'arithmetic-primary-draft','exec'),module.__dict__);sys.modules['gpu_offload_analysis']=module
from test_gpu_offload_analysis import OffloadAnalysisTest
class ArithmeticPrimaryDraftTest(OffloadAnalysisTest):
 def test_geometric_pass_arithmetic_primary_fail(self):
  roster,rows=self.aggregate_fixture()
  for r in rows:
   if r['variant']=='gpu':r['result']['benchmark_run_cpu_ns']={'early':20,'middle':60,'late':160}[r['phase']]
  result=self.aggregate(roster,rows)
  self.assertAlmostEqual(result['primary_ratio'],.8)
  self.assertTrue(result['legacy_geometric_cpu_target_pass']);self.assertFalse(result['cpu_target_pass']);self.assertFalse(result['qualifying_evidence'])
 def test_offsetting_phases_cluster_whole_game(self):
  roster,rows=self.aggregate_fixture()
  for r in rows:
   if r['variant']=='gpu':
    phases=(20,140,80) if r['map_id'].endswith('-1') else (140,20,80)
    r['result']['benchmark_run_cpu_ns']=phases[('early','middle','late').index(r['phase'])]
  result=self.aggregate(roster,rows)
  for key in ('primary_ratio','primary_lower95','primary_upper95','primary_upper_one_sided95'):self.assertAlmostEqual(result[key],.8)
 def test_unequal_stratum_populations_keep_equal_weight(self):
  roster,rows=self.aggregate_fixture()
  import copy
  for group in ('corridors',):
   for mid in (3,4):
    for s in list(roster):
     if s['map_id']==group+'-1':
      new=dict(s,id=s['id'].replace('-1-','-'+str(mid)+'-'),map_id=group+'-'+str(mid));roster.append(new)
      for row in list(rows):
       if row['scenario']==s['id']:
        r=copy.deepcopy(row);r.update(scenario=new['id'],map_id=new['map_id']);rows.append(r)
  for r in rows:
   if r['variant']=='gpu':r['result']['benchmark_run_cpu_ns']=20 if r['group']=='open' else 140
  result=self.aggregate(roster,rows)
  self.assertEqual(result['maps_per_stratum'],{'open':2,'corridors':4});self.assertAlmostEqual(result['primary_ratio'],.8);self.assertAlmostEqual(result['primary_upper_one_sided95'],.8)
if __name__=='__main__':unittest.main()
