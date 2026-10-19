#!/usr/bin/env python3
"""Admission behavior metadata is independent from the immutable label contract."""
import copy
import unittest
from tools import adaptive_gradient_dataset as data
from tools.adaptive_gradient_tournament import ADAPTIVE_POLICY,policy_from_bundles

class ModelThresholdPolicyTests(unittest.TestCase):
 def bundle(self,value,cap=1024):
  return {'build_options':{'refresh_threshold_milli':value,'refresh_age_cap_ticks':cap}}
 def test_default_matches_selected_a16_with_demanded_age_cap(self):
  self.assertEqual(ADAPTIVE_POLICY['threshold_milli'],16000)
  self.assertEqual(ADAPTIVE_POLICY['threshold_milli'],data.DEFAULT_THRESHOLD_MILLI)
  self.assertEqual(ADAPTIVE_POLICY['minimum_interval'],8)
  self.assertEqual(ADAPTIVE_POLICY['publication_delay'],8)
  self.assertEqual(ADAPTIVE_POLICY['horizon_ticks'],128)
  self.assertEqual(ADAPTIVE_POLICY['forced_age_cap'],1024)
 def test_qualified_model_threshold_overrides_current_tool_default(self):
  before=copy.deepcopy(ADAPTIVE_POLICY)
  self.assertEqual(policy_from_bundles([self.bundle(16000),self.bundle(16000)])['threshold_milli'],16000)
  self.assertEqual(policy_from_bundles([self.bundle(128000)])['threshold_milli'],128000)
  self.assertEqual(policy_from_bundles([self.bundle(17001)])['threshold_milli'],17001)
  self.assertEqual(ADAPTIVE_POLICY,before)
 def test_pool_models_must_attest_same_threshold(self):
  with self.assertRaisesRegex(ValueError,'agree'):
   policy_from_bundles([self.bundle(16000),self.bundle(128000)])
 def test_missing_attestation_does_not_silently_assume_new_default(self):
  with self.assertRaisesRegex(ValueError,'attest'):
   policy_from_bundles([{'build_options':{'model_sha256':'a'*64}}])
  with self.assertRaisesRegex(ValueError,'agree'):
   policy_from_bundles([])
 def test_threshold_metadata_requires_exact_positive_signed64_integer(self):
  for bad in [True,False,16000.0,'16000',None,0,-1,1<<63]:
   with self.subTest(value=bad),self.assertRaises(ValueError):
    policy_from_bundles([self.bundle(bad)])
  self.assertEqual(policy_from_bundles([self.bundle((1<<63)-1)])['threshold_milli'],(1<<63)-1)

 def test_fresh_bundles_must_attest_an_actual_common_cap(self):
  before=copy.deepcopy(ADAPTIVE_POLICY)
  for cap in [8,512,1024,4096,(1<<32)-1]:
   with self.subTest(cap=cap):
    self.assertEqual(policy_from_bundles([self.bundle(16000,cap),self.bundle(16000,cap)])['forced_age_cap'],cap)
  self.assertEqual(ADAPTIVE_POLICY,before)
  with self.assertRaisesRegex(ValueError,'caps must agree'):
   policy_from_bundles([self.bundle(16000,512),self.bundle(16000,1024)])
 def test_old_missing_or_uncapped_bundle_is_not_silently_relabelled(self):
  historical={'build_options':{'refresh_threshold_milli':128000}}
  before=copy.deepcopy(historical)
  with self.assertRaisesRegex(ValueError,'refresh_age_cap_ticks'):
   policy_from_bundles([historical])
  self.assertEqual(historical,before)
  for cap in [None,True,False,1024.0,'1024',0,-1,7,1<<32]:
   with self.subTest(cap=cap),self.assertRaisesRegex(ValueError,'refresh_age_cap_ticks'):
    policy_from_bundles([self.bundle(16000,cap)])

