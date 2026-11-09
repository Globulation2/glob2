#!/usr/bin/env python3
"""Contracts for tools/sim_diff.py and the sidecar parser in tools/sim_fingerprint.py."""
import gzip
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sim_diff  # noqa: E402
import sim_fingerprint  # noqa: E402


def bundle(games=None, match=None, maps=None, ticks=600):
    scenarios = {}
    for name, rows in (games or {}).items():
        scenarios[name] = {'kind': 'game', 'ticks': rows}
    if match is not None:
        scenarios['match/FourSquares1'] = {'kind': 'match', 'ticks': match}
    if maps is not None:
        scenarios['generators'] = {'kind': 'generators', 'maps': maps}
    return {'schema': sim_fingerprint.SCHEMA, 'ticks': ticks, 'scenarios': scenarios}


def game(team1=7, units='u', aggregate=100):
    return [[0, 1, [[5, 'u', 'b'], [6, 'u', 'b']]],
            [1, aggregate, [[5, 'u', 'b'], [team1, units, 'b']]]]


class CompareTest(unittest.TestCase):
    def test_identical_bundles_have_no_differences(self):
        b = bundle({'game/a': game()}, [[1, 2]], {'g 8x8': 'ok 1'})
        self.assertEqual(sim_diff.compare(b, json.loads(json.dumps(b))), [])

    def test_first_difference_names_scenario_tick_and_component(self):
        base = bundle({'game/a': game()})
        self.assertEqual(sim_diff.compare(base, bundle({'game/a': game(team1=8)})),
                         [{'scenario': 'game/a', 'tick': 1, 'component': 'team 1 team checksum'}])
        self.assertEqual(sim_diff.compare(base, bundle({'game/a': game(units='v')}))[0]['component'],
                         'team 1 units')
        self.assertIn('aggregate only', sim_diff.compare(base, bundle({'game/a': game(aggregate=101)}))[0]['component'])

    def test_match_and_generator_differences(self):
        self.assertEqual(sim_diff.compare(bundle(match=[[1, 2], [2, 3]]), bundle(match=[[1, 2], [2, 4]])),
                         [{'scenario': 'match/FourSquares1', 'tick': 2, 'component': 'state checksum'}])
        found = sim_diff.compare(bundle(maps={'a': 'ok 1', 'b': 'ok 2'}), bundle(maps={'a': 'ok 1', 'b': 'ok 3'}))
        self.assertIn('1 generator maps, first: b', found[0]['component'])

    def test_a_new_generator_revision_is_noted_not_a_difference(self):
        base = bundle(maps={'forts@3 2x2 teams=2 seed=1': 'ok 1', 'river@1 2x2 teams=2 seed=1': 'ok 5'})
        cand = bundle(maps={'forts@4 2x2 teams=2 seed=1': 'ok 9', 'river@1 2x2 teams=2 seed=1': 'ok 5'})
        self.assertEqual(sim_diff.compare(base, cand), [])
        [note] = sim_diff.unmatched_maps(base, cand)
        self.assertIn('1 rows only in the base and 1 only in the candidate (forts@3, forts@4)', note)
        cand['scenarios']['generators']['maps']['river@1 2x2 teams=2 seed=1'] = 'ok 6'
        [found] = sim_diff.compare(base, cand)
        self.assertIn('first: river@1', found['component'])

    def test_golden_traces_report_the_first_differing_line(self):
        trace = lambda lines: {'schema': sim_fingerprint.SCHEMA, 'ticks': 600,
                               'scenarios': {'golden/x.txt': {'kind': 'trace', 'lines': lines}}}
        self.assertEqual(sim_diff.compare(trace(['1 a', '2 b']), trace(['1 a', '2 b'])), [])
        [found] = sim_diff.compare(trace(['1 a', '2 b']), trace(['1 a', '2 c']))
        self.assertEqual((found['scenario'], found['component']), ('golden/x.txt', "line 2: '2 b' -> '2 c'"))
        self.assertIn('line count 2 vs 3', sim_diff.compare(trace(['1', '2']), trace(['1', '2', '3']))[0]['component'])

    def test_scenarios_only_one_build_has_are_noted_not_compared(self):
        base, cand = bundle({'game/a': game(), 'game/b': game()}), bundle({'game/a': game()})
        self.assertEqual(sim_diff.compare(base, cand), [])
        self.assertEqual(sim_diff.unmatched(base, cand), ['game/b'])

    def test_mismatched_runs(self):
        with self.assertRaisesRegex(ValueError, 'tick counts'):
            sim_diff.compare(bundle(ticks=600), bundle(ticks=300))

    def test_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for name, content in (('a', bundle(match=[[1, 2]])), ('b', bundle(match=[[1, 3]]))):
                path = Path(directory) / f'{name}.json.gz'
                with gzip.open(path, 'wt') as stream:
                    json.dump(content, stream)
                paths.append(str(path))
            with mock.patch('builtins.print'):
                self.assertEqual(sim_diff.main([paths[0], paths[0]]), 0)
                self.assertEqual(sim_diff.main(paths), 1)
                self.assertEqual(sim_diff.main([paths[0], str(Path(directory) / 'missing.json.gz')]), 2)


class SidecarTest(unittest.TestCase):
    def test_parses_teams_units_and_buildings(self):
        entity = struct.pack('<HHHI', 1, 0, 0, 1) + struct.pack('<I', 42)  # 10-byte header + one field
        tick = struct.pack('<II', 3, 99) + struct.pack('<I', 7) + struct.pack('<I', 1) + entity + struct.pack('<I', 0)
        data = b'GCS1' + struct.pack('<4I', 1, 1, 1, 0) + tick
        [[number, total, teams]] = sim_fingerprint.parse_sidecar(data)
        self.assertEqual((number, total, teams[0][0]), (3, 99, 7))
        with self.assertRaisesRegex(ValueError, 'truncated'):
            sim_fingerprint.parse_sidecar(data + b'x')


if __name__ == '__main__':
    unittest.main()
