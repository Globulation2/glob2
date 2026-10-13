#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Dependency-free collector tests: python3 test/test_map_telemetry.py."""
import json
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import map_telemetry as telemetry

FAKE = '''#!/usr/bin/env python3
import json, os, pathlib, sys, time
args = sys.argv
assert pathlib.Path.cwd() == pathlib.Path(os.environ['GLOB2_USER_DIR']).resolve()
assert args[args.index('--data-dir') + 1] == ROOT
seed = int(args[args.index('--seed') + 1])
output = pathlib.Path(args[args.index('--report-file') + 1])
print('retained stdout', flush=True)
print('retained stderr', file=sys.stderr, flush=True)
if seed == 3: time.sleep(5)
if seed == 4:
    output.write_text('{broken')
    sys.exit(0)
if seed == 5: sys.exit(7)
records = [] if seed == 2 else [
    {'key':'x.fallback','kind':'fallback','subject':0,'value':'one'},
    {'key':'x.fallback','kind':'fallback','subject':1,'value':'two'},
    {'key':'x.length','kind':'measurement','subject':0,'value':8},
    {'key':'x.toggle','kind':'measurement','subject':None,'value':True}]
output.write_text(json.dumps({'schema_version':2,
    'report_type':'generation_failure' if seed == 2 else 'map',
    'generation':{'generator':args[3], 'seed':seed, 'revision':4,
    'parameters':{'width':128}, 'outcome':{'success':seed != 2},
    'telemetry':{'schema_version':1,'enabled':True,'dropped_records':0,
    'invalid_values':0,'records':records}}}))
sys.exit(1 if seed == 2 else 0)
'''