class DirectExperimentThresholdTests(unittest.TestCase):
 def case(self,kind,option='missing',policy_threshold=16000):
  import tempfile
  from pathlib import Path
  from tools.test_resource_gradient_distributed import DistributedTests
  from tools import resource_gradient_distributed as distributed,adaptive_gradient_collection as collector
  temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
  root=Path(temporary.name);fixture=DistributedTests();options={}
  if option!='missing':options['refresh_threshold_milli']=option
  if kind=='unified':options['adaptive_gradient_contract_sha256']=distributed.canonical_hash(collector.CONTRACT)
  bundle=fixture.bundle(root,'linux',options);plan,maps=fixture.plan(root,1);cohort=fixture.cohort();cohort['policy']['threshold_milli']=policy_threshold
  if kind=='unified':cohort['contract']=copy.deepcopy(collector.CONTRACT)
  return distributed,plan,maps,bundle,cohort
 def test_direct_constructor_rejects_explicit_bundle_cohort_mismatch(self):
  for kind in ['legacy','unified']:
   with self.subTest(contract=kind):
    module,plan,maps,bundle,cohort=self.case(kind,128000)
    with self.assertRaisesRegex(ValueError,'threshold differs'):
     module.make_experiment(plan,maps,[bundle],cohort)
 def test_direct_constructor_accepts_matching_explicit_behavior(self):
  for kind in ['legacy','unified']:
   with self.subTest(contract=kind):
    module,plan,maps,bundle,cohort=self.case(kind,16000)
    manifest=module.make_experiment(plan,maps,[bundle],cohort)
    self.assertEqual(manifest['jobs'][0]['labels']['resource_gradient']['cohort']['policy']['threshold_milli'],16000)
 def test_missing_historical_bundle_attestation_remains_compatible(self):
  for kind in ['legacy','unified']:
   with self.subTest(contract=kind):
    module,plan,maps,bundle,cohort=self.case(kind)
    self.assertEqual(len(module.make_experiment(plan,maps,[bundle],cohort)['jobs']),1)
 def test_present_invalid_attestation_is_not_missing_legacy_metadata(self):
  for value in [None,True,'16000',0,-1,1<<63]:
   with self.subTest(value=value):
    module,plan,maps,bundle,cohort=self.case('unified',value)
    with self.assertRaises(ValueError):module.make_experiment(plan,maps,[bundle],cohort)

 def test_capped_direct_constructor_requires_matching_explicit_bundle_cap(self):
  module,plan,maps,bundle,cohort=self.case('unified',16000)
  cohort['policy']['forced_age_cap']=1024
  for attested in ['missing',None,True,1024.0,'1024',0,7,-1,1<<32,512]:
   with self.subTest(cap=attested):
    supplied=copy.deepcopy(bundle)
    if attested!='missing':supplied['build_options']['refresh_age_cap_ticks']=attested
    with self.assertRaisesRegex(ValueError,'[Aa]ge.cap|refresh_age_cap_ticks'):
     module.make_experiment(plan,maps,[supplied],cohort)
  bundle['build_options']['refresh_age_cap_ticks']=1024
  actual=module.make_experiment(plan,maps,[bundle],cohort)
  self.assertEqual(actual['jobs'][0]['labels']['resource_gradient']['cohort']['policy']['forced_age_cap'],1024)
 def test_direct_historical_uncapped_metadata_remains_unchanged(self):
  module,plan,maps,bundle,cohort=self.case('unified',16000)
  cohort['policy']['forced_age_cap']=None;before=copy.deepcopy(cohort)
  result=module.make_experiment(plan,maps,[bundle],cohort)
  self.assertEqual(result['jobs'][0]['labels']['resource_gradient']['cohort'],before)
  self.assertEqual(cohort,before)
 def test_direct_constructor_rejects_invalid_explicit_cohort_cap(self):
  for cap in [True,False,7,0,-1,1024.0,'1024',1<<32]:
   with self.subTest(cap=cap):
    module,plan,maps,bundle,cohort=self.case('unified',16000)
    cohort['policy']['forced_age_cap']=cap;bundle['build_options']['refresh_age_cap_ticks']=1024
    with self.assertRaisesRegex(ValueError,'collection age cap'):
     module.make_experiment(plan,maps,[bundle],cohort)

class CurrentCollectorPlanTests(unittest.TestCase):
 def test_new_plans_emit_v4_and_do_not_reinterpret_historical_plans(self):
  from tools import adaptive_gradient_tournament as tournament,adaptive_gradient_collection as collector
  from tools.test_resource_gradient_tournament import catalog
  document=tournament.plan(catalog(),181106,'remote-only')
  self.assertEqual(document['unified']['contract'],collector.CONTRACT)
  self.assertNotIn('spatial',document['unified']['contract'])
  document['unified']['contract']=collector.LEGACY_CONTRACT
  with self.assertRaisesRegex(ValueError,'Exact unified'):
   tournament.validate_plan(document)

if __name__=='__main__':unittest.main()
