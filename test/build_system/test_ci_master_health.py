"""Contracts for the fast gate's master-red rule."""
import datetime as dt
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '.github/scripts'))
import ci_master_health as health  # noqa: E402

NOW = dt.datetime(2026, 11, 7, 12, tzinfo=dt.timezone.utc)


def run(conclusion, hours_ago):
    created = (NOW - dt.timedelta(hours=hours_ago)).isoformat().replace('+00:00', 'Z')
    return {'conclusion': conclusion, 'created_at': created, 'html_url': 'u', 'event': 'push'}


class MasterHealthTest(unittest.TestCase):
    def test_green_and_recently_red_pass(self):
        self.assertTrue(health.verdict([run('success', 1), run('failure', 30)], [], NOW)[0])
        self.assertTrue(health.verdict([run('failure', 2), run('failure', 10), run('success', 20)], [], NOW)[0])

    def test_red_for_a_day_needs_the_repair_label(self):
        runs = [run('cancelled', 1), run('failure', 5), run('failure', 30), run('success', 40)]
        self.assertFalse(health.verdict(runs, [], NOW)[0])
        self.assertTrue(health.verdict(runs, ['ci:repair'], NOW)[0])

    def test_cancelled_runs_decide_nothing(self):
        self.assertTrue(health.verdict([run('cancelled', 1), run('success', 50)], [], NOW)[0])
        self.assertTrue(health.verdict([], [], NOW)[0])


if __name__ == '__main__':
    unittest.main()
