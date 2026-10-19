# SPDX-License-Identifier: GPL-3.0-or-later
import csv,json,pathlib,tempfile,unittest,copy,subprocess,shutil
from tools import adaptive_gradient_dataset as d
from tools.adaptive_gradient_model import validate,predict,age_demand_support
from tools.resource_gradient_dataset import seal,file_hash
ROOT=pathlib.Path(__file__).resolve().parents[1]
def shared_model():
 """Historical shared fixtures stay independent of the selected deployment."""
 value=json.loads((ROOT/'tools/models/adaptive-gradient.json').read_text())
 value.pop('family_experts',None);value['schema']=4
 if 'deployment' in value:
  value['deployment'].pop('strategy',None);value['deployment'].pop('demanded_age_cap_ticks',None)
 return value

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
  self.assertEqual(d.CONTRACT['future_offsets'],[8,127]);self.assertEqual(d.CONTRACT['horizon_ticks'],128);self.assertNotIn('refresh_threshold_milli',d.CONTRACT)
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp));document=json.loads(p.read_text());document['cohort']['contract']['future_offsets']=[8,31];document['content_sha256']=d.canonical_hash(dict(cohort=document['cohort'],records=document['records']));p.write_text(json.dumps(document))
   with self.assertRaises(ValueError):d.load([p])
  model=shared_model();model['threshold']=16000;validate(model)
  for value in (0,-1,True,1.5,1<<63):
   model['threshold']=value
   with self.assertRaises(ValueError):validate(model)
 def test_threshold_only_legacy_contract_compatibility_preserves_provenance(self):
  from tools import adaptive_gradient_collection as collection
  for expected in (d.CONTRACT,collection.CONTRACT,collection.LEGACY_CONTRACT):
   for threshold in (16000,128000):
    legacy=dict(expected,refresh_threshold_milli=threshold)
    self.assertTrue(d.compatible_contract(legacy,expected));self.assertNotEqual(d.canonical_hash(legacy),d.canonical_hash(expected))
   changed=dict(expected,future_offsets=[8,31]);self.assertFalse(d.compatible_contract(changed,expected))
   self.assertFalse(d.compatible_contract(dict(expected,refresh_threshold_milli=0),expected));self.assertFalse(d.compatible_contract(dict(expected,refresh_threshold_milli='16000'),expected))
   self.assertTrue(d.compatible_contract_hash(d.canonical_hash(dict(expected,refresh_threshold_milli=128000)),expected))
   self.assertFalse(d.compatible_contract_hash(d.canonical_hash(dict(expected,refresh_threshold_milli=16000)),expected))
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp));doc=json.loads(p.read_text());doc['cohort']['contract']['refresh_threshold_milli']=128000;doc['cohort']['policy']['threshold_milli']=16000
   original=d.canonical_hash(doc['cohort']['contract']);doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc));row=d.load([p])[0]
   self.assertEqual(row['collection_threshold_milli'],16000);self.assertEqual(row['source_contract_sha256'],original);self.assertEqual(row['label_contract'],d.canonical_hash(d.CONTRACT));self.assertEqual(json.loads(p.read_text())['cohort']['contract']['refresh_threshold_milli'],128000)
   doc['cohort']['policy']['adaptive']={'threshold_milli':128000};doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'Conflicting'):d.load([p])
 def test_emit_threshold_override_keeps_heads_predictions_and_source_metrics(self):
  from types import SimpleNamespace
  from tools.adaptive_gradient_model import emit
  from tools.adaptive_gradient_codegen import render
  model=shared_model();model.update(schema=2,research_only=True,threshold=128000,candidate_contract=dict(d.CONTRACT,refresh_threshold_milli=128000));model['provenance']={'threshold':128000,'holdout':{'threshold':128}}
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);source=root/'research.json';source.write_text(json.dumps(model));before=file_hash(source);a=SimpleNamespace(model=str(source),contract=d.CONTRACT_ID,output=str(root/'emitted.json'),header=str(root/'model.h'),threshold_milli=16000);emit(a);new=json.loads(pathlib.Path(a.output).read_text());validate(new)
   self.assertEqual(new['threshold'],16000);self.assertEqual(new['deployment']['source_fit_metrics_threshold_milli'],128000);self.assertFalse(new['deployment']['coefficients_refit']);self.assertEqual(new['provenance'],model['provenance']);self.assertEqual(file_hash(source),before)
   for name in ('positive_probability','negative_probability','positive_mean','negative_mean'):self.assertEqual(new[name],model[name])
   old=dict(new,threshold=128000,deployment=dict(new['deployment'],threshold_milli=128000));self.assertEqual(predict(new,[32,1,2,3,4,5,6],[1,1,2]),predict(old,[32,1,2,3,4,5,6],[1,1,2]));self.assertIn('ThresholdMilli=16000',pathlib.Path(a.header).read_text());self.assertIn('ThresholdMilli=128000',render(old))
   wrong=copy.deepcopy(new);wrong['deployment']['threshold_milli']=128000
   with self.assertRaisesRegex(ValueError,'Deployment threshold'):validate(wrong)
 def test_uncapped_model_support_reports_older_demand_without_forcing_refresh(self):
  report=age_demand_support([dict(family='materials',group='map',age=4096,q128=32)])
  self.assertEqual(report['materials']['high_demand_age4096plus'],1);self.assertEqual(report['clear']['episodes'],0)
 def test_untrained_bootstrap_rejects_inference_and_nonzero_coefficients(self):
  # This contract remains valid after the committed deployment model is trained.
  model=shared_model()
  model['trained']=False
  for name in ('positive_probability','negative_probability','positive_mean','negative_mean'):
   model[name]=dict(coef=[0]*len(model['features']),bias=0,shift=28)
  validate(model)
  from tools.adaptive_gradient_codegen import render
  header = render(model)
  self.assertIn('DemandedAgeCapTicks=1024', header)
  compiler = shutil.which('clang++') or shutil.which('g++')
  if compiler:
   with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    (root/'model.h').write_text(header)
    (root/'main.cpp').write_text('#include "model.h"\nstatic_assert(AdaptiveGradientModel::DemandedAgeCapTicks == 1024);\nstatic_assert(!AdaptiveGradientModel::Trained);\nint main() {}\n')
    subprocess.run([compiler, '-std=c++20', str(root/'main.cpp'), '-o', str(root/'run')], check=True, capture_output=True)
  with self.assertRaisesRegex(ValueError,'No unified model'):predict(model,[0]*7,[0]*3)
  for invalid_cap in (None, True, 0, 7, 2**32):
   invalid=copy.deepcopy(model)
   invalid['deployment']['demanded_age_cap_ticks']=invalid_cap
   with self.assertRaisesRegex(ValueError,'demanded-age cap'):validate(invalid)
  model['positive_mean']['bias']=1
  with self.assertRaisesRegex(ValueError,'Untrained'):validate(model)
 def test_portable_fraction_and_predicted_math(self):
  compiler=shutil.which('clang++') or shutil.which('g++')
  if not compiler:self.skipTest('C++20 compiler unavailable')
  from tools.adaptive_gradient_codegen import render
  from tools.resource_gradient_model import predict as integer_predict
  model=shared_model();model['trained']=True;model.update(features=list(d.FEATURES),transforms=list(d.TRANSFORMS))
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
  new=resource_gradient_collection_outputs(dict(contract='unified-gradient-h128-v4',exploration='32,128'))
  self.assertIn('source_added',old['gradient-episodes.csv']);self.assertNotIn('source_added',new['gradient-episodes.csv'])
  self.assertTrue({'family','movement','capture_known',*d.BASE_FEATURES,*d.COUNTERS,*d.CAPTURE_COLUMNS}<=new['gradient-episodes.csv'])
  self.assertIn('gradient-decisions.csv',new);self.assertIn('gradient-contradictions.csv',new)
  _validate_resource_gradient_collection(dict(contract='unified-gradient-h128-v4',exploration='32,128',seed=181101))
  with self.assertRaises(ValueError):_validate_resource_gradient_collection(dict(contract='resource-rows-mapped-as-generic'))
  with self.assertRaises(ValueError):_validate_resource_gradient_collection(dict(contract='unified-gradient-h128-v2'))

 def test_current_completed_worker_result_attests_collector_separately_from_model(self):
  from tools.tournaments.jobs import EngineJob,resource_gradient_collection_outputs
  collection=dict(contract='unified-gradient-h128-v4');job=dict(config=dict(resource_gradient_collection=collection))
  with tempfile.TemporaryDirectory() as tmp:
   output=pathlib.Path(tmp)/'output';output.mkdir();result=output/'result.json';value=dict(schema_version=1,status='completed',adaptive_gradient_policy=dict(contract='unified-gradient-h128-v2'))
   result.write_text(json.dumps(value))
   with self.assertRaisesRegex(ValueError,'attest'):EngineJob('game').collect(job,tmp)
   for name,columns in resource_gradient_collection_outputs(collection).items():(output/name).write_text(','.join(sorted(columns))+'\n')
   value['adaptive_gradient_policy']['collector_contract']='unified-gradient-h128-v4';result.write_text(json.dumps(value))
   self.assertEqual(EngineJob('game').collect(job,tmp),value)

