"""Contracts for the fast gate's master-red rule."""
import datetime as dt
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '.github/scripts'))
import ci_master_health as health  # noqa: E402

NOW = dt.datetime(2026, 11, 7, 12, tzinfo=dt.timezone.utc)


def run(state, hours_ago):
    created = (NOW - dt.timedelta(hours=hours_ago)).isoformat().replace('+00:00', 'Z')
    return {'state': state, 'created_at': created, 'html_url': 'u', 'event': 'push'}


def job(name, conclusion):
    return {'name': name, 'conclusion': conclusion}


class RunStateTest(unittest.TestCase):
    def test_only_gating_jobs_decide(self):
        self.assertEqual(health.run_state([job('windows (mingw-w64)', 'success'),
                                           job('Native Clang coverage', 'failure'),
                                           job('tsan', 'failure')]), 'success')
        self.assertEqual(health.run_state([job('browser tests (chromium 1/5)', 'failure'),
                                           job('windows (mingw-w64)', 'success')]), 'failure')
        self.assertEqual(health.run_state([job('Linux GCC 13 build and tests / Linux tests (ubuntu-24.04, shard 1/4)',
                                               'timed_out')]), 'failure')

    def test_cancelled_or_missing_gates_are_undecided(self):
        self.assertEqual(health.run_state([job('windows (mingw-w64)', 'cancelled'),
                                           job('macos / native', 'success')]), 'undecided')
        self.assertEqual(health.run_state([job('Native Clang coverage', 'success')]), 'undecided')
        self.assertEqual(health.run_state([]), 'undecided')


class MasterHealthTest(unittest.TestCase):
    def test_green_and_recently_red_pass(self):
        self.assertTrue(health.verdict([run('success', 1), run('failure', 30)], [], NOW)[0])
        self.assertTrue(health.verdict([run('failure', 2), run('failure', 10), run('success', 20)], [], NOW)[0])

    def test_red_for_a_day_needs_the_repair_label(self):
        runs = [run('undecided', 1), run('failure', 5), run('failure', 30), run('success', 40)]
        self.assertFalse(health.verdict(runs, [], NOW)[0])
        self.assertTrue(health.verdict(runs, ['ci:repair'], NOW)[0])

    def test_undecided_runs_decide_nothing(self):
        self.assertTrue(health.verdict([run('undecided', 1), run('success', 50)], [], NOW)[0])
        self.assertTrue(health.verdict([], [], NOW)[0])

    def test_rule_is_advisory_until_master_stays_green(self):
        # Re-enable in its own pull request after about 14 green days.
        self.assertFalse(health.ENFORCED)


if __name__ == '__main__':
    unittest.main()
