# SPDX-License-Identifier: GPL-3.0-or-later
import csv,json,pathlib,tempfile,unittest,copy,subprocess,shutil
from tools import adaptive_gradient_dataset as d
from tools.adaptive_gradient_model import validate,predict,age_demand_support
from tools.resource_gradient_dataset import seal,file_hash
ROOT=pathlib.Path(__file__).resolve().parents[1]
class UnifiedDatasetTests(unittest.TestCase):
 def fixture(self,root,**updates):
  row=dict(tick=300,family=0,team=0,movement=0,material=1,key=1,complete=1,publication_delay=8,queries=2,defer_extra=9,refresh_extra=1,capture_known=1,movement_observed=1,**dict.fromkeys(d.MOVEMENT_REACHABILITY,0),**dict(zip(d.BASE_FEATURES,[32,1,2,3,4,5,6])),**dict(zip(d.COUNTERS,[1,1,2])));row.update(dict(serving_capture_tick=268,captured_goal_count=4096,capture_target_added_epoch=0,capture_target_removed_epoch=0,capture_route_epoch=0,capture_seed_cost_epoch=0,current_target_added_epoch=3,current_target_removed_epoch=4,current_route_epoch=0,current_seed_cost_epoch=0,target_added_frac_q12=3,target_removed_frac_q12=4));row.update(updates);p=root/'episodes.csv'
  with p.open('w') as out:w=csv.DictWriter(out,fieldnames=list(row));w.writeheader();w.writerow(row)
  records=[dict(group='map-group',format='1v1',binary_sha256='a'*64,episode_sha256=file_hash(p),episodes=p.name,spec=dict(generator='river',width=64,height=64))];manifest=root/'raw.json';manifest.write_text(json.dumps(records));target=root/'sealed.json';seal(manifest,target,dict(id='cohort',role='exploration',policy={'name':'fixed-interval-bootstrap'},contract=d.CONTRACT));return target
 def test_schema_and_uncapped_rates(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp));rows=d.load([p]);self.assertEqual(rows[0]['family'],'materials');self.assertEqual(rows[0][d.FRACTIONS[0]],2048)
  self.assertEqual(d.prepare([0]*7,[1<<62,0,1<<63])[-2],2048)
 def test_rejects_key_and_denominator(self):
  for changes in [dict(key=42),dict(age=33),dict(since_capture_invalid_goal=3),dict(family=4,material=1,key=4<<24|1)]:
   with tempfile.TemporaryDirectory() as tmp:
    with self.assertRaises(ValueError):d.load([self.fixture(pathlib.Path(tmp),**changes)])
 def test_counterfactual_movement_reachability_is_required_and_uses_movement_denominator(self):
  for changes in [dict(defer_false_unreachable=3),dict(refresh_false_reachable=-1),dict(defer_false_unreachable=2,defer_false_reachable=1)]:
   with tempfile.TemporaryDirectory() as tmp:
    with self.assertRaises(ValueError):d.load([self.fixture(pathlib.Path(tmp),**changes)])
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);p=self.fixture(root,defer_false_unreachable=1,refresh_false_reachable=2);self.assertEqual(d.load([p])[0]['refresh_false_reachable'],2)
   doc=json.loads(p.read_text());csvpath=root/doc['records'][0]['episodes']
   with csvpath.open() as source:rows=list(csv.DictReader(source))
   fields=[k for k in rows[0] if k!='defer_false_unreachable']
   with csvpath.open('w') as out:
    writer=csv.DictWriter(out,fieldnames=fields);writer.writeheader();writer.writerow({k:rows[0][k] for k in fields})
   doc['records'][0]['episode_sha256']=file_hash(csvpath);doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'Missing generic episode'):d.load([p])
 def test_resource_only_never_silently_mapped(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp));document=json.loads(p.read_text());document['cohort']['contract']['id']='goal-stall-v1';document['content_sha256']=d.canonical_hash(dict(cohort=document['cohort'],records=document['records']));p.write_text(json.dumps(document))
   with self.assertRaisesRegex(ValueError,'resource-only'):d.load([p])
 def test_duplicate_platform_same_cohort_preserves_map_family_share(self):
  try:import numpy as np
  except ImportError:self.skipTest('numpy is a pinned development dependency')
  rows=[]
  for group,family,copies in [('large','materials',20),('large','forbidden',2),('small','guard',1)]:
   rows += [dict(format='1v1',cohort_role='exploration',cohort='shared-Linux-Mac',group=group,family=family,collection=str(i)) for i in range(copies)]
  weights=d.balanced_weights(rows);total=sum(weights)
  for family,expected in [('materials',.25),('forbidden',.25),('guard',.5)]:self.assertAlmostEqual(sum(w for w,r in zip(weights,rows) if r['family']==family)/total,expected)
 def test_horizon_and_threshold_semantics_reject_old_thirty_two_target(self):
  self.assertEqual(d.CONTRACT['future_offsets'],[8,127]);self.assertEqual(d.CONTRACT['horizon_ticks'],128);self.assertEqual(d.CONTRACT['refresh_threshold_milli'],128000)
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp));document=json.loads(p.read_text());document['cohort']['contract']['future_offsets']=[8,31];document['content_sha256']=d.canonical_hash(dict(cohort=document['cohort'],records=document['records']));p.write_text(json.dumps(document))
   with self.assertRaises(ValueError):d.load([p])
  model=json.loads((ROOT/'tools/models/adaptive-gradient.json').read_text());model['threshold']=32000
  with self.assertRaises(ValueError):validate(model)
 def test_uncapped_model_support_reports_older_demand_without_forcing_refresh(self):
  report=age_demand_support([dict(family='materials',group='map',age=4096,q128=32)])
  self.assertEqual(report['materials']['high_demand_age4096plus'],1);self.assertEqual(report['clear']['episodes'],0)
 def test_new_header_is_explicitly_untrained(self):
  model=json.loads((ROOT/'tools/models/adaptive-gradient.json').read_text());validate(model)
  with self.assertRaisesRegex(ValueError,'No unified model'):predict(model,[0]*7,[0]*3)
  model['positive_mean']['bias']=1
  with self.assertRaisesRegex(ValueError,'Untrained'):validate(model)
 def test_portable_fraction_and_predicted_math(self):
  compiler=shutil.which('clang++') or shutil.which('g++')
  if not compiler:self.skipTest('C++20 compiler unavailable')
  from tools.adaptive_gradient_codegen import render
  from tools.resource_gradient_model import predict as integer_predict
  model=json.loads((ROOT/'tools/models/adaptive-gradient.json').read_text());model['trained']=True
  for index,name in enumerate(('positive_probability','negative_probability','positive_mean','negative_mean')):model[name]=dict(coef=[((-1)**(i+index))*(i+1)*5000 for i in range(12)],bias=(index+1)*123456789,shift=28)
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp);(p/'model.h').write_text(render(model));(p/'main.cpp').write_text('#include "model.h"\n#include <iostream>\nint main(){std::uint64_t a,b,q;while(std::cin>>a>>b>>q){AdaptiveGradientModel::BaseFeatures x={32,17,99,3,7,123,456};std::cout<<AdaptiveGradientModel::predict(x,{a,b,q})<<" "<<AdaptiveGradientModel::fraction(a,q)<<"\\n";}}')
   subprocess.run([compiler,'-std=c++20','-O2','-fsanitize=undefined',str(p/'main.cpp'),'-o',str(p/'run')],check=True,capture_output=True)
   vectors=[(0,0,0),(1,0,2),(1<<62,1,(1<<64)-1),((1<<64)-1,0,(1<<64)-1),(1000001,1,2000002)];raw=''.join(f'{a} {b} {q}\n' for a,b,q in vectors);values=subprocess.check_output([str(p/'run')],input=raw,text=True).splitlines()
   for (a,b,q),result in zip(vectors,values):self.assertEqual(tuple(map(int,result.split())),(integer_predict(model,d.prepare([32,17,99,3,7,123,456],[a,b,q])),a*4096//max(1,q)))

class UnifiedWorkerContractTests(unittest.TestCase):
 def test_old_and_new_output_contracts_are_distinct(self):
  from tools.tournaments.jobs import resource_gradient_collection_outputs,_validate_resource_gradient_collection
  old=resource_gradient_collection_outputs({})
  new=resource_gradient_collection_outputs(dict(contract='unified-gradient-spatial-h128-v3',exploration='32,128'))
  self.assertIn('source_added',old['gradient-episodes.csv']);self.assertNotIn('source_added',new['gradient-episodes.csv'])
  self.assertTrue({'family','movement','capture_known',*d.BASE_FEATURES,*d.COUNTERS,*d.CAPTURE_COLUMNS}<=new['gradient-episodes.csv'])
  self.assertIn('gradient-decisions.csv',new);self.assertIn('gradient-contradictions.csv',new)
  _validate_resource_gradient_collection(dict(contract='unified-gradient-spatial-h128-v3',exploration='32,128',seed=181101))
  with self.assertRaises(ValueError):_validate_resource_gradient_collection(dict(contract='resource-rows-mapped-as-generic'))
  with self.assertRaises(ValueError):_validate_resource_gradient_collection(dict(contract='unified-gradient-h128-v2'))

 def test_spatial_completed_worker_result_attests_collector_separately_from_model(self):
  from tools.tournaments.jobs import EngineJob,resource_gradient_collection_outputs
  collection=dict(contract='unified-gradient-spatial-h128-v3');job=dict(config=dict(resource_gradient_collection=collection))
  with tempfile.TemporaryDirectory() as tmp:
   output=pathlib.Path(tmp)/'output';output.mkdir();result=output/'result.json';value=dict(schema_version=1,status='completed',adaptive_gradient_policy=dict(contract='unified-gradient-h128-v2'))
   result.write_text(json.dumps(value))
   with self.assertRaisesRegex(ValueError,'attest'):EngineJob('game').collect(job,tmp)
   for name,columns in resource_gradient_collection_outputs(collection).items():(output/name).write_text(','.join(sorted(columns))+'\n')
   value['adaptive_gradient_policy']['collector_contract']='unified-gradient-spatial-h128-v3';result.write_text(json.dumps(value))
   self.assertEqual(EngineJob('game').collect(job,tmp),value)

class UnifiedTournamentTests(unittest.TestCase):
 def test256_slots_freeze_roles_formats_and_size_pools_before_outcomes(self):
  from tools.adaptive_gradient_tournament import plan,ADAPTIVE_POLICY
  from tools.test_resource_gradient_tournament import catalog
  from collections import Counter
  self.assertEqual(ADAPTIVE_POLICY['minimum_interval'],8);self.assertIsNone(ADAPTIVE_POLICY['forced_age_cap']);self.assertEqual(ADAPTIVE_POLICY['threshold_milli'],128000);self.assertEqual(ADAPTIVE_POLICY['horizon_ticks'],128)
  a=plan(catalog(),181101);self.assertEqual(a,plan(catalog(),181101));slots=a['unified']['slots']
  self.assertEqual(len(slots),256);self.assertEqual(Counter(r['role'] for r in slots),{'exploration':128,'candidate':128})
  for role in ['exploration','candidate']:self.assertEqual(set(Counter(r['spec']['format'] for r in slots if r['role']==role).values()),{16})
  self.assertEqual(Counter(r['pool'] for r in slots),{'small-pharaoh':32,'middle-linux':224})
  self.assertEqual(a['unified']['timeout_seconds'],1800);self.assertEqual(a['unified']['process_attempts'],1)

 def test_optional_mac_routing_is_frozen_and_cannot_be_relabelled(self):
  from tools.adaptive_gradient_tournament import plan,validate_plan
  from tools.test_resource_gradient_tournament import catalog
  from collections import Counter
  remote=plan(catalog(),181101);mac=plan(catalog(),181101,'with-mac')
  self.assertEqual(Counter(r['pool'] for r in mac['unified']['slots']),{'small-pharaoh':32,'middle-linux':192,'large-mac':32})
  self.assertEqual([r['group'] for r in remote['unified']['slots']],[r['group'] for r in mac['unified']['slots']])
  for role in ['exploration','candidate']:
   self.assertEqual(Counter(r['pool'] for r in mac['unified']['slots'] if r['role']==role),{'small-pharaoh':16,'middle-linux':96,'large-mac':16})
  self.assertEqual(validate_plan(remote),'remote-only');self.assertEqual(validate_plan(mac),'with-mac')
  remote['unified']['slots'][-1]['pool']='large-mac'
  with self.assertRaisesRegex(ValueError,'routing'):validate_plan(remote)
  mac['unified']['routing']['mode']='remote-only'
  with self.assertRaisesRegex(ValueError,'routing'):validate_plan(mac)

class UnifiedSplitTests(unittest.TestCase):
 def test_map_only_split_assignment_preserves_historical_exclusions(self):
  from tools.adaptive_gradient_tournament import freeze_splits
  from tools.resource_gradient_tournament import map_identity
  spec=dict(generator='river',width=128,height=128,players=4,map_seed=181102)
  group=map_identity(spec)
  with tempfile.TemporaryDirectory() as tmp:
   path=pathlib.Path(tmp)/'splits.json';path.write_text(json.dumps(dict(schema=1,strategy='map-hash-713-v1',assignments={'historical-heldout':-1,group:2})))
   result=freeze_splits([dict(spec=spec)],path);self.assertTrue(result['prior_assignments_preserved'])
   self.assertEqual(json.loads(path.read_text())['assignments'],{'historical-heldout':-1,group:2})
   before=path.read_bytes();freeze_splits([dict(spec=dict(spec,game_seed=7,format='2v2'))],path);self.assertEqual(path.read_bytes(),before)
 def test_split_registry_must_exist_before_any_new_assignment(self):
  from tools.adaptive_gradient_tournament import freeze_splits
  with tempfile.TemporaryDirectory() as tmp:
   with self.assertRaisesRegex(ValueError,'persistent registry'):freeze_splits([],pathlib.Path(tmp)/'missing.json')

class UnifiedCensoredRecoveryTests(unittest.TestCase):
 def test_complete_horizons_survive_truncated_final_record_without_fabrication(self):
  import io,hashlib
  from tools.adaptive_gradient_export import recover_complete_records
  raw=b'complete,value\n1,42\n0,99\n1,7';out=io.StringIO();r=recover_complete_records(io.BytesIO(raw),csv.writer(out,lineterminator='\n'))
  self.assertEqual(out.getvalue(),'complete,value\n1,42\n');self.assertEqual((r['count'],r['incomplete'],r['discarded']),(1,1,3));self.assertEqual(r['decoded_sha256'],hashlib.sha256(raw).hexdigest())
 def test_malformed_interior_is_rejected_not_silently_dropped(self):
  import io
  from tools.adaptive_gradient_export import recover_complete_records
  for raw in [b'complete,value\n1,42,extra\n',b'complete,value\n2,42\n',b'complete,value\n1,nan\n']:
   with self.assertRaises(ValueError):recover_complete_records(io.BytesIO(raw),csv.writer(io.StringIO()))

class SpatialContractTests(unittest.TestCase):
 def test_censored_spatial_recovery_preserves_categorical_support(self):
  from tools.adaptive_gradient_export import recover_complete_records
  import io
  out=io.StringIO();raw=b'complete,overlap_support_reason\n1,clear_ground\n1,natural_ground'
  result=recover_complete_records(io.BytesIO(raw),csv.writer(out,lineterminator='\n'));self.assertEqual(result['count'],1);self.assertIn('clear_ground',out.getvalue());self.assertNotIn('natural_ground',out.getvalue())

 def fixture(self,root,**updates):
  from tools import adaptive_gradient_spatial as spatial
  raw=dict(overlap_support_reason='natural_ground',overlap_supported=1,overlap_history_known=1,overlap_generation=7,overlap_publication_tick=280,queries_near_goal_change=1,queries_near_passability_change=1,overlap_movement_queries=2,queries_near_goal_added=1,queries_near_goal_removed=0,queries_near_blocked=0,queries_near_opened=1);raw.update(updates)
  child=root/('spatial-'+str(len(list(root.iterdir()))));child.mkdir();path=UnifiedDatasetTests().fixture(child,**raw);doc=json.loads(path.read_text());doc['cohort']['contract']=spatial.CONTRACT;doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));path.write_text(json.dumps(doc));return path
 def test_old_contract_rejected_and_known_zero_remains_measured(self):
  from tools import adaptive_gradient_spatial as spatial
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp)
   with self.assertRaises(ValueError):spatial.load([UnifiedDatasetTests().fixture(root)])
   p=self.fixture(root,queries_near_goal_change=0,queries_near_passability_change=0,overlap_movement_queries=0,queries_near_goal_added=0,queries_near_opened=0)
   rows=spatial.load([p]);self.assertEqual(len(spatial.eligible(rows)),1);self.assertEqual(rows[0][spatial.RATES[0]],0)
 def test_unknown_history_not_imputed_and_clear_supported(self):
  from tools import adaptive_gradient_spatial as spatial
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);self.assertEqual(spatial.eligible(spatial.load([self.fixture(root,overlap_history_known=0)])),[])
   rows=spatial.load([self.fixture(root,family=3,material=255,key=(3<<24)|255,overlap_support_reason='clear_ground')]);self.assertEqual(rows[0]['family'],'clear');self.assertEqual(len(spatial.eligible(rows)),1)
 def test_missing_flag_wrong_window_unsupported_known_and_wrong_denominator_rejected(self):
  from tools import adaptive_gradient_spatial as spatial
  for changes in [dict(overlap_history_known=2),dict(overlap_publication_tick=301),dict(overlap_supported=0),dict(overlap_movement_queries=0),dict(overlap_generation=0),dict(family=2,material=255,key=(2<<24)|255)]:
   with tempfile.TemporaryDirectory() as tmp:
    with self.assertRaises(ValueError):spatial.load([self.fixture(pathlib.Path(tmp),**changes)])
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);p=self.fixture(root);doc=json.loads(p.read_text());csvpath=p.parent/doc['records'][0]['episodes'];
   with csvpath.open() as source:lines=list(csv.DictReader(source))
   fields=[k for k in lines[0] if k!='overlap_history_known']
   with csvpath.open('w') as f:
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerow({k:lines[0][k] for k in fields})
   doc['records'][0]['episode_sha256']=file_hash(csvpath);doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'Missing explicit spatial'):spatial.load([p])
 def test_explicit_disabled_benchmark_history_is_unknown_not_measured_zero(self):
  from tools import adaptive_gradient_spatial as spatial
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp),overlap_support_reason='disabled',overlap_history_known=0,queries_near_goal_change=0,queries_near_passability_change=0,overlap_movement_queries=0,queries_near_goal_added=0,queries_near_opened=0)
   self.assertEqual(spatial.eligible(spatial.load([p])),[])
 def test_research_variants_leave_baseline_twelve_inputs_unchanged(self):
  from tools import adaptive_gradient_spatial as spatial
  self.assertEqual(spatial.variants()['baseline12'],(d.FEATURES,d.TRANSFORMS))
  for features,transforms in spatial.variants().values():self.assertEqual(len(features),len(transforms))

if __name__=='__main__':unittest.main()
