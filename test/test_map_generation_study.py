#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structured generator-study regressions; no native build is required."""
import json
import importlib.util
import contextlib
import io
import tempfile
from types import SimpleNamespace
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import map_generation_study as study


class StudyTests(unittest.TestCase):
    def invoke(self, status='completed', code=0, malformed=False, timeout=False, change=None):
        directories = []
        def native(command, **kwargs):
            directory = Path(command[command.index('--output-dir') + 1])
            directories.append(directory)
            self.assertIn('width=7', command)
            self.assertIn('height=9', command)
            if timeout:
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])
            report = dict(generation={'outcome': {'success': True}},
                          terrain={k: {'percent': 0} for k in ('grass', 'sand', 'water')},
                          resources={'types': {k: {'coverage': {'tiles': 0}}
                              for k in ('wheat', 'wood', 'stone', 'algae', 'cherry', 'orange', 'prune')}},
                          space={'build_sites_4x4': 10, 'land_regions': {'components': 1}},
                          fertility={'all_tiles': {'mean': 1}})
            if change: change(report)
            result = dict(schema_version=1, status=status, diagnostic='native detail', map_report=report)
            (directory / 'result.json').write_text('not JSON' if malformed else json.dumps(result))
            # Misleading console output must not determine success.
            return subprocess.CompletedProcess(command, code, '[complete]', 'failure')
        with patch.object(study.subprocess, 'run', side_effect=native):
            result = study.generate_map('glob2', 60, 12, 128, 512, 6)
        self.assertTrue(directories)
        self.assertFalse(directories[0].exists(), 'temporary profile leaked')
        return result

    def test_result_categories_and_cleanup(self):
        self.assertEqual(self.invoke()['category'], 'completed')
        self.assertEqual(self.invoke('generation_failed', 4)['category'], 'refused')
        self.assertEqual(self.invoke('invalid_request', 2)['category'], 'refused')
        self.assertEqual(self.invoke('completed', -11)['category'], 'execution_error')
        self.assertEqual(self.invoke(malformed=True)['category'], 'execution_error')
        self.assertEqual(self.invoke(timeout=True)['category'], 'execution_error')

    def test_malformed_completed_reports_are_execution_errors(self):
        mutations = [lambda r: r['terrain'].clear(),
                     lambda r: r['resources']['types'].pop('wheat'),
                     lambda r: r['space'].update(build_sites_4x4='many'),
                     lambda r: r['fertility']['all_tiles'].update(mean=float('nan')),
                     lambda r: r['generation']['outcome'].update(success=False)]
        for mutation in mutations:
            self.assertEqual(self.invoke(change=mutation)['category'], 'execution_error')

    def test_refusal_rates_exclude_execution_errors(self):
        path = Path(__file__).resolve().parents[1] / '.agents/skills/glob2-map-design/scripts/refusal_sweep.py'
        spec = importlib.util.spec_from_file_location('refusal_sweep', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        errors = [{'category': 'execution_error'}] * 9
        self.assertIsNone(module.refusal_rate(errors))
        self.assertEqual(module.refusal_rate(errors + [{'category': 'refused'}]), 1)
        self.assertEqual(module.refusal_rate(errors + [{'category': 'completed'}]), 0)

    def test_subjects_and_extrema_survive(self):
        records = [dict(key='food', subject=0, value=0), dict(key='food', subject=1, value=100),
                   dict(key='variant', subject=None, value='wet')]
        metrics, raw = study.report_metrics({'generation': {'telemetry': {'records': records}}})
        self.assertEqual(metrics['tel:food'], 50)
        self.assertEqual(metrics['tel-min:food'], 0)
        self.assertEqual(metrics['tel-max:food'], 100)
        self.assertEqual(raw, records)

    def test_missing_measurement_is_not_a_zero(self):
        path = Path(__file__).resolve().parents[1] / '.agents/skills/glob2-map-design/scripts/control_study.py'
        spec = importlib.util.spec_from_file_location('control_study', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        rows = [dict(ok=True, study='baseline', control='', value=0, w=256, h=256,
                     m={'optional': 100, 'common': 2}),
                dict(ok=True, study='ablation', control='x', value=1, w=256, h=256,
                     m={'common': 2})]
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()) as output:
            Path(directory, 'ablation.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))
            module.report(SimpleNamespace(out=directory), {'x': [0, 1]}, {'x': 0})
        self.assertNotIn('optional', output.getvalue())

    def test_catalog_and_dimensions(self):
        catalog = {'generators': [{'method': 60, 'id': 'even-ground', 'controls': []}]}
        self.assertEqual(study.generator_definition(catalog, '60'), catalog['generators'][0])
        self.assertEqual(study.generator_definition(catalog, 'even-ground'), catalog['generators'][0])
        with self.assertRaises(ValueError): study.generator_definition(catalog, 'missing')
        for size in (0, 127, -128):
            with self.assertRaises(ValueError): study.dimension_exponent(size)
        self.assertEqual(study.dimension_exponent(512), 9)


class ResourceContractTests(unittest.TestCase):
    @staticmethod
    def description(command='map study'):
        return {'schema_version': 1, 'cli_version': 2,
                'execution_profiles': {'policy_version': 1, 'resource_plan_schema_version': 1,
                                       'profiles': list(study.PROFILES)},
                'commands': [{'path': command, 'available': True,
                              'options': [{'name': '--execution-profile'}, {'name': '--memory-budget-bytes'}]}]}

    @staticmethod
    def plan(profile='eco', memory=8192):
        return {'schema_version': 1, 'policy_version': 1,
                'requested_execution_profile': profile, 'selected_execution_profile': profile,
                'profile_source': 'explicit_profile', 'profile_tuning_pending': True,
                'resource_sources': {'planning_memory_budget_bytes': 'explicit_override'},
                'original_request': {'resource_overrides': {'planning_memory_budget_bytes': memory}},
                'resolved_resources': {'compute_participants': 1, 'compute_participants_include_owner': True,
                                       'planning_memory_budget_bytes': memory}}

    def test_omitted_request_does_not_probe_or_change_flags(self):
        with patch.object(study.subprocess, 'run') as run:
            self.assertIsNone(study.resource_request())
            self.assertEqual(study.native_resource_options('glob2', 'map study', None, 1), [])
            self.assertIsNone(study.validate_resource_report({}, None))
            run.assert_not_called()

    def test_all_explicit_intents_and_memory_only_keep_owner_policy(self):
        for profile in study.PROFILES:
            request = study.resource_request(profile, 8192)
            with patch.object(study.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 0, json.dumps(self.description()), '')):
                self.assertEqual(study.native_resource_options('glob2', 'map study', request, 1),
                                 ['--execution-profile', profile, '--memory-budget-bytes', '8192'])
            self.assertEqual(study.validate_resource_report({'execution_resources': self.plan(profile)}, request),
                             self.plan(profile))
        request = study.resource_request(memory_planning_budget_bytes=1)
        self.assertEqual(study.validate_resource_report({'execution_resources': self.plan('balanced', 1)}, request),
                         self.plan('balanced', 1))

    def test_invalid_request_rejected_before_any_probe(self):
        with patch.object(study.subprocess, 'run') as run:
            for memory in [0, -1, True, 1.5, 'not-bytes', 2**63]:
                with self.subTest(memory=memory), self.assertRaises(ValueError):
                    study.generate_map('glob2', 60, 12, 128, 512, 6,
                                       memory_planning_budget_bytes=memory)
            with self.assertRaises(ValueError):
                study.generate_map('glob2', 60, 12, 128, 512, 6, execution_profile='tiny')
            run.assert_not_called()

    def test_unsupported_binary_rejected_before_generation(self):
        for change in [lambda d: d.pop('execution_profiles'),
                       lambda d: d['commands'].clear(),
                       lambda d: d['commands'][0]['options'].clear()]:
            description = self.description(); change(description)
            with patch.object(study.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 0, json.dumps(description), '')) as run, self.assertRaises(ValueError):
                study.generate_map('glob2', 60, 12, 128, 512, 6, execution_profile='eco')
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0], ['glob2', 'help', '--format', 'json'])

    def test_mismatched_or_missing_native_resource_receipts_reject(self):
        request = study.resource_request('eco', 8192)
        for change in [lambda p: p.update(schema_version=True),
                       lambda p: p.update(profile_source='context_default'),
                       lambda p: p.update(selected_execution_profile='auto'),
                       lambda p: p['resolved_resources'].update(compute_participants=2),
                       lambda p: p['resolved_resources'].update(compute_participants=True),
                       lambda p: p['resolved_resources'].update(planning_memory_budget_bytes=8191),
                       lambda p: p['resource_sources'].update(planning_memory_budget_bytes='validated_default')]:
            plan = self.plan(); change(plan)
            with self.subTest(plan=plan), self.assertRaises(ValueError):
                study.validate_resource_report({'execution_resources': plan}, request)
        with self.assertRaises(ValueError): study.validate_resource_report({}, request)

    def test_explicit_generation_refusal_retains_requested_resolved_receipt(self):
        def native(command, **kwargs):
            if command[1] == 'help':
                return subprocess.CompletedProcess(command, 0, json.dumps(self.description()), '')
            self.assertIn('--execution-profile', command)
            self.assertEqual(command[command.index('--execution-profile') + 1], 'eco')
            self.assertEqual(command[command.index('--memory-budget-bytes') + 1], '8192')
            self.assertNotIn('--compute-threads', command)
            directory = Path(command[command.index('--output-dir') + 1])
            (directory/'result.json').write_text(json.dumps({'schema_version': 1, 'status': 'generation_failed',
                'diagnostic': 'layout refused', 'execution_resources': self.plan()}))
            return subprocess.CompletedProcess(command, 4, '', '')
        with patch.object(study.subprocess, 'run', side_effect=native) as run:
            result = study.generate_map('glob2', 60, 12, 128, 512, 6,
                                        execution_profile='eco', memory_planning_budget_bytes=8192)
        self.assertEqual(run.call_count, 2)
        self.assertEqual(result['category'], 'refused')
        self.assertEqual(result['execution_resources']['resolved'], self.plan())

    def test_unvalidated_native_plan_keeps_request_and_native_error(self):
        def native(command, **kwargs):
            if command[1] == 'help':
                return subprocess.CompletedProcess(command, 0, json.dumps(self.description()), '')
            directory = Path(command[command.index('--output-dir')+1])
            (directory/'result.json').write_text(json.dumps({'schema_version': 1,
                'status': 'invalid_request', 'diagnostic': 'preflight refusal'}))
            return subprocess.CompletedProcess(command, 2, '', '')
        with patch.object(study.subprocess, 'run', side_effect=native):
            result = study.generate_map('glob2', 60, 12, 128, 512, 6, execution_profile='eco')
        self.assertEqual(result['category'], 'execution_error')
        self.assertEqual(result['native']['status'], 'invalid_request')
        self.assertEqual(result['execution_resources']['requested']['execution_profile'], 'eco')
        self.assertNotIn('resolved', result['execution_resources'])


if __name__ == '__main__':
    unittest.main()
