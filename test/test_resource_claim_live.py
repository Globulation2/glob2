#!/usr/bin/env python3
"""Live plans retain exact paired tuples and reject infeasible allocations."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.common import digest
from tools.tournaments.resource_claim_live import factor_configs, fractional_configs, plan
from tools.tournaments.claim_policy import INTERVALS, RECOVERY_V1, policy_digest
from tools.tournaments.model import job


def fixture():
    p = {'source_version': 1, 'pattern': 'spiral:k16', 'pattern_digest': 'a'*64,
         'selection': 'first_available', 'fallback': 'wait', 'fallback_after_decisions': 16,
         'destination_refresh_j': 0, 'route_refresh_j': 0, 'route_target': 'claimed_access',
         'pending_step': 'wait', 'maximum_expansions': 4096, 'use_primary_gradient': False}
    bundle = {'id': 'b'*64, 'source_revision': 'c'*40, 'capabilities': {
        'material_destinations_version': 1, 'resource_claims_probe_version': 1,
        'resource_claims_policy_config_version': 1, 'resource_claims_active_policies': ['experimental'],
        'resource_claims_policy_patterns': [{'name': p['pattern'], 'digest': p['pattern_digest']}]}}
    split = {'games': {'g1': 'discovery', 'g2': 'holdout'}, 'strata': {'g1': 'pooled', 'g2': 'protected'},
             'cohort_identity': 'd'*64}
    split['identity'] = digest(split)
    jobs = []
    for i, game in enumerate(split['games']):
        jobs.append(job('game', 'e'*64, seeds={'map': 100+i, 'game': 200+i},
            config={'generator': 1, 'params': {'width': 8, 'height': 8, 'teams': 2},
                    'players': ['cortex', 'maxima'], 'ticks': 180000, 'experiments': ['farm-areas'],
                    'material_destinations': True, 'resource_claims_shadow': {
                        'sample_every': 256, 'max_samples': 32768, 'max_bytes': 134217728},
                    'win_probability_permille': 950},
            outputs={'required': ['resource-claims-shadow.jsonl.gz']},
            limits={'artifact_bytes': 1024, 'log_bytes': 512, 'timeout_seconds': 21600},
            labels={'initial_state_identity': game, 'discovery_split': split['games'][game], 'farm_mode': split['strata'][game]}))
    return p, bundle, split, {'id': 'frozen32', 'jobs': jobs, 'design': {'split_identity': split['identity']}}


class LivePlans(unittest.TestCase):
    def test_probe_capability_versions_are_explicit(self):
        from tools.tournaments.experiments import Planner
        p,bundle,split,cohort=fixture()
        for version in (1,2):
            bundle['capabilities']['resource_claims_probe_version']=version
            Planner('test',{'resource_claims_probe':{}},[bundle])
            plan(cohort,split,bundle,[{'label':'control','policy':p}])
        for version in (None,0,3):
            bundle['capabilities']['resource_claims_probe_version']=version
            with self.assertRaises(ValueError):Planner('test',{'resource_claims_probe':{}},[bundle])
            with self.assertRaises(ValueError):plan(cohort,split,bundle,[{'label':'control','policy':p}])

    def test_adjacent_pickup_comparison_requires_transport_audit_on_every_arm(self):
        p,bundle,split,cohort=fixture();bundle['capabilities']['resource_claims_policy_config_version']=3
        current=p|{'source_version':3,'opportunistic_adjacent_pickup':True}|RECOVERY_V1
        configs=[{'label':'sticky','policy':p},{'label':'adjacent','policy':current}]
        with self.assertRaisesRegex(ValueError,'movement audit'):plan(cohort,split,bundle,configs)
        bundle['capabilities']['resource_claims_movement_audit_version']=1
        result=plan(cohort,split,bundle,configs)
        self.assertTrue(all(row['labels']['require_movement_audit'] for row in result['jobs']))
        self.assertEqual({row['policy']['opportunistic_adjacent_pickup'] for row in factor_configs(current)},{False,True})

    def test_fallback_batch_requires_movement_coverage_for_control_too(self):
        p,bundle,split,cohort=fixture()
        configs=[{'label':'control','policy':p},{'label':'roam','policy':p|{'fallback':'roam'}}]
        with self.assertRaisesRegex(ValueError,'movement audit'):
            plan(cohort,split,bundle,configs)
        bundle['capabilities']['resource_claims_movement_audit_version']=1
        result=plan(cohort,split,bundle,configs)
        self.assertTrue(all(j['labels']['require_movement_audit'] for j in result['jobs']))
    def test_recovery_identity_is_frozen_across_factor_sweep_and_plan(self):
        p, bundle, split, cohort = fixture()
        p = p | {'source_version': 2} | RECOVERY_V1
        bundle['capabilities']['resource_claims_policy_config_version'] = 2
        configs = factor_configs(p)
        self.assertEqual(len(configs), 50)
        for row in configs:
            self.assertEqual(row['policy']['source_version'], 2)
            self.assertEqual({k: row['policy'][k] for k in RECOVERY_V1}, RECOVERY_V1)
        result = plan(cohort, split, bundle, configs[:2])
        self.assertEqual([j['config']['resource_claims_policy'] for j in result['jobs']],
                         [row['policy'] for row in configs[:2]])
        bundle['capabilities']['resource_claims_policy_config_version'] = 1
        with self.assertRaisesRegex(ValueError, 'version 2'):
            plan(cohort, split, bundle, configs[:2])

    def test_ofat_all_intervals_and_no_crossproduct(self):
        p, _, _, _ = fixture(); configs = factor_configs(p)
        self.assertEqual(len(configs), 50)
        self.assertEqual(len(configs), len({policy_digest(row['policy']) for row in configs}))
        for interval in INTERVALS:
            for changes in ({'destination_refresh_j': interval}, {'route_refresh_j': interval},
                            {'destination_refresh_j': interval, 'route_refresh_j': interval}):
                self.assertIn(dict(p, **changes), [r['policy'] for r in configs])
        self.assertEqual({r['policy']['fallback'] for r in configs}, {'wait', 'roam', 'release_job', 'bounded_wait_release'})

    def test_paired_source_split_and_inputs_unchanged(self):
        p, bundle, split, cohort = fixture(); original = copy.deepcopy(cohort)
        configs = [{'label': 'control', 'policy': p}, {'label': 'heading', 'policy': p | {'pending_step': 'direct_heading'}}]
        result = plan(cohort, split, bundle, configs)
        self.assertEqual(cohort, original)
        self.assertEqual(len(result['jobs']), 2)
        for item in result['jobs']:
            self.assertEqual(item['seeds'], cohort['jobs'][0]['seeds'])
            for field in ('generator', 'params', 'players', 'experiments', 'ticks', 'win_probability_permille'):
                self.assertEqual(item['config'][field], cohort['jobs'][0]['config'][field])
            self.assertEqual(item['labels']['initial_state_identity'], 'g1')
            self.assertEqual(item['labels']['discovery_split'], 'discovery')
            self.assertNotIn('resource_claims_shadow', item['config'])
            self.assertEqual(item['build'], bundle['id'])
            self.assertEqual(item['outputs']['log_mode'],'gameplay')
            self.assertIn('team-timeline',item['outputs']['telemetry'])
            self.assertIs(item['labels']['require_gameplay_telemetry'],True)
            self.assertEqual(item['limits']['log_bytes'],128*1024**2)
        self.assertEqual(result['design']['reserved_bytes'],4*(1024+128*1024**2))
        held = plan(cohort, split, bundle, configs, stage='confirmation', partition='holdout')
        self.assertEqual({j['labels']['initial_state_identity'] for j in held['jobs']}, {'g2'})

    def test_fail_closed_budgets_assets_and_holdout(self):
        p, bundle, split, cohort = fixture(); configs = [{'label': 'control', 'policy': p}]
        for kwargs in ({'max_jobs': 0}, {'campaign_bytes': 3000}, {'free_bytes': 0},
                       {'additional_reserved_bytes': -1}, {'partition': 'holdout'}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                plan(cohort, split, bundle, configs, **kwargs)
        with self.assertRaises(ValueError): plan(cohort, split, bundle, configs*2)
        with self.assertRaises(ValueError): plan(cohort, split | {'identity': 'bad'}, bundle, configs)
        invalid = copy.deepcopy(bundle); invalid['capabilities']['resource_claims_policy_patterns'] = []
        with self.assertRaises(ValueError): plan(cohort, split, invalid, configs)
        with self.assertRaises(ValueError): plan(cohort | {'jobs': cohort['jobs'][:1]}, split, bundle, configs)

    def test_fraction_is_small_explicit_and_validated(self):
        p, _, _, _ = fixture()
        factors = {'pending_step': ['wait', 'direct_heading'], 'use_primary_gradient': [False, True],
                   'route_target': ['claimed_access', 'probe_then_gradient'], 'selection': ['first_available', 'closest_geometric']}
        result = fractional_configs(p, factors)
        self.assertEqual(len(result['configs']), 8)
        self.assertEqual(result['design']['resolution'], 4)
        self.assertIn('Aliased', result['design']['limitation'])
        with self.assertRaises(ValueError): fractional_configs(p, {'pending_step': ['wait', 'direct_heading']})
        with self.assertRaises(ValueError): fractional_configs(p, factors | {'fallback': ['wait', 'bad']})


if __name__ == '__main__': unittest.main()