class UnifiedTournamentTests(unittest.TestCase):
 def test256_slots_freeze_roles_formats_and_size_pools_before_outcomes(self):
  from tools.adaptive_gradient_tournament import plan,ADAPTIVE_POLICY
  from tools.test_resource_gradient_tournament import catalog
  from collections import Counter
  self.assertEqual(ADAPTIVE_POLICY['minimum_interval'],8);self.assertEqual(ADAPTIVE_POLICY['forced_age_cap'],1024);self.assertEqual(ADAPTIVE_POLICY['threshold_milli'],16000);self.assertEqual(ADAPTIVE_POLICY['horizon_ticks'],128)
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

class ActualRoutingTests(unittest.TestCase):
 def test_actual_size_amendment_is_mechanical_and_preserves_every_role_and_seed(self):
  from tools.adaptive_gradient_tournament import plan,accepted_routing
  from tools.test_resource_gradient_tournament import catalog
  from collections import Counter
  document=plan(catalog(),181101,'with-mac');slots=document['unified']['slots'];records=[dict(group=r['group'],spec={k:v for k,v in r['spec'].items() if k!='map_candidates'},sha256='a'*64,generation_attempts=[]) for r in slots]
  records[0]['spec'].update(slots[0]['spec']['map_candidates'][0]);records[0]['generation_attempts']=[dict(returncode=2),dict(returncode=0)]
  frozen=copy.deepcopy(records);result=accepted_routing(document,records);self.assertEqual(records,frozen)
  self.assertEqual(Counter(r['actual_pool'] for r in result['assignments']),{'small-pharaoh':32,'middle-linux':192,'large-mac':32})
  original={r['group']:r['role'] for r in slots};self.assertEqual({r['group']:r['role'] for r in result['assignments']},original)
  self.assertTrue(result['roles_maps_seeds_unchanged']);self.assertEqual(result,accepted_routing(document,records))
  self.assertTrue(any(r['fallback_rejections']==1 for r in result['assignments']))
  with self.assertRaises(ValueError):accepted_routing(document,records[:-1]+[records[0]])
  records[0]['spec']['width']=1
  with self.assertRaisesRegex(ValueError,'planned candidate'):accepted_routing(document,records)

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

