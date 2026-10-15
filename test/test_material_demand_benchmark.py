import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location('material_demand_benchmark', Path(__file__).parents[1] / 'tools/material_demand_benchmark.py')
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def fixture_output(*, measure=False):
    return '\n'.join(json.dumps({'fixture': 'building-material-demand', 'case': case,
                                'operation': op, 'repeat': repeat, 'iterations': 30,
                                'digest': 713, 'elapsed_ns': 123 if measure else None}, separators=(',', ':'))
                     for case in sorted(bench.CASES) for op in sorted(bench.OPERATIONS)
                     for repeat in range(7 if measure else 1))


class MaterialDemandBenchmarkTests(unittest.TestCase):
    def test_check_mode_requires_complete_untimed_inventory(self):
        output = fixture_output()
        self.assertEqual(len(bench.fixture_records(output, measure=False)), 117)
        with self.assertRaises(ValueError):
            bench.fixture_records(output + '\n' + output.splitlines()[0], measure=False)
        with self.assertRaises(ValueError):
            bench.fixture_records('\n'.join(output.splitlines()[1:]), measure=False)
        with self.assertRaises(ValueError):
            bench.fixture_records(output.replace('"elapsed_ns":null', '"elapsed_ns":123'), measure=False)
        with self.assertRaises(ValueError):
            bench.fixture_records(output.replace('"iterations":30', '"iterations":0'), measure=False)

    def test_measure_mode_requires_seven_repetitions(self):
        self.assertEqual(len(bench.fixture_records(fixture_output(measure=True), measure=True)), 819)
        with self.assertRaises(ValueError):
            bench.fixture_records(fixture_output(), measure=True)

    def test_summary_samples_are_optional_for_baseline_binaries_but_never_partial(self):
        rows = [json.loads(line) for line in fixture_output().splitlines()]
        extra = [dict(row, operation='summary') for row in rows if row['operation'] == 'staffing']
        complete = '\n'.join(json.dumps(row, separators=(',', ':')) for row in rows + extra)
        self.assertEqual(len(bench.fixture_records(complete, measure=False)), 130)
        with self.assertRaises(ValueError):
            bench.fixture_records(complete.rsplit('\n', 1)[0], measure=False)

    def test_reference_digest_divergence_is_not_a_successful_benchmark(self):
        output = fixture_output().replace('"operation":"single_current","repeat":0,"iterations":30,"digest":713',
                                          '"operation":"single_current","repeat":0,"iterations":30,"digest":714', 1)
        with self.assertRaises(ValueError):
            bench.fixture_records(output, measure=False)

    def test_binary_sidecars_and_save_continuation_tail(self):
        with tempfile.TemporaryDirectory() as scratch:
            a, b = Path(scratch) / 'a', Path(scratch) / 'b'
            a.mkdir(); b.mkdir()
            for p in (a, b):
                (p / 'world.checksums').write_text('10 aa\n11 bb\n')
                (p / 'game.replay.checksums').write_bytes(b'\x80\xff\x00')
            bench.compare_traces(a, b)
            (b / 'world.checksums').write_text('11 bb\n')
            bench.compare_traces(a, b, tail_from=11)
            with self.assertRaises(ValueError):
                bench.compare_traces(a, b)
            (b / 'world.checksums').write_text('11 cc\n')
            with self.assertRaises(ValueError):
                bench.compare_traces(a, b, tail_from=11)


    def test_historical_headers_are_canonicalized_in_both_split_branches(self):
        args = SimpleNamespace(game_binary=Path('game'), save=Path('old.game'),
                               measure=False, ticks=25153, baseline_game=None)
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(bench, 'game_run', return_value={'ticks': 25153, 'finalChecksum': 7}) as run, \
                 patch.object(bench, 'compare_traces'), patch.object(bench, 'sha', return_value='hash'):
                proof = bench.simulation_fixture(args, Path(directory))
        self.assertTrue(proof['save_load_equal'])
        self.assertEqual(run.call_count, 3)
        for call in run.call_args_list:
            self.assertIn('--write-replay', call.args[1])


if __name__ == '__main__':
    unittest.main()
