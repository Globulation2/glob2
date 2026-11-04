#!/usr/bin/env python3
"""Frozen live experimental policy transport and executable attestation."""
import json
import gzip
import io
from unittest.mock import patch
import random
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.claim_policy import validate_policy, canonical_policy, policy_digest, RECOVERY_V1
from tools.tournaments.jobs import EngineJob


def policy():
    return {'source_version': 1, 'pattern': 'spiral:k16:unit:s1:every:forward:d0:seed1',
        'pattern_digest': 'a'*64, 'selection': 'first_available', 'fallback': 'wait',
        'fallback_after_decisions': 16, 'destination_refresh_j': 0, 'route_refresh_j': 0,
        'route_target': 'claimed_access', 'pending_step': 'wait', 'maximum_expansions': 4096,
        'use_primary_gradient': False}


def job():
    return {'config': {'material_destinations': True, 'resource_claims_policy': policy(),
        'players': ['cortex', 'cortex'], 'ticks': 64}, 'inputs': {'map': 'frozen-map'},
        'seeds': {'game': 17}, 'outputs': {}, 'limits': {}, 'labels': {}}


class LivePolicy(unittest.TestCase):
    def test_direct_resource_probes_require_source_four_and_keep_old_hashes(self):
        hashes=['f5b1257cb0263acb94c41fb96def0016065fc49f5db3c257edd9d4fc368351f4','3503d4c91a7af35b39f4969589d02fb4b7fe532b088b3d561026b66d2e234154','eda0c89c9e6b6dad7d16b66bdf9be667ca0a5646b0e05809c622a3a94e7629d1']
        for version in (1,2,3):
            old=policy()|({'source_version':version}|RECOVERY_V1 if version>=2 else {})|({'opportunistic_adjacent_pickup':True} if version>=3 else {})
            self.assertEqual(policy_digest(old),hashes[version-1])
            with self.assertRaises(ValueError):validate_policy(old|{'direct_resource_probes':False})
        current=old|{'source_version':4,'direct_resource_probes':False}
        caps={'resource_claims_policy_config_version':4,'resource_claims_active_policies':['experimental'],
              'resource_claims_policy_patterns':[{'name':current['pattern'],'digest':current['pattern_digest']}]}
        for enabled in (False,True):
            self.assertEqual(validate_policy(current|{'direct_resource_probes':enabled},caps)['direct_resource_probes'],enabled)
        self.assertNotEqual(policy_digest(current),policy_digest(current|{'direct_resource_probes':True}))
        for field in ('direct_resource_probes','opportunistic_adjacent_pickup'):
            for bad in (None,0,1,'true'):
                with self.assertRaises(ValueError):validate_policy(current|{field:bad})
            with self.assertRaises(ValueError):validate_policy({k:v for k,v in current.items() if k!=field})
        with self.assertRaises(ValueError):validate_policy(current,caps|{'resource_claims_policy_config_version':3})
        with self.assertRaises(ValueError):validate_policy(current,caps|{'resource_claims_policy_config_version':6})

    def test_adjacent_pickup_policy_requires_explicit_version_and_boolean(self):
        legacy=policy()|{'source_version':2}|RECOVERY_V1
        caps={'resource_claims_policy_config_version':3,'resource_claims_active_policies':['experimental'],
              'resource_claims_policy_patterns':[{'name':legacy['pattern'],'digest':legacy['pattern_digest']}]}
        self.assertNotIn('opportunistic_adjacent_pickup',json.loads(canonical_policy(legacy)))
        self.assertEqual(validate_policy(legacy,caps),legacy)
        for enabled in (False,True):
            current=legacy|{'source_version':3,'opportunistic_adjacent_pickup':enabled}
            self.assertEqual(validate_policy(current,caps),current)
            self.assertNotEqual(policy_digest(current),policy_digest(legacy))
            with self.assertRaisesRegex(ValueError,'version 3'):
                validate_policy(current,caps|{'resource_claims_policy_config_version':2})
        for bad in (legacy|{'opportunistic_adjacent_pickup':False},legacy|{'source_version':3},
                    legacy|{'source_version':3,'opportunistic_adjacent_pickup':1}):
            with self.subTest(bad=bad),self.assertRaises(ValueError):validate_policy(bad)

    def test_recovery_version_is_explicit_and_old_policy_identity_is_stable(self):
        old = policy()
        self.assertEqual(policy_digest(old), 'f5b1257cb0263acb94c41fb96def0016065fc49f5db3c257edd9d4fc368351f4')
        current = old | {'source_version': 2} | RECOVERY_V1
        self.assertEqual(validate_policy(current), current)
        self.assertNotEqual(policy_digest(current), policy_digest(old))
        caps = {'resource_claims_policy_config_version': 2,
            'resource_claims_active_policies': ['experimental'],
            'resource_claims_policy_patterns': [{'name': old['pattern'], 'digest': old['pattern_digest']}]}
        self.assertEqual(validate_policy(old, caps), old)
        self.assertEqual(validate_policy(current, caps), current)
        with self.assertRaisesRegex(ValueError, 'version 2'):
            validate_policy(current, caps | {'resource_claims_policy_config_version': 1})
        for key, value in RECOVERY_V1.items():
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    validate_policy({k: v for k, v in current.items() if k != key})
                with self.assertRaisesRegex(ValueError, 'recovery parameter'):
                    validate_policy(current | {key: value + 1 if type(value) is int else 'closest_geometric'})
                with self.assertRaises(ValueError):
                    validate_policy(old | {key: value})
                if type(value) is int:
                    with self.assertRaisesRegex(ValueError, 'recovery parameter'):
                        validate_policy(current | {key: True})

    def test_recovery_config_survives_isolated_worker_transport_and_result_attestation(self):
        from tools.tournaments.transport import worker_archive
        p = policy() | {'source_version': 2} | RECOVERY_V1
        item = job(); item['config']['resource_claims_policy'] = p
        EngineJob('game').validate(item)
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'worker.pyz'; worker_archive(archive)
            script = ('import json,sys;sys.path.insert(0,sys.argv[1]);'
                'from tournaments.claim_policy import policy_digest;'
                'print(policy_digest(json.loads(sys.argv[2])))')
            done = subprocess.run([sys.executable, '-I', '-S', '-c', script,
                str(archive), canonical_policy(p)], cwd=directory, capture_output=True, text=True, check=True)
            self.assertEqual(done.stdout.strip(), policy_digest(p))
            out = Path(directory)/'output'; out.mkdir()
            result = {'schema_version': 1, 'status': 'completed', 'resolved': {
                'active_claim_policy': 'experimental', 'material_destinations_enabled': True,
                'resource_claims_policy_config': p, 'resource_claims_policy_digest': policy_digest(p)}}
            (out/'result.json').write_text(json.dumps(result))
            self.assertEqual(EngineJob('game').collect(item, directory), result)
            changed = p | {'source_version': 1}
            for key in RECOVERY_V1: changed.pop(key)
            result['resolved'].update(resource_claims_policy_config=changed,
                                      resource_claims_policy_digest=policy_digest(changed))
            (out/'result.json').write_text(json.dumps(result))
            with self.assertRaises(ValueError): EngineJob('game').collect(item, directory)

    def test_complete_config_and_pinned_asset_required(self):
        p = policy()
        caps = {'resource_claims_policy_config_version': 1,
            'resource_claims_active_policies': ['experimental'],
            'resource_claims_policy_patterns': [{'name': p['pattern'], 'digest': p['pattern_digest']}]}
        self.assertEqual(validate_policy(p, caps), p)
        with self.assertRaises(ValueError): validate_policy(p, caps | {'resource_claims_policy_config_version': True})
        self.assertEqual(policy_digest(p), policy_digest(dict(reversed(list(p.items())))))
        for key, value in (('destination_refresh_j', True), ('route_refresh_j', 5),
                           ('maximum_expansions', 0), ('use_primary_gradient', 1),
                           ('selection', 'unknown'), ('pattern_digest', 'b'*63)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_policy(p | {key: value})
        with self.assertRaises(ValueError): validate_policy({k: v for k, v in p.items() if k != 'pending_step'})
        with self.assertRaisesRegex(ValueError, 'asset absent'):
            validate_policy(p, caps | {'resource_claims_policy_patterns': []})

    def test_modes_arguments_and_saved_state_boundary(self):
        adapter = EngineJob('game'); item = job(); adapter.validate(item)
        args = adapter.command(item, {'directory': '/bundle', 'executable': 'glob2'}, '/attempt', {'map': '/map'})
        self.assertEqual(args[args.index('--resource-claims-policy-json')+1], canonical_policy(policy()))
        self.assertNotIn('--resource-claims-active', args)
        for key, value in (('material_destinations', False), ('resource_claims_active', 'prototype-stationary-v1'),
                           ('resource_claims_shadow', {})):
            with self.subTest(key=key), self.assertRaises(ValueError):
                adapter.validate(item | {'config': item['config'] | {key: value}})
        with self.assertRaises(ValueError):
            adapter.validate(item | {'inputs': {'save': 'frozen'}, 'seeds': {}})

    def test_result_attests_exact_config_and_digest(self):
        item = job(); p = policy()
        resolved = {'material_destinations_enabled': True, 'active_claim_policy': 'experimental',
            'resource_claims_policy_config': p, 'resource_claims_policy_digest': policy_digest(p)}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'output'; output.mkdir()
            result = {'schema_version': 1, 'status': 'completed', 'resolved': resolved}
            path = output/'result.json'; path.write_text(json.dumps(result))
            self.assertEqual(EngineJob('game').collect(item, directory), result)
            for key, value in (('active_claim_policy', 'prototype-stationary-v1'),
                               ('resource_claims_policy_digest', 'b'*64),
                               ('resource_claims_policy_config', p | {'route_refresh_j': 1}),
                               ('resource_claims_policy_config', p | {'use_primary_gradient': 0})):
                with self.subTest(key=key):
                    path.write_text(json.dumps(result | {'resolved': resolved | {key: value}}))
                    with self.assertRaisesRegex(ValueError, 'claim policy|primary gradient'):
                        EngineJob('game').collect(item, directory)

    def test_worker_package_needs_no_checkout_or_scientific_python(self):
        from tools.tournaments.transport import worker_archive
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'worker.pyz'; worker_archive(archive)
            script = ('import json,sys;sys.path.insert(0,sys.argv[1]);'
                'from tournaments.claim_policy import policy_digest;'
                'from tournaments.jobs import EngineJob;'
                'print(policy_digest(json.loads(sys.argv[2])))')
            completed = subprocess.run([sys.executable, '-I', '-S', '-c', script,
                str(archive), canonical_policy(policy())], cwd=directory,
                capture_output=True, text=True, check=True)
            self.assertEqual(completed.stdout.strip(), policy_digest(policy()))

    def test_planner_freezes_asset_in_sampled_and_rotated_jobs(self):
        from tools.tournaments.experiments import Planner
        p = policy()
        capabilities = {'resource_claims_policy_config_version': 1,
            'resource_claims_active_policies': ['experimental'], 'material_destinations_version': 1,
            'resource_claims_policy_patterns': [{'name': p['pattern'], 'digest': p['pattern_digest']}]}
        bundle = {'id': 'a'*64, 'capabilities': capabilities}
        config = {'resource_claims_policy': p, 'material_destinations': True}
        planner = Planner('ai_comparison', config, [bundle])
        planner.sampled_game(random.Random(19), bundle['id'], ['cortex'], [15],
                             [{'width': 7, 'height': 7}], ['1v1'])
        self.assertEqual(planner.jobs[0]['config']['resource_claims_policy'], p)
        generated = planner.generated(15, 42, 2)
        planner.game(generated, bundle['id'], 17, ['cortex', 'cortex'], 1, '1v1')
        rotated = planner.jobs[-1]
        self.assertEqual(rotated['config']['resource_claims_policy'], p)
        self.assertEqual(rotated['labels']['rotation'], 1)
        self.assertIn('map-r1', rotated['inputs']['map']['artifact'])
        # A policy sweep may change its next input after a job has been hashed.
        # Neither caller dictionaries nor planner config may mutate old jobs.
        p['route_refresh_j'] = 1
        planner.config['resource_claims_policy']['pending_step'] = 'direct_heading'
        self.assertEqual(planner.jobs[0]['config']['resource_claims_policy'], policy())
        self.assertEqual(rotated['config']['resource_claims_policy'], policy())
        with self.assertRaisesRegex(ValueError, 'asset absent'):
            Planner('ai_comparison', config, [bundle | {'capabilities': capabilities | {'resource_claims_policy_patterns': []}}])

    def test_saved_experimental_probe_inherits_frozen_save_policy(self):
        from tools.tournaments.resource_claims import ARTIFACT, COLUMNS
        sampling = {'sample_every': 1000, 'max_samples': 512, 'max_bytes': 1048576}
        item = {'config': {'ticks': 64, 'resource_claims_probe': sampling},
                'inputs': {'save': 'frozen'}, 'seeds': {}, 'outputs': {'required': [ARTIFACT]}}
        adapter = EngineJob('game'); adapter.validate(item)
        rows = [{'kind': 'schema', 'version': 1, 'patterns': ['p'], 'policies': ['first_available'],
                 'outcome_columns': list(COLUMNS), 'sample_every': 1000, 'max_samples': 512,
                 'max_output_bytes': 1048576, 'claims_enforced': True},
                {'kind': 'summary', 'complete': True, 'submitted': 0, 'completed': 0}]
        result = {'schema_version': 1, 'status': 'completed',
            'resolved': {'material_destinations_enabled': True, 'active_claim_policy': 'experimental',
                         'resource_claims_policy_config': policy(), 'resource_claims_policy_digest': policy_digest(policy())},
            'resource_claims_shadow': {'schema_version': 1, 'enforced': True, 'submitted': 0, 'completed': 0}}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'output'; output.mkdir()
            with gzip.open(output/ARTIFACT, 'wt') as stream:
                stream.write(''.join(json.dumps(row)+'\n' for row in rows))
            (output/'result.json').write_text(json.dumps(result))
            self.assertEqual(adapter.collect(item, directory), result)
            for key in ('resource_claims_policy_config', 'resource_claims_policy_digest'):
                bad = result | {'resolved': {k: v for k, v in result['resolved'].items() if k != key}}
                (output/'result.json').write_text(json.dumps(bad))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    adapter.collect(item, directory)
            result['resolved']['active_claim_policy'] = 'unknown'
            (output/'result.json').write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError, 'frozen request'):
                adapter.collect(item, directory)

    def test_analyzer_rejects_distinct_live_configs_even_with_identical_trace_schemas(self):
        from tools.tournaments.resource_claims import analyze, COLUMNS
        policies = [policy(), policy() | {'route_refresh_j': 1}]
        jobs = [{'id': str(i), 'type': 'game', 'config': {'resource_claims_policy': p}}
                for i, p in enumerate(policies)]
        records = [{'job': item, 'category': 'success', 'result': {'resolved': {
            'active_claim_policy': 'experimental', 'material_destinations_enabled': True,
            'resource_claims_policy_config': p, 'resource_claims_policy_digest': policy_digest(p)}}}
            for item, p in zip(jobs, policies)]
        rows = [{'kind': 'schema', 'version': 1, 'patterns': ['p'], 'policies': ['first_available'],
                 'outcome_columns': list(COLUMNS), 'sample_every': 1000, 'max_samples': 512,
                 'max_output_bytes': 1048576, 'claims_enforced': True},
                {'kind': 'summary', 'complete': True, 'submitted': 0, 'completed': 0}]
        packed = gzip.compress((''.join(json.dumps(row)+'\n' for row in rows)).encode())
        class Source:
            manifest = {'jobs': jobs}
            def __iter__(self): return iter(records)
            def open_artifact(self, *args): return io.BytesIO(packed)
        with patch('tools.tournaments.resource_claims.Results', return_value=Source()):
            with self.assertRaisesRegex(ValueError, 'mixed claim trace'):
                analyze('/unused', draws=5)


if __name__ == '__main__': unittest.main()