class CollectionContractTests(unittest.TestCase):
 def fixture(self,root,legacy=False,**updates):
  from tools import adaptive_gradient_collection as c
  raw={}
  if legacy:raw=dict(overlap_support_reason='natural_ground',overlap_supported=1,overlap_history_known=1,overlap_generation=7,overlap_publication_tick=280,queries_near_goal_change=1,queries_near_passability_change=1,overlap_movement_queries=2,queries_near_goal_added=1,queries_near_goal_removed=0,queries_near_blocked=0,queries_near_opened=1)
  raw.update(updates);child=root/('rows-'+str(len(list(root.iterdir()))));child.mkdir();p=UnifiedDatasetTests().fixture(child,**raw);doc=json.loads(p.read_text());doc['cohort'].update(id=child.name,contract=copy.deepcopy(c.LEGACY_CONTRACT if legacy else c.CONTRACT));doc['cohort']['policy']['threshold_milli']=16000
  if legacy:doc['cohort']['contract']['refresh_threshold_milli']=128000
  doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc));return p
 def test_current_has_no_spatial_observations_or_fabricated_zeros(self):
  from tools import adaptive_gradient_collection as c
  with tempfile.TemporaryDirectory() as tmp:
   rows=c.load([self.fixture(pathlib.Path(tmp))]);self.assertEqual(rows[0]['collector_contract_id'],c.CONTRACT_ID);self.assertFalse(any(k in rows[0] for k in c.LEGACY_COLUMNS));self.assertEqual(len(d.eligible(rows)),1)
 def test_legacy_read_preserves_raw_declaration_actual_policy_and_unknown(self):
  from tools import adaptive_gradient_collection as c
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);p=self.fixture(root,True,overlap_history_known=0);before=p.read_bytes();rows=c.load([p,self.fixture(root)]);self.assertEqual(len(rows),2);old=rows[0];self.assertEqual(old['collector_contract_id'],c.LEGACY_CONTRACT_ID);self.assertEqual(old['overlap_history_known'],0);self.assertEqual(old['collection_threshold_milli'],16000);self.assertEqual(p.read_bytes(),before);self.assertNotIn('queries_near_goal_change_frac_q12',old)
 def test_historical_validation_rejects_unknown_corruption(self):
  from tools import adaptive_gradient_collection as c
  for changes in [dict(overlap_history_known=2),dict(overlap_publication_tick=301),dict(overlap_supported=0),dict(overlap_movement_queries=0),dict(overlap_generation=0),dict(family=2,material=255,key=(2<<24)|255)]:
   with tempfile.TemporaryDirectory() as tmp:
    with self.assertRaises(ValueError):c.load([self.fixture(pathlib.Path(tmp),True,**changes)])
 def test_current_cannot_relabel_historical_spatial_rows(self):
  from tools import adaptive_gradient_collection as c
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp),True);doc=json.loads(p.read_text());doc['cohort']['contract']=c.CONTRACT;doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'removed spatial'):c.load([p])
 def test_missing_legacy_measurement_is_not_zero(self):
  from tools import adaptive_gradient_collection as c
  with tempfile.TemporaryDirectory() as tmp:
   p=self.fixture(pathlib.Path(tmp),True);doc=json.loads(p.read_text());csvpath=p.parent/doc['records'][0]['episodes']
   with csvpath.open() as f:rows=list(csv.DictReader(f))
   fields=[k for k in rows[0] if k!='overlap_history_known']
   with csvpath.open('w') as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerow({k:rows[0][k] for k in fields})
   doc['records'][0]['episode_sha256']=file_hash(csvpath);doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));p.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'Missing historical'):c.load([p])
 def test_changed_cohort_or_label_semantics_rejected(self):
  from tools import adaptive_gradient_collection as c
  self.assertEqual(c.contract_id(c.CONTRACT),c.CONTRACT_ID);self.assertEqual(c.contract_id(c.LEGACY_CONTRACT),c.LEGACY_CONTRACT_ID)
  for contract in [dict(c.CONTRACT,future_offsets=[8,31]),dict(c.LEGACY_CONTRACT,label='planning'),dict(c.CONTRACT,refresh_threshold_milli=True)]:self.assertFalse(c.compatible_contract(contract))
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);old=self.fixture(root,True);new=self.fixture(root);doc=json.loads(new.read_text());doc['cohort']['id']=json.loads(old.read_text())['cohort']['id'];doc['content_sha256']=d.canonical_hash(dict(cohort=doc['cohort'],records=doc['records']));new.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'Cohort identity changed'):c.load([old,new])
 def test_hash_only_receipts_are_narrow(self):
  from tools import adaptive_gradient_collection as c
  for contract in [c.CONTRACT,c.LEGACY_CONTRACT,dict(c.LEGACY_CONTRACT,refresh_threshold_milli=128000)]:self.assertTrue(c.compatible_contract_hash(d.canonical_hash(contract)))
  self.assertFalse(c.compatible_contract_hash('0'*64));self.assertFalse(c.compatible_contract_hash(d.canonical_hash(dict(c.CONTRACT,refresh_threshold_milli=128000))))
 def test_censored_legacy_recovery_preserves_support_text(self):
  from tools.adaptive_gradient_export import recover_complete_records
  import io
  out=io.StringIO();raw=b'complete,overlap_support_reason\n1,clear_ground\n1,natural_ground';r=recover_complete_records(io.BytesIO(raw),csv.writer(out,lineterminator='\n'));self.assertEqual(r['count'],1);self.assertIn('clear_ground',out.getvalue());self.assertNotIn('natural_ground',out.getvalue())

