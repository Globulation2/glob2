#!/usr/bin/env python3
"""Cheap contracts for the opt-in benchmark runner; no compiler or timing gates."""
import io
import json
import subprocess
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from gradient_benchmark import (
    baseline_header,
    baseline_travel_header,
    cases,
    load_cases,
    real_registry,
    main,
    prepare_output,
    run_cases,
    kernel_execution_objective,
    parse_args,
)
from software_render_benchmark import execution_request, validate_execution_output

# Minimal excerpts with each syntax anchor used by the historical adapters.
# Tests deliberately mutate the hooks: losing instrumentation must fail before
# compilation rather than silently generating misleading benchmark evidence.
GRADIENT_SOURCE = '''
namespace gradient_kernel {
template<class TerrainAt>
void expandTerrainBucket(const TerrainEntryCosts &costs, TerrainAt terrainAt)
{
    std::array<unsigned, 2 * TERRAIN_COUNT> steps{};
    __m128i vectors[TERRAIN_COUNT], limits[TERRAIN_COUNT];
    uint16x8_t neonVectors[TERRAIN_COUNT], neonLimits[TERRAIN_COUNT];
    for (unsigned t = 0; t < TERRAIN_COUNT; ++t) {}
    for (unsigned t = 0; t < TERRAIN_COUNT; ++t) {}
    if (!count) return;
    target.reserveExtra(extra);
    if (gradient[i] != GRADIENT_AT_GOAL - cur) continue;
    pending += newSize-target.size;
}
template<class TerrainAt>
void propagateTerrainField(TerrainAt terrainAt) {}
}
'''
TRAVEL_SOURCE = '''
#include "ignored-header.h"
namespace field {
void expandTerrainTravel() {
    const auto [cost,index]=queue.top();queue.pop();
    if(cost!=costs[index]) continue;
    costs[next]=candidate;queue.emplace(candidate,next);
}
}
'''


class BaselineAdapterTest(unittest.TestCase):
    def adapt(self, function, text):
        source = Mock(spec=Path)
        source.read_text.return_value = text
        return function(source)

    def test_gradient_adapter_preserves_body_and_instruments_every_hook(self):
        adapted = self.adapt(baseline_header, GRADIENT_SOURCE)
        self.assertIn('template<class TerrainAt, std::size_t N>', adapted)
        self.assertIn('expandBaselineTerrainBucket', adapted)
        self.assertIn('const std::array<EntrySteps,N> &costs', adapted)
        self.assertNotIn('TERRAIN_COUNT', adapted)
        self.assertNotIn('propagateTerrainField', adapted)
        for counter in ('occupied', 'popped', 'chunkReserves', 'stale', 'relaxations'):
            self.assertEqual(adapted.count('GLOB2_GRADIENT_BENCH_EVENT(' + counter + ','), 1)

    def test_missing_or_duplicated_gradient_hooks_fail_closed(self):
        for hook in (
            'if (!count) return;',
            'target.reserveExtra(extra);',
            'if (gradient[i] != GRADIENT_AT_GOAL - cur) continue;',
            'pending += newSize-target.size;',
        ):
            for replacement in ('', hook + hook):
                with self.subTest(hook=hook, replacement=replacement):
                    with self.assertRaisesRegex(ValueError, 'Unsupported baseline'):
                        self.adapt(baseline_header, GRADIENT_SOURCE.replace(hook, replacement))

    def test_unknown_gradient_function_boundary_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported baseline kernel'):
            self.adapt(baseline_header, 'namespace gradient_kernel {}')

    def test_travel_adapter_preserves_body_and_rejects_counter_drift(self):
        adapted = self.adapt(baseline_travel_header, TRAVEL_SOURCE)
        self.assertTrue(adapted.startswith('namespace baseline_field'))
        self.assertNotIn('ignored-header', adapted)
        for counter in ('popped', 'stale', 'relaxations'):
            self.assertEqual(adapted.count('GLOB2_GRADIENT_BENCH_EVENT(' + counter + ','), 1)
        for hook in (
            'const auto [cost,index]=queue.top();queue.pop();',
            'if(cost!=costs[index]) continue;',
            'costs[next]=candidate;queue.emplace(candidate,next);',
        ):
            for replacement in ('', hook + hook):
                with self.subTest(hook=hook, replacement=replacement):
                    with self.assertRaisesRegex(ValueError, 'Unsupported baseline'):
                        self.adapt(baseline_travel_header, TRAVEL_SOURCE.replace(hook, replacement))


