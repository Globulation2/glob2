"""Directly selected checks remain distinct from executable prerequisite closure."""

import re
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import ci_check_inventory as checks
import ci_changed_paths as selector


class InventoryTest(unittest.TestCase):
    def test_every_workflow_job_has_an_explicit_inventory_flag(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text()
        jobs = set(re.findall(r"^  ([a-z][a-z0-9-]*):$", workflow.split("\njobs:\n", 1)[1], re.MULTILINE))
        self.assertEqual(jobs, set(checks.TOP_JOBS) | {"changes", "ci-result"})
        self.assertTrue({row["job"] for row in checks.inventory()} <= set(checks.TOP_JOBS))

    def test_one_selected_browser_check_reports_all_prerequisites_separately(self):
        browser = next(row for row in checks.inventory() if row['job'] == 'web-test')
        plan = checks.execution_plan([browser['id']])
        self.assertEqual(plan['ranked_ids'], [browser['id']])
        self.assertEqual(set(plan['dependency_ids']), {'web-build', 'web-native'})
        self.assertEqual(len(plan['executed_ids']), 3)
        self.assertEqual(plan['options']['browser_matrix']['include'], [browser['browser_entry']])
        self.assertEqual(plan['options']['gcc13_shards'], [])
        self.assertFalse(plan['options']['gcc13_regressions'])
        flags = checks.flags_from_plan(plan)
        self.assertTrue(flags['browser'])
        self.assertFalse(flags['native'])
        self.assertFalse(flags['cross_platform'])

    def test_affected_selection_keeps_complete_primary_suite_and_its_producers(self):
        flags, plan, _ = selector.choose_checks(['src/game/Game_sync.cpp'], True, 'pull_request')
        self.assertEqual(plan['mode'], 'affected')
        self.assertFalse(plan['full_matrix'])
        self.assertEqual(plan['options']['gcc13_shards'], [1, 2, 3, 4])
        self.assertTrue(set(plan['dependency_ids']) <= set(plan['executed_ids']))
        self.assertEqual(plan['executed_count'], len(plan['executed_ids']))
        self.assertTrue(flags['native'] and flags['browser'])

    def test_selected_native_matrix_members_do_not_expand_to_other_shards(self):
        plan = checks.execution_plan(['native/gcc13/shard-2', 'native/gcc11/generators-sweep-a'])
        self.assertEqual(plan['options']['gcc13_shards'], [2])
        self.assertEqual(plan['options']['gcc11_shards'], [])
        self.assertEqual(plan['options']['gcc11_generators'], ['sweep-a'])
        self.assertEqual(plan['options']['gcc13_generators'], [])
        self.assertEqual(plan['options']['android_arches'], [])
        self.assertEqual(set(plan['dependency_ids']), {'native/gcc13/build', 'native/gcc11/build'})
        self.assertTrue(plan['jobs']['linux-build'])
        self.assertTrue(plan['jobs']['linux-map-generators'])
        self.assertFalse(plan['jobs']['windows'])

    def test_cheap_and_full_branches(self):
        flags, plan, _ = selector.choose_checks(['README.md'], True, 'pull_request')
        self.assertEqual(plan['mode'], 'cheap-contracts')
        self.assertEqual(plan['executed_ids'], [])
        self.assertFalse(any(flags.values()))
        flags, plan, _ = selector.choose_checks(['tools/music/glob2music/cli.py'], True, 'pull_request')
        self.assertEqual(plan['mode'], 'cheap-contracts')
        self.assertEqual(plan['executed_ids'], ['music'])
        self.assertTrue(flags['music'])
        _, plan, _ = selector.choose_checks(['.github/scripts/ci_changed_paths.py'], True, 'pull_request')
        self.assertEqual(plan['mode'], 'full')
        self.assertTrue(plan['full_matrix'])
        _, plan, _ = selector.choose_checks([], False, 'pull_request')
        self.assertEqual(plan['mode'], 'full')
        self.assertEqual(plan['exceptions'], ['diff unavailable'])
        self.assertTrue(plan['full_matrix'])
        for event in ('push', 'schedule', 'workflow_dispatch'):
            _, plan, _ = selector.choose_checks(['README.md'], True, event)
            self.assertEqual(plan['mode'], 'full')
            self.assertTrue(plan['full_matrix'])
        for kwargs in ({'called_full': True}, {}):
            labels = () if kwargs else ('ci:full',)
            _, plan, _ = selector.choose_checks(['README.md'], True, 'pull_request', labels, **kwargs)
            self.assertEqual(plan['mode'], 'full')
            self.assertTrue(plan['full_matrix'])
        _, plan, _ = selector.choose_checks([], True, 'schedule', reused=True)
        self.assertEqual(plan['mode'], 'nightly-reused')
        self.assertEqual(plan['executed_ids'], [])

    def test_documentation_beside_ci_and_build_code_stays_cheap(self):
        for path in ('.github/README.md', '.github/actions/sdl3/README.md',
                     'buildsys/README.md', 'test/build_system/README.md'):
            with self.subTest(path=path):
                _, plan, _ = selector.choose_checks([path], True, 'pull_request')
                self.assertEqual(plan['mode'], 'cheap-contracts')
                self.assertEqual(plan['executed_ids'], [])

    def test_dependency_graph_rejects_unknown_checks_and_cycles(self):
        with self.assertRaisesRegex(ValueError, 'Unknown check'):
            checks.dependency_closure(['unknown'])
        cyclic = [{'id': 'a', 'requires': ['b']}, {'id': 'b', 'requires': ['a']}]
        with self.assertRaisesRegex(ValueError, 'Cyclic'):
            checks.dependency_closure(['a'], cyclic)


if __name__ == '__main__':
    unittest.main()