class CompactDeploymentTests(unittest.TestCase):
 def test_compaction_keeps_exact_codegen_and_predictions(self):
  from tools.adaptive_gradient_model import compact_deployment
  from tools.adaptive_gradient_codegen import render
  model=shared_model();before=copy.deepcopy(model);small=compact_deployment(model,evidence_url='https://example.com/evidence');self.assertEqual(render(small),render(model));self.assertLess(len(json.dumps(small)),12000);self.assertNotIn('candidate_contract',small);self.assertNotIn('holdout',small['provenance']);self.assertEqual(model,before);self.assertEqual(predict(small,[32,1,2,3,4,5,6],[1,1,2]),predict(model,[32,1,2,3,4,5,6],[1,1,2]))
 def test_training_receipt_must_match_fit_manifests_and_splits(self):
  from tools.adaptive_gradient_model import compact_deployment
  model=shared_model();model['provenance']=dict(manifests=[dict(sha256='a'*64)],split=dict(registry=dict(sha256='b'*64)),coverage={})
  inputs=dict(expected_slots=4,slot_counts={'completed':3,'user-censored':1},manifests=['sealed.json'],files={'sealed.json':'a'*64,'map-splits.json':'b'*64})
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'inputs.json';p.write_text(json.dumps(inputs));small=compact_deployment(model,p);self.assertEqual(small['provenance']['training_game_slots'],4);self.assertEqual(small['provenance']['training_input_sha256'],file_hash(p));inputs['files']['sealed.json']='c'*64;p.write_text(json.dumps(inputs))
   with self.assertRaisesRegex(ValueError,'manifests differ'):compact_deployment(model,p)
   inputs['files']['sealed.json']='a'*64;inputs['files']['map-splits.json']='d'*64;p.write_text(json.dumps(inputs))
   with self.assertRaisesRegex(ValueError,'splits differ'):compact_deployment(model,p)