class TelemetryTests(unittest.TestCase):
    def test_collect_failures_and_summary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binary = root / 'fake'
            binary.write_text(FAKE.replace('ROOT', repr(str(telemetry.ROOT))))
            binary.chmod(0o755)
            out = root / 'results'
            args = ['collect', '--generators', 'canals', '--count', '5', '--jobs', '2',
                    '--timeout', '0.5', '--set', 'width=128', '--binary', str(binary),
                    '--out', str(out)]
            real_run = telemetry.subprocess.run
            def run_python(command, **kwargs):
                return real_run([sys.executable, *command], **kwargs)
            with patch.object(telemetry.subprocess, 'run', side_effect=run_python):
                self.assertEqual(telemetry.main(args), 1)
            manifests = [json.loads(p.read_text()) for p in sorted(out.glob('attempt-*/manifest.json'))]
            self.assertEqual([m['status'] for m in manifests],
                             ['ok', 'process_failure', 'timeout', 'report_failure', 'process_failure'])
            self.assertEqual(manifests[1]['report_type'], 'generation_failure')
            self.assertEqual(manifests[4]['returncode'], 7)
            self.assertIn('retained stdout', (out / 'attempt-000002/stdout.log').read_text())
            self.assertIn('retained stderr', (out / 'attempt-000002/stderr.log').read_text())
            self.assertEqual((out / 'attempt-000003/report.json').read_text(), '{broken')
            self.assertFalse((out / 'attempt-000004/report.json').exists())
            groups = telemetry.summarize(out)['groups']
            known = next(g for g in groups if g['revision'] == 4)
            self.assertEqual(known['attempts'], 2)
            self.assertEqual(known['fallbacks']['x.fallback']['observed_rate'], 0.5)
            self.assertEqual(known['metrics']['x.length']['mean'], 8)
            self.assertEqual(known['metrics']['x.length']['records'], 1)
            self.assertNotIn('x.toggle', known['metrics'])
            rows = [json.loads(line) for line in (out / 'records.jsonl').read_text().splitlines()]
            self.assertEqual([r['sequence'] for r in rows], [0, 1, 2, 3])
            self.assertEqual([r['subject'] for r in rows], [0, 1, 0, None])
            self.assertEqual(rows[0]['seed'], 1)
            with self.assertRaises(FileExistsError):
                telemetry.main(args)

    def test_launch_failure_and_group_separation(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            attempt = telemetry.attempt(out / 'missing', 'canals', 1, {}, out / 'attempt-000', 1)
            self.assertEqual(attempt['status'], 'launch_failure')
            for index, (revision, width) in enumerate([(1, 128), (2, 128), (2, 256)], 1):
                directory = out / f'attempt-{index:03}'
                directory.mkdir()
                telemetry.write_json(directory / 'manifest.json', dict(attempt, status='ok'))
                telemetry.write_json(directory / 'report.json', {'schema_version': 2,
                    'report_type': 'map', 'generation': {'generator':'canals', 'seed':1,
                    'revision':revision, 'parameters':{'width':width}, 'telemetry':{
                    'schema_version':1, 'enabled':True, 'dropped_records':2, 'records':[]}}})
            groups = telemetry.summarize(out)['groups']
            self.assertEqual(len(groups), 4)
            self.assertEqual(sum(g['incomplete_telemetry_attempts'] for g in groups), 3)


class TelemetryResourceTests(unittest.TestCase):
    @staticmethod
    def description():
        return {'schema_version': 1, 'cli_version': 2,
                'execution_profiles': {'policy_version': 1, 'resource_plan_schema_version': 1,
                                       'profiles': list(telemetry.PROFILES)},
                'commands': [{'path': 'map generate', 'available': True,
                              'options': [{'name': '--execution-profile'}, {'name': '--memory-budget-bytes'}]}]}

    @staticmethod
    def plan():
        return {'schema_version': 1, 'policy_version': 1,
                'requested_execution_profile': 'eco', 'selected_execution_profile': 'eco',
                'profile_source': 'explicit_profile', 'profile_tuning_pending': True,
                'resource_sources': {'planning_memory_budget_bytes': 'explicit_override'},
                'original_request': {'resource_overrides': {'planning_memory_budget_bytes': 8192}},
                'resolved_resources': {'compute_participants': 1, 'compute_participants_include_owner': True,
                                       'planning_memory_budget_bytes': 8192}}

    def native(self, command, **kwargs):
        if command[1] == 'help':
            return subprocess.CompletedProcess(command, 0, json.dumps(self.description()), '')
        self.assertEqual(command[command.index('--execution-profile')+1], 'eco')
        self.assertEqual(command[command.index('--memory-budget-bytes')+1], '8192')
        self.assertNotIn('--compute-threads', command)
        seed = int(command[command.index('--seed')+1])
        report = {'schema_version': 2, 'report_type': 'map', 'execution_resources': self.plan(),
                  'generation': {'generator': command[3], 'seed': seed,
                                 'outcome': {'success': True}}}
        Path(command[command.index('--report-file')+1]).write_text(json.dumps(report))
        return subprocess.CompletedProcess(command, 0)

    def test_collect_probes_once_and_retains_each_requested_resolved_plan(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)/'collection'
            with patch.object(telemetry.subprocess, 'run', side_effect=self.native) as run:
                self.assertEqual(telemetry.main(['collect', '--generators', 'canals', '--count', '2',
                    '--jobs', '1', '--out', str(out), '--execution-profile', 'eco',
                    '--memory-planning-budget-bytes', '8192']), 0)
            self.assertEqual(run.call_count, 3)  # One discovery plus two generation requests.
            self.assertEqual(json.loads((out/'run.json').read_text())['execution_resource_request'],
                             {'execution_profile': 'eco', 'memory_planning_budget_bytes': 8192})
            for p in out.glob('attempt-*/manifest.json'):
                record = json.loads(p.read_text())
                self.assertEqual(record['status'], 'ok')
                self.assertEqual(record['execution_resources']['resolved'], self.plan())

    def test_unsupported_explicit_contract_never_creates_or_launches_attempt(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)/'attempt'
            with patch.object(telemetry.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 0, json.dumps({'schema_version': 1, 'cli_version': 2, 'commands': []}), '')) as run:
                with self.assertRaises(ValueError):
                    telemetry.attempt('glob2', 'canals', 1, {}, directory, 1, execution_profile='eco')
            self.assertEqual(run.call_count, 1)
            self.assertFalse(directory.exists())

    def test_bad_native_receipt_is_report_failure_not_success(self):
        with tempfile.TemporaryDirectory() as temp:
            with patch.object(telemetry.subprocess, 'run', side_effect=self.native), \
                 patch.object(self, 'plan', return_value={}):
                record = telemetry.attempt('glob2', 'canals', 1, {}, Path(temp)/'attempt', 1,
                                           execution_profile='eco', memory_planning_budget_bytes=8192)
            self.assertEqual(record['status'], 'report_failure')
            self.assertIn('resource plan', record['report_error'])
            self.assertNotIn('resolved', record['execution_resources'])


if __name__ == '__main__':
    unittest.main()
