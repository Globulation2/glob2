import math
import io
import json
import tempfile
from contextlib import ExitStack, redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

import benchmark_resource_refactor as benchmark
import unittest

from benchmark_resource_refactor import aggregate_interval


class ExecutionCleanupTest(unittest.TestCase):
    def test_interrupted_wait_kills_and_reaps_child_before_propagating(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'run'
            with patch.object(benchmark.benchmark_parallel_compute.subprocess, 'Popen') as popen, \
                    patch.object(benchmark.benchmark_parallel_compute.os, 'wait4', side_effect=KeyboardInterrupt()):
                with self.assertRaises(KeyboardInterrupt):
                    benchmark.benchmark_parallel_compute.execute(Path('/engine'), [], output)
                popen.return_value.kill.assert_called_once_with()
                popen.return_value.wait.assert_called_once_with()
                self.assertTrue(popen.call_args.kwargs['stdout'].closed)
            self.assertTrue((output / 'engine.log').exists())


class FrequencyEvidenceTest(unittest.TestCase):
    def test_denied_counter_probe_retains_error_without_claiming_effective_frequency(self):
        with patch.object(benchmark.platform, 'system', return_value='Linux'), \
                patch.object(benchmark.shutil, 'which', return_value='/usr/bin/perf'), \
                patch.object(benchmark.subprocess, 'run') as run:
            run.return_value.returncode = 255
            run.return_value.stderr = 'No permission to enable cycles event.'
            run.return_value.stdout = ''
            result = benchmark.frequency_capability()
        self.assertEqual(result['effective_frequency'], 'unavailable')
        self.assertEqual(result['counter_probe']['status'], 'unavailable')
        self.assertIn('No permission', result['counter_probe']['stderr'])

    def test_successful_probe_is_not_mislabeled_as_measured_frequency(self):
        with patch.object(benchmark.platform, 'system', return_value='Linux'), \
                patch.object(benchmark.shutil, 'which', return_value='/usr/bin/perf'), \
                patch.object(benchmark.subprocess, 'run') as run:
            run.return_value.returncode = 0
            run.return_value.stderr = '1000;;cycles;\n1.0;msec;task-clock;'
            run.return_value.stdout = ''
            result = benchmark.frequency_capability()
        self.assertEqual(result['effective_frequency'], 'unavailable')
        self.assertEqual(result['counter_probe']['status'], 'available')


class AggregateIntervalTest(unittest.TestCase):
    def test_independent_repeat_permutations_do_not_create_timing_blocks(self):
        # Opposite repeat ordering must not cancel independent measurement noise.
        first = [-0.12, -0.06, 0.0, 0.06, 0.12]
        second = [-value for value in first]
        original = aggregate_interval([first, second])
        permuted = aggregate_interval([second[2:] + second[:2], first[::-1]])
        self.assertEqual(original, permuted)
        self.assertAlmostEqual(original['ratio'], 1.0)
        self.assertLess(original['ci95'][0], 0.99)
        self.assertGreater(original['ci95'][1], 1.01)

    def test_constant_scenarios_keep_equal_weight_with_unequal_repeat_counts(self):
        result = aggregate_interval([[math.log(1.21)] * 3, [0.0] * 9])
        self.assertAlmostEqual(result['ratio'], 1.1)
        for bound in result['ci95']:
            self.assertAlmostEqual(bound, 1.1)

    def test_empty_strata_are_rejected(self):
        for values in ([], [[]], [[0.0], []]):
            with self.assertRaises(ValueError):
                aggregate_interval(values)



class CampaignIntegrityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.binaries = {}
        for name in ('baseline', 'candidate'):
            directory = self.root / name
            (directory / 'data').mkdir(parents=True)
            (directory / 'data' / 'registry.json').write_text('{}')
            self.binaries[name] = directory / 'engine'
            self.binaries[name].write_bytes(name.encode())
        self.fixture = self.root / 'checkpoint.game'
        self.fixture.write_bytes(b'frozen save')
        self.library = self.root / 'runtime.so'
        self.library.write_bytes(b'library')
        self.runner = self.root / 'runner.py'
        self.runner.write_text('# frozen runner')
        self.manifest = self.root / 'windows.json'
        self.manifest.write_text(json.dumps({'scenarios': [{
            'id': 'window', 'start_tick': 100,
            'args': ['--load-game', str(self.fixture), '--ticks', '200'],
            'fixture_sha256': {str(self.fixture): benchmark.digest(self.fixture)},
        }]}))
        self.output = self.root / 'output'
        self.calls = []

    def run_campaign(self, action=None, *, report_only=False, ratio=1.0, measured_ticks=100):
        def fake_execute(binary, args, output, *, cwd):
            output.mkdir(parents=True)
            self.calls.append(binary.parent.name)
            if action:
                action(len(self.calls))
            # A severe candidate-only process warmup must not enter the gate.
            measured_cpu = 100_000_000_000 if len(self.calls) <= 2 and binary.parent.name == 'candidate' else 1_000_000_000 * (ratio if binary.parent.name == 'candidate' else 1.0)
            return dict(cpu_s=4.0, wall_s=5.0, peak_rss_bytes=1024,
                        result={'ticks': 200, 'benchmark_measured_ticks': measured_ticks,
                                'benchmark_run_cpu_ns': measured_cpu,
                                'benchmark_setup_cpu_ns': 2_000_000_000,
                                'benchmark_save_cpu_ns': 0, 'run_ns': 1_100_000_000})

        argv = ['benchmark_resource_refactor.py', str(self.binaries['baseline']),
                str(self.binaries['candidate']), str(self.manifest),
                '--before-root', str(self.root / 'baseline'),
                '--after-root', str(self.root / 'candidate'),
                '--repeats', '4', '--output', str(self.output)]
        if report_only:
            argv.append('--report-only')
        with ExitStack() as stack:
            stack.enter_context(patch.object(benchmark.sys, 'argv', argv))
            stack.enter_context(patch.object(benchmark, 'execute', side_effect=fake_execute))
            stack.enter_context(patch.object(benchmark, 'RUNNER_INPUTS', (self.runner,)))
            stack.enter_context(patch.object(benchmark, 'frequency_capability', return_value={'effective_frequency': 'unavailable'}))
            stack.enter_context(patch.object(benchmark, 'frequency_snapshot', return_value={'scope': 'test boundary snapshot'}))
            stack.enter_context(patch.object(benchmark, 'runtime_libraries', side_effect=lambda binary: {
                'resolution': f'ASLR address changes {len(self.calls)}',
                'sha256': {str(self.library): benchmark.digest(self.library)}}))
            stack.enter_context(redirect_stdout(io.StringIO()))
            stack.enter_context(redirect_stderr(io.StringIO()))
            return benchmark.main()

    def report(self):
        return json.loads((self.output / 'input-verification.json').read_text())

    def test_unchanged_campaign_accepts_balanced_pairs_and_excludes_process_warmup(self):
        self.assertEqual(self.run_campaign(), 0)
        self.assertEqual(self.calls[:2], ['baseline', 'candidate'])
        measured = list(zip(self.calls[2::2], self.calls[3::2]))
        self.assertEqual(measured.count(('baseline', 'candidate')), 2)
        self.assertEqual(measured.count(('candidate', 'baseline')), 2)
        self.assertEqual(self.report()['state'], 'verified')
        self.assertTrue(self.report()['inputs_unchanged'])
        metadata = json.loads((self.output / 'metadata.json').read_text())
        self.assertEqual(metadata['warmup']['simulation_warmup_ticks'], 0)
        self.assertIn('clears GLOB2_PERF_DISABLE', metadata['instrumentation']['parent_disable_flag'])
        summary = json.loads((self.output / 'summary.json').read_text())
        self.assertEqual(summary['scenarios']['window']['simulation_cpu_s']['baseline_median'], 1.0)
        self.assertEqual(summary['scenarios']['window']['cpu_s']['baseline_median'], 4.0)
        self.assertEqual(summary['aggregate_cpu']['ratio'], 1.0)
        rows = [json.loads(line) for line in (self.output / 'measurements.jsonl').read_text().splitlines()]
        self.assertEqual(rows[1]['repeat'], -1)
        self.assertEqual(rows[1]['simulation_cpu_s'], 100.0)

    def test_report_only_preserves_threshold_diagnostics_and_default_exit_codes(self):
        for ratio, gate, default_exit in ((1.0, 'within_limits', 0),
                                           (1.03, 'regression', 1)):
            for report_only in (False, True):
                with self.subTest(ratio=ratio, report_only=report_only):
                    self.output = self.root / f'output-{ratio}-{report_only}'
                    self.calls = []
                    self.assertEqual(self.run_campaign(report_only=report_only, ratio=ratio),
                                     0 if report_only else default_exit)
                    summary = json.loads((self.output / 'summary.json').read_text())
                    self.assertEqual(summary['performance_gate'], gate)
                    self.assertEqual(summary['report_only'], report_only)
                    self.assertAlmostEqual(summary['aggregate_cpu']['ratio'], ratio)
        for report_only in (False, True):
            self.output = self.root / f'inconclusive-{report_only}'
            self.calls = []
            with patch.object(benchmark, 'aggregate_interval', return_value={
                    'ratio': 1.02, 'ci95': [1.01, 1.03]}):
                self.assertEqual(self.run_campaign(report_only=report_only),
                                 0 if report_only else 2)
            self.assertEqual(json.loads((self.output / 'summary.json').read_text())[
                'performance_gate'], 'inconclusive')

    def test_report_only_rejects_incomplete_windows(self):
        with self.assertRaisesRegex(RuntimeError, 'full measurement window'):
            self.run_campaign(report_only=True, measured_ticks=99)
        self.assertEqual(self.report()['state'], 'failed')
        self.assertFalse((self.output / 'summary.json').exists())

    def test_individual_threshold_remains_diagnostic_in_report_only(self):
        # A small aggregate can hide a single scenario above its historical limit.
        for report_only in (False, True):
            self.output = self.root / f'individual-{report_only}'
            self.calls = []
            with patch.object(benchmark, 'aggregate_interval', return_value={
                    'ratio': 1.0, 'ci95': [1.0, 1.0]}):
                self.assertEqual(self.run_campaign(report_only=report_only, ratio=1.06),
                                 0 if report_only else 1)
            self.assertEqual(json.loads((self.output / 'summary.json').read_text())[
                'performance_gate'], 'regression')

    def test_invalid_window_and_duplicate_ids_fail_before_execution(self):
        original = json.loads(self.manifest.read_text())
        for kind in ('zero', 'negative', 'duplicate', 'unsafe-id', 'missing-save', 'string-args'):
            with self.subTest(kind=kind):
                manifest = json.loads(json.dumps(original))
                scenario = manifest['scenarios'][0]
                if kind == 'zero':
                    scenario['start_tick'] = 200
                elif kind == 'negative':
                    scenario['start_tick'] = -1
                elif kind == 'duplicate':
                    manifest['scenarios'].append(dict(scenario))
                elif kind == 'unsafe-id':
                    scenario['id'] = '../escape'
                elif kind == 'missing-save':
                    scenario['args'] = ['--ticks', '200', '--load-game']
                else:
                    scenario['args'] = '--ticks 200 --load-game missing'
                self.manifest.write_text(json.dumps(manifest))
                with self.assertRaises(SystemExit):
                    self.run_campaign(report_only=True)
                self.assertEqual(self.calls, [])
                self.assertFalse(self.output.exists())

    def test_mutated_inputs_never_publish_acceptance(self):
        paths = {'binaries': self.binaries['candidate'], 'manifest': self.manifest,
                 'fixtures': self.fixture, 'data_roots': self.root / 'candidate/data/registry.json',
                 'runtime_libraries': self.library, 'runner_inputs': self.runner}
        # Separate fake campaigns retain real file hashing while replacing only engine execution.
        for category, target in paths.items():
            with self.subTest(category=category):
                original = target.read_bytes()
                self.output = self.root / ('output-' + category)
                self.calls = []
                def mutate(call):
                    if call == 1:
                        target.write_bytes(original + b'\nchanged')
                with self.assertRaisesRegex(RuntimeError, 'Benchmark inputs changed'):
                    self.run_campaign(mutate, report_only=True)
                report = self.report()
                self.assertEqual(report['state'], 'invalid')
                self.assertTrue(any(path.startswith('/' + category + '/') for path in report['changed_inputs']))
                self.assertFalse((self.output / 'summary.json').exists())
                target.write_bytes(original)

    def test_catalog_addition_and_missing_library_reject_acceptance(self):
        for kind in ('addition', 'deletion'):
            with self.subTest(kind=kind):
                self.output = self.root / kind
                self.calls = []
                def change(call):
                    if call == 1:
                        if kind == 'addition':
                            (self.root / 'candidate/data/new.json').write_text('{}')
                        else:
                            self.library.unlink()
                with self.assertRaisesRegex(RuntimeError, 'Benchmark inputs changed'):
                    self.run_campaign(change)
                self.assertEqual(self.report()['state'], 'invalid')
                self.assertFalse((self.output / 'summary.json').exists())

    def test_failure_and_keyboard_interrupt_preserve_partial_measurements_and_audit(self):
        for error in (RuntimeError('fake engine failure'), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                self.output = self.root / type(error).__name__
                self.calls = []
                def fail(call):
                    if call == 2:
                        raise error
                with self.assertRaises(type(error)):
                    self.run_campaign(fail, report_only=True)
                report = self.report()
                self.assertEqual(report['state'], 'failed')
                self.assertTrue(report['inputs_unchanged'])
                self.assertEqual(report['campaign_error']['type'], type(error).__name__)
                self.assertEqual(len((self.output / 'measurements.jsonl').read_text().splitlines()), 1)
                self.assertFalse((self.output / 'summary.json').exists())

    def test_mutation_during_engine_failure_retains_both_diagnostics(self):
        def fail(call):
            self.runner.write_text('# changed')
            raise RuntimeError('original engine failure')
        with self.assertRaisesRegex(RuntimeError, 'original engine failure'):
            self.run_campaign(fail, report_only=True)
        self.assertEqual(self.report()['state'], 'invalid')
        self.assertEqual(self.report()['campaign_error']['message'], 'original engine failure')
        self.assertTrue(self.report()['changed_inputs'])

if __name__ == '__main__':
    unittest.main()