class CampaignFamilyScopeTests(unittest.TestCase):
 def test_scope_is_explicit_unique_and_known(self):
  from tools.adaptive_gradient_model import expected_family_scope
  self.assertEqual(expected_family_scope(','.join(d.FAMILIES)),tuple(d.FAMILIES))
  self.assertEqual(expected_family_scope('materials,guard,clear,forbidden'),('materials','guard','clear','forbidden'))
  for value in ['',',materials','materials,','materials,materials','unknown','materials, guard']:
   with self.assertRaises(ValueError):expected_family_scope(value)
 def test_gate_requires_declared_family_without_imputing_other_families(self):
  from tools.adaptive_gradient_model import require_family_support
  rows=[dict(family='materials',group='map-a')]
  coverage=require_family_support(rows,1,('materials',))
  self.assertEqual(coverage['markets']['episodes'],0)
  self.assertEqual(rows,[dict(family='materials',group='map-a')])
  with self.assertRaisesRegex(ValueError,'markets'):require_family_support(rows,1,tuple(d.FAMILIES))
  with self.assertRaisesRegex(ValueError,'materials'):require_family_support(rows,2,('materials',))

if __name__=='__main__':unittest.main()

class SelectedBase7Tests(unittest.TestCase):
 def test_changed_tooling_is_python311_syntax(self):
  import ast
  for name in ['adaptive_gradient_collection','adaptive_gradient_codegen','adaptive_gradient_dataset','adaptive_gradient_model']:
   ast.parse((ROOT/'tools'/f'{name}.py').read_text(),feature_version=(3,11))
 def model(self):
  value=shared_model();value.update(features=list(d.BASE_FEATURES),transforms=list(d.TRANSFORMS[:7]),trained=True)
  for index,name in enumerate(('positive_probability','negative_probability','positive_mean','negative_mean')):value[name]=dict(coef=[((-1)**(i+index))*(i+1)*5000 for i in range(7)],bias=(index+1)*123456789,shift=28)
  return value
 def test_strict_selection_and_counter_independence(self):
  model=self.model();validate(model);self.assertEqual(predict(model,[32,17,99,3,7,123,456],[0,0,0]),predict(model,[32,17,99,3,7,123,456],[2**63,1,2**64-1]))
  for features in [list(d.BASE_FEATURES[:-1]),list(reversed(d.BASE_FEATURES))]:
   with self.assertRaises(ValueError):validate(dict(model,features=features))
 def test_base7_cpp_parity_and_preserved_counter_abi(self):
  compiler=shutil.which('clang++') or shutil.which('g++')
  if not compiler:self.skipTest('C++20 compiler unavailable')
  from tools.adaptive_gradient_codegen import render
  model=self.model();header=render(model);self.assertIn('SelectedFeatureCount=7',header);self.assertIn('CounterCount=3',header);self.assertNotIn('inline I fraction(',header);self.assertNotIn('auto bounded=',header)
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp);(p/'model.h').write_text(header);(p/'main.cpp').write_text('#include "model.h"\n#include <iostream>\nint main(){AdaptiveGradientModel::BaseFeatures b;while(std::cin>>b[0]>>b[1]>>b[2]>>b[3]>>b[4]>>b[5]>>b[6])std::cout<<AdaptiveGradientModel::predict(b,{0,0,0})<<" "<<AdaptiveGradientModel::predict(b,{UINT64_MAX,UINT64_MAX,UINT64_MAX})<<"\\n";}')
   subprocess.run([compiler,'-std=c++20','-O2','-fsanitize=undefined',str(p/'main.cpp'),'-o',str(p/'run')],check=True,capture_output=True)
   vectors=[[0]*7,[1]*7,[1000000]*7,[2**63-1]*7,[32,17,99,3,7,123,456]];result=subprocess.check_output([str(p/'run')],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),text=True).splitlines()
   for vector,line in zip(vectors,result):self.assertEqual(list(map(int,line.split())),[predict(model,vector,[0,0,0])]*2)

