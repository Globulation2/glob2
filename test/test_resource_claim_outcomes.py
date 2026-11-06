#!/usr/bin/env python3
"""Decision outcome footer counters are validated and exposed per game."""
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.claim_outcomes import COUNTERS
from tools.tournaments.resource_claims import trace_rows, read_game
from tools.tournaments.transport import worker_archive
from claim_trace_fixture import outcome_footer, stream, trace


def fixture(counts=None):
    rows = trace()
    rows[-1]['decision_outcomes'] = outcome_footer(counts if counts is not None else
        {(1, 0): {'eligible': 2, 'attempted': 2, 'acquired': 1, 'no_candidate': 1, 'route_pending': 1, 'fallback_wait': 1}})
    return rows


class OutcomeTests(unittest.TestCase):
    def test_counters_are_exposed_per_game(self):
        game = read_game(stream(fixture()))
        table = game['decision_outcomes']['material_phase_counters'][1][0]
        self.assertEqual(table[COUNTERS.index('acquired')], 1)
        self.assertEqual(table[COUNTERS.index('fallback_wait')], 1)

    def test_cooldown_wait_is_independent_of_attempted(self):
        rows = fixture({(1, 0): {'eligible': 1, 'attempted': 1, 'no_candidate': 1, 'cooldown_wait': 1}})
        self.assertEqual(len(list(trace_rows(stream(rows)))), 3)

    def test_inconsistent_counters_rejected(self):
        for counts in ({'eligible': 1, 'attempted': 1, 'not_attempted': 1},
                       {'eligible': 1, 'not_attempted': 1, 'cooldown_not_attempted': 1, 'missing_field_not_attempted': 1},
                       {'eligible': 1, 'attempted': 1, 'no_candidate': 2},
                       {'eligible': 1, 'attempted': 1, 'route_ready': 2}):
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                list(trace_rows(stream(fixture({(1, 1): counts}))))

    def test_unadvertised_old_and_bad_footer_rejected(self):
        rows = fixture(); del rows[0]['decision_outcome_version']
        with self.assertRaises(ValueError): list(trace_rows(stream(rows)))
        rows = fixture(); rows[0]['decision_outcome_version'] = 1
        with self.assertRaises(ValueError): list(trace_rows(stream(rows)))
        for change in ({'version': 1}, {'counter_names': list(COUNTERS)[:-1]}, {'phase_names': ['movement']},
                       {'material_phase_counters': []}):
            rows = fixture(); rows[-1]['decision_outcomes'].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError): list(trace_rows(stream(rows)))
        rows = fixture(); rows.insert(1, {'kind': 'decision_outcome', 'schema_version': 1})
        with self.assertRaises(ValueError): list(trace_rows(stream(rows)))

    def test_isolated_worker_archive_includes_validator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); archive = root/'worker.pyz'; worker_archive(archive)
            path = root/'trace.jsonl'; path.write_text(stream(fixture()).getvalue())
            script = "import sys;sys.path.insert(0,sys.argv[1]);from tournaments.resource_claims import trace_rows;assert len(list(trace_rows(open(sys.argv[2]))))==3"
            subprocess.run([sys.executable, '-I', '-S', '-c', script, str(archive), str(path)], cwd=root, check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