class BenchmarkProtocolTest(unittest.TestCase):
    def test_custom_cases_cannot_override_paired_protocol(self):
        for case in ({'layout': 'shared'}, {'repeats': 1}, {'no-oracle': True}):
            with self.subTest(case=case):
                with self.assertRaisesRegex(ValueError, 'Unsupported --case keys'):
                    load_cases([json.dumps(case)], 'smoke')

    def test_custom_cases_validate_types_before_compilation(self):
        for case in ([], 42, {'size': True}, {'size': 32.5}, {'mode': 1}):
            with self.subTest(case=case):
                with self.assertRaises(ValueError):
                    load_cases([json.dumps(case)], 'smoke')
        valid = {'size': 32, 'mode': 'terrain', 'cap': 63}
        self.assertEqual(load_cases([json.dumps(valid)], 'smoke'), [valid])

    def test_default_suites_retain_correctness_corners_and_all_profiles(self):
        for suite in ('smoke', 'representative', 'full'):
            matrix = list(cases(suite))
            self.assertEqual({case['swim'] for case in matrix}, set(range(7)))
            self.assertEqual({case['registry'] for case in matrix}, {real_registry(), 32, 64})
            self.assertTrue(any(case.get('width') == 1 for case in matrix))
            self.assertTrue(any(case.get('cap') == 0 for case in matrix))
            self.assertEqual({case['costs'] for case in matrix}, {'equivalent', 'distinct'})

    def test_existing_evidence_is_rejected_before_sources_are_modified(self):
        for name in ('manifest.json', 'samples.jsonl', 'summary.json'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                output = Path(directory)
                (output / name).write_text('existing evidence')
                args = SimpleNamespace(output=output, case=None, suite='smoke')
                diagnostic = io.StringIO()
                with patch('gradient_benchmark.parse_args', return_value=args), \
                        patch('gradient_benchmark.copy_sources') as copy, \
                        patch('sys.stderr', diagnostic):
                    self.assertEqual(main(), 1)
                copy.assert_not_called()
                self.assertEqual((output / name).read_text(), 'existing evidence')
                self.assertIn(name, diagnostic.getvalue())
                self.assertIn('fresh output directory', diagnostic.getvalue())
                self.assertNotIn('Traceback', diagnostic.getvalue())

    def test_fresh_output_can_be_created_or_precreated(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new' / 'results'
            prepare_output(output)
            self.assertTrue(output.is_dir())
            prepare_output(output)

    def test_failed_case_reports_command_and_stderr_and_preserves_partial_rows(self):
        partial = '{"candidate":false,"repetition":-1}\n'
        diagnostic = 'candidate differs from independent oracle'
        failure = subprocess.CalledProcessError(
            1, ['/unused/benchmark'], output=partial, stderr=diagnostic + '\n',
        )
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(output=Path(directory), repeats=2, cpu=None, backend='scalar')
            with patch('gradient_benchmark.subprocess.run', side_effect=failure):
                with self.assertRaises(RuntimeError) as raised:
                    run_cases(args, Path('/unused/benchmark'), [dict(size=32)])
            message = str(raised.exception)
            self.assertIn('exit 1', message)
            self.assertIn('/unused/benchmark --repeats 2 --layout shared --size 32', message)
            self.assertIn(diagnostic, message)
            self.assertEqual((args.output / 'samples.jsonl').read_text(), partial)
            self.assertFalse((args.output / 'summary.json').exists())

    def test_summary_excludes_cold_samples_and_preserves_both_layouts(self):
        rows = [
            dict(candidate=False, repetition=-1, cpu_ms=1000),
            dict(candidate=True, repetition=-1, cpu_ms=2000),
            dict(candidate=False, repetition=0, cpu_ms=10),
            dict(candidate=True, repetition=0, cpu_ms=5),
            dict(candidate=False, repetition=1, cpu_ms=30),
            dict(candidate=True, repetition=1, cpu_ms=15),
        ]
        raw = ''.join(json.dumps(row) + '\n' for row in rows)
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(output=Path(directory), repeats=2, cpu=None, backend='sse2')
            with patch('gradient_benchmark.subprocess.run', return_value=Mock(stdout=raw)) as run:
                run_cases(args, Path('/unused/benchmark'), [dict(size=32)])
            summary = json.loads((args.output / 'summary.json').read_text())
            self.assertEqual([row['layout'] for row in summary], ['shared', 'separate'])
            for row in summary:
                self.assertEqual(row['baseline_cpu_ms'], 20)
                self.assertEqual(row['candidate_cpu_ms'], 10)
                self.assertEqual(row['ratio'], 0.5)
            self.assertEqual((args.output / 'samples.jsonl').read_text(), raw + raw)
            for call, layout in zip(run.call_args_list, ('shared', 'separate')):
                self.assertEqual(call.kwargs['env']['GLOB2_GRADIENT_BACKEND'], 'sse2')
                self.assertEqual(call.kwargs['env']['GLOB2_GRADIENT_VERIFY'], 'off')
                self.assertEqual(call.kwargs['env']['GLOB2_GRADIENT_ALGORITHM'], 'buckets')
                self.assertEqual(call.args[0], [
                    '/unused/benchmark', '--repeats', '2', '--layout', layout, '--size', '32',
                ])


class BenchmarkResourceContracts(unittest.TestCase):
    def test_kernel_pin_respects_injected_caller_allocation(self):
        argv = ['gradient_benchmark.py', '--baseline-dir', '/baseline', '--output', '/output', '--cpu', '7']
        with patch('sys.argv', argv), patch('gradient_benchmark.os.sched_getaffinity', return_value={7, 8}, create=True):
            self.assertEqual(parse_args().cpu, 7)
        with patch('sys.argv', argv), patch('gradient_benchmark.os.sched_getaffinity', return_value={8}, create=True), \
                patch('sys.stderr', io.StringIO()), self.assertRaises(SystemExit):
            parse_args()

    def test_standalone_kernel_objective_never_claims_engine_policy(self):
        args = SimpleNamespace(cpu=7, backend='scalar', repeats=11, execution_profile='eco')
        receipt = kernel_execution_objective(args)
        self.assertEqual(receipt['requested_execution_profile'], 'eco')
        self.assertFalse(receipt['execution_policy_applied'])
        self.assertEqual(receipt['cpu_ids'], [7])
        self.assertEqual(receipt['compute_participants'], 1)
        self.assertEqual(receipt['backend'], 'scalar')
        args.cpu = None
        self.assertEqual(kernel_execution_objective(args)['cpu_enforcement'], 'not_enforced')

    def test_renderer_allocation_argv_and_explicit_legacy(self):
        modes = {'baseline': 'legacy_argv', 'candidate': 'require_profile'}
        receipt = execution_request([3, 4, 19, 20], 'fast-simulation', modes, 2, 1024)
        self.assertEqual(receipt['flags']['baseline'], ['--compute-threads', '2'])
        self.assertEqual(receipt['flags']['candidate'], ['--execution-profile', 'fast-simulation',
                         '--compute-threads', '2', '--memory-budget-bytes', '1024'])
        self.assertEqual(receipt['participants'], 2)
        self.assertEqual(validate_execution_output('compute_threads=2\n', receipt, 'baseline')['compute_threads'], 2)
        with self.assertRaisesRegex(ValueError, 'legacy renderer actual participants'):
            validate_execution_output('compute_threads=4\n', receipt, 'baseline')
        for mask, count in [(None, None), ([3], 2), ([3], True), ([3], 0)]:
            with self.subTest(mask=mask, count=count), self.assertRaises(ValueError):
                execution_request(mask, 'fast-simulation', modes, count)
        legacy = execution_request(None, 'fast-simulation', {'baseline': 'legacy_argv', 'candidate': 'legacy_argv'})
        self.assertEqual(validate_execution_output('old timing only', legacy, 'baseline')['scope'],
                         'explicit_legacy_resolved_plan_unknown')

    def test_renderer_markers_require_actual_frozen_pool_and_plan(self):
        receipt = execution_request([3, 19], 'auto', {'candidate': 'require_profile'}, 1, 1024)
        plan = {'schema_version': 1, 'policy_version': 1, 'requested_execution_profile': 'auto',
                'selected_execution_profile': 'auto', 'profile_source': 'explicit_profile',
                'profile_tuning_pending': True,
                'resolved_resources': {'compute_participants': 1, 'planning_memory_budget_bytes': 1024},
                'device_capabilities': {'allocated_cpu_participants': 2}}
        stdout = 'execution_resources=' + json.dumps(plan) + '\ncompute_threads=1\ncompute_resolved_threads=1\n'
        self.assertEqual(validate_execution_output(stdout, receipt, 'candidate')['compute_threads'], 1)
        for bad in (stdout.replace('compute_threads=1', 'compute_threads=2'),
                    stdout.replace('compute_resolved_threads=1', 'compute_resolved_threads=2'),
                    stdout + 'compute_threads=1\n',
                    stdout.replace('explicit_profile', 'validated_default'),
                    stdout.replace('execution_resources=', 'unrecognized=')):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate_execution_output(bad, receipt, 'candidate')


if __name__ == '__main__':
    unittest.main()