class FamilyExpertDeploymentTests(unittest.TestCase):
 def model(self):
  value=shared_model();value.update(schema=5,features=list(d.FEATURES),transforms=list(d.TRANSFORMS),trained=True)
  value['deployment']=dict(threshold_milli=value['threshold'],strategy='family-experts-with-combined-fallback',demanded_age_cap_ticks=1024)
  value['family_experts']={}
  for identifier in (0,2,3,4):
   heads={name:dict(coef=[0]*12,bias=0,shift=28) for name in ('positive_probability','negative_probability','positive_mean','negative_mean')}
   heads['positive_probability']['bias']=12000*(1<<28);heads['negative_probability']['bias']=-12000*(1<<28);heads['positive_mean']['bias']=(identifier+1)*1000*(1<<28)
   value['family_experts'][str(identifier)]=dict(family=d.FAMILIES[identifier],training_support=dict(rows=3,maps=1,positive=1,negative=1,zero=1),**heads)
  return value
 def test_dispatch_known_ids_and_combined_unmeasured_or_future_fallback(self):
  from tools.resource_gradient_model import predict as raw_predict
  model=self.model();validate(model);base=[32,1,2,3,4,5,6];counts=[1,1,2];raw=d.prepare(base,counts)
  for identifier in (0,2,3,4):
   selected=dict(model,**{name:model['family_experts'][str(identifier)][name] for name in ('positive_probability','negative_probability','positive_mean','negative_mean')})
   self.assertEqual(predict(model,base,counts,identifier),raw_predict(selected,raw))
  for identifier in (None,1,5,255,2**32-1):self.assertEqual(predict(model,base,counts,identifier),raw_predict(model,raw))
  for identifier in (-1,True,2**32):
   with self.assertRaises(ValueError):predict(model,base,counts,identifier)
 def test_cpp_family_dispatch_and_unknown_fallback(self):
  compiler=shutil.which('clang++') or shutil.which('g++')
  if not compiler:self.skipTest('C++20 compiler unavailable')
  from tools.adaptive_gradient_codegen import render
  model=self.model()
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);(root/'model.h').write_text(render(model))
   (root/'main.cpp').write_text('#include "model.h"\n#include <iostream>\nint main(){unsigned family;while(std::cin>>family)std::cout<<AdaptiveGradientModel::predict({32,1,2,3,4,5,6},{1,1,2},family)<<"\\n";}')
   subprocess.run([compiler,'-std=c++20','-O2','-fsanitize=undefined',str(root/'main.cpp'),'-o',str(root/'run')],check=True,capture_output=True)
   families=[0,1,2,3,4,5,255,2**32-1]
   actual=subprocess.check_output([str(root/'run')],input=''.join(str(f)+'\n' for f in families),text=True).splitlines()
   self.assertEqual(list(map(int,actual)),[predict(model,[32,1,2,3,4,5,6],[1,1,2],f) for f in families])
 def test_rejects_hidden_unmeasured_mismatched_or_unsafe_experts(self):
  changes=[]
  value=self.model();value['family_experts']['00']=value['family_experts'].pop('0');changes.append(value)
  value=self.model();value['family_experts']['5']=value['family_experts'].pop('0');changes.append(value)
  value=self.model();value['family_experts']['0']['family']='guard';changes.append(value)
  value=self.model();value['family_experts']['0']['training_support']['maps']=0;changes.append(value)
  value=self.model();value['family_experts']['0']['positive_mean']['coef'][0]=1<<61;changes.append(value)
  value=self.model();value['schema']=4;changes.append(value)
  value=self.model();value['deployment']['demanded_age_cap_ticks']=True;changes.append(value)
  for value in changes:
   with self.assertRaises(ValueError):validate(value)
 def test_emit_compacts_experts_without_relabeling_historical_cap_metrics(self):
  from tools.adaptive_gradient_model import emit
  from tools.adaptive_gradient_codegen import render
  from types import SimpleNamespace
  model=self.model();model.update(schema=2,research_only=True,candidate_contract=d.CONTRACT)
  model['provenance']=dict(adaptive_policy={'forced_age_cap':None},holdout={'decision_regret':123},expert_fit_sha256='a'*64,combined_fit_sha256='b'*64)
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp);source=p/'research.json';source.write_text(json.dumps(model));before=file_hash(source)
   emit(SimpleNamespace(model=str(source),contract=d.CONTRACT_ID,threshold_milli=16000,compact=True,output=str(p/'model.json'),header=str(p/'model.h')))
   actual=json.loads((p/'model.json').read_text());validate(actual)
   self.assertEqual(actual['family_experts'],model['family_experts']);self.assertEqual(actual['deployment']['demanded_age_cap_ticks'],1024);self.assertIsNone(actual['deployment']['source_fit_age_cap_ticks']);self.assertNotIn('holdout',actual['provenance']);self.assertEqual(file_hash(source),before);self.assertEqual((p/'model.h').read_text(),render(actual));self.assertLess(len(json.dumps(actual)),24000)
 def test_fit_experts_preserves_global_weights_and_omits_unmeasured_families(self):
  try:import numpy as np
  except ImportError:self.skipTest('numpy unavailable')
  from unittest.mock import patch
  from tools.adaptive_gradient_model import fit_family_experts
  rows=[dict(format='1v1',cohort_role='exploration',cohort='c',group=g,family=f,collection=g+f,defer_extra=1,refresh_extra=0) for g,f in [('a','materials'),('a','guard'),('b','materials'),('c','materials'),('c','guard')]]
  expected={id(row):weight for row,weight in zip(rows,d.balanced_weights(rows))};seen=[]
  def fitted(actual,features,transforms,weighting,threshold):
   got=weighting(actual);want=np.asarray([expected[id(row)] for row in actual]);want*=len(want)/sum(want);np.testing.assert_allclose(got,want,rtol=0,atol=1e-15);seen.append(actual[0]['family'])
   return {name:dict(coef=[0]*12,bias=0,shift=28) for name in ('positive_probability','negative_probability','positive_mean','negative_mean')}
  with patch('tools.adaptive_gradient_model.integer.fit_heads',side_effect=fitted):experts=fit_family_experts(rows,16000)
  self.assertEqual(set(experts),{'0','2'});self.assertEqual(set(seen),{'materials','guard'})
 def test_mixture_metrics_uses_expert_predictions_without_rebalancing_rows(self):
  try:import numpy as np
  except ImportError:self.skipTest('numpy unavailable')
  from tools.adaptive_gradient_model import model_metrics
  from tools.resource_gradient_model import metrics
  model=self.model();rows=[dict(zip(d.FEATURES,[32,1,2,3,4,5,6,1,1,2,2048,2048]),family_id=family,group='g',defer_extra=20,refresh_extra=1) for family in [0,1,2,255]]
  weights=lambda actual:np.asarray([1,2,3,4],float)
  actual=model_metrics(rows,model,weights);expected=metrics(rows,model,weighting=weights,prediction=lambda row:predict(model,[row[k] for k in d.BASE_FEATURES],[row[k] for k in d.COUNTERS],row['family_id']))
  self.assertEqual(actual,expected)
