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
    main,
    prepare_output,
    run_cases,
)

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
            self.assertEqual({case['registry'] for case in matrix}, {7, 8, 32, 64})
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
            args = SimpleNamespace(output=Path(directory), repeats=2, cpu=None)
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
            args = SimpleNamespace(output=Path(directory), repeats=2, cpu=None)
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
                self.assertEqual(call.args[0], [
                    '/unused/benchmark', '--repeats', '2', '--layout', layout, '--size', '32',
                ])


if __name__ == '__main__':
    unittest.main()
