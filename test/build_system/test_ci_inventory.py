"""Logical check budgets remain distinct from executable prerequisite closure."""

import math
import re
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import ci_check_inventory as checks
import ci_changed_paths as selector
import ci_predictive as predictor


def known_history(failing=None):
    rows = [dict(name=check['name'], branch='topic', event='pull_request',
                 sha='a' * 40, run_id=index + 1, attempt=1, created_at=0,
                 end=1, seconds=1, failed=int(check['id'] == failing), paths=[])
            for index, check in enumerate(checks.inventory())]
    return {'schema': 1, 'updated_at': 1, 'records': rows}


def fixture_model():
    return dict(schema=1, feature_schema='history54-v1', model_id='inventory-fixture',
                feature_names=['last_failed'], scales=[1.0],
                trees=[dict(left=[1, -1, -1], right=[2, -1, -1],
                            feature=[0, -2, -2], threshold=[.5, -2., -2.],
                            probability=[.1, .05, .9])])


class InventoryTest(unittest.TestCase):
    def test_every_workflow_job_has_an_explicit_inventory_flag(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text()
        jobs = set(re.findall(r"^  ([a-z][a-z0-9-]*):$", workflow.split("\njobs:\n", 1)[1], re.MULTILINE))
        self.assertEqual(jobs, set(checks.TOP_JOBS) | {"changes", "ci-result"})
        self.assertTrue({row["job"] for row in checks.inventory()} <= set(checks.TOP_JOBS))

    def test_one_ranked_browser_check_reports_all_prerequisites_separately(self):
        browser = next(row for row in checks.inventory() if row['job'] == 'web-test')
        flags, plan, _ = selector.choose_checks(['src/game/Game_sync.cpp'], True, 'pull_request',
                                                branch='topic', history=known_history(browser['id']),
                                                model=fixture_model(), budget=.01, prediction_time=10)
        self.assertEqual(plan['mode'], 'predictive')
        self.assertEqual(plan['predictor']['selected_ids'], [browser['id']])
        self.assertEqual(plan['ranked_count'], 1)
        self.assertEqual(set(plan['dependency_ids']), {'web-build', 'web-native'})
        self.assertEqual(plan['executed_count'], 3)
        self.assertEqual(plan['options']['browser_matrix']['include'], [browser['browser_entry']])
        self.assertEqual(plan['options']['gcc13_shards'], [])
        self.assertFalse(plan['options']['gcc13_regressions'])
        self.assertFalse(plan['full_matrix'])
        self.assertTrue(flags['browser'])
        self.assertFalse(flags['native'])
        self.assertFalse(flags['cross_platform'])

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

    def test_missing_history_checks_are_mandatory_beyond_the_ranked_budget(self):
        catalog = [row for row in checks.inventory() if not row.get('full_only')]
        data = known_history()
        missing = {row['name'] for row in catalog[:15]}
        data['records'] = [row for row in data['records'] if row['name'] not in missing]
        _, plan, _ = selector.choose_checks(['src/game/Game_sync.cpp'], True, 'pull_request',
                                            branch='topic', history=data, model=fixture_model(),
                                            prediction_time=10)
        expected_unknown = {row['id'] for row in catalog[:15]}
        self.assertEqual(set(plan['predictor']['unknown_ids']), expected_unknown)
        self.assertTrue(expected_unknown <= set(plan['executed_ids']))
        self.assertEqual(plan['ranked_count'], math.ceil(.12 * len(catalog)))
        self.assertGreater(plan['executed_count'], plan['ranked_count'])
        self.assertTrue(any(reason.startswith('new checks:') for reason in plan['exceptions']))

    def test_windows_label_adds_execution_without_changing_ranked_budget(self):
        browser = next(row for row in checks.inventory() if row['job'] == 'web-test')
        _, plan, _ = selector.choose_checks(['src/game/Game_sync.cpp'], True, 'pull_request',
                                            ['ci:windows'], branch='topic', history=known_history(browser['id']),
                                            model=fixture_model(), budget=.01, prediction_time=10)
        self.assertEqual(plan['predictor']['selected_ids'], [browser['id']])
        self.assertEqual(plan['ranked_count'], 1)
        self.assertIn('windows', plan['executed_ids'])
        self.assertEqual(plan['mode'], 'predictive')

    def test_cheap_and_full_branches_never_invoke_predictor(self):
        with patch.object(predictor, 'select_checks', side_effect=AssertionError('unexpected prediction')):
            flags, plan, _ = selector.choose_checks(['README.md'], True, 'pull_request')
            self.assertEqual(plan['mode'], 'cheap-contracts')
            self.assertEqual(plan['executed_ids'], [])
            self.assertFalse(any(flags.values()))
            flags, plan, _ = selector.choose_checks(['tools/music/glob2music/cli.py'], True, 'pull_request')
            self.assertEqual(plan['mode'], 'cheap-contracts')
            self.assertEqual(plan['executed_ids'], ['music'])
            self.assertTrue(flags['music'])
            for kwargs in ({'disabled': True}, {'audit': True}):
                _, plan, _ = selector.choose_checks(['src/game/Game_sync.cpp'], True, 'pull_request', **kwargs)
                self.assertEqual(plan['mode'], 'full')
                self.assertTrue(plan['full_matrix'])
            _, plan, _ = selector.choose_checks(['.github/scripts/ci_predictive.py'], True, 'pull_request')
            self.assertEqual(plan['mode'], 'full')
            self.assertTrue(plan['full_matrix'])
            _, plan, _ = selector.choose_checks([], False, 'pull_request')
            self.assertEqual(plan['mode'], 'full')
            self.assertTrue(plan['full_matrix'])

    def test_documentation_beside_ci_and_build_code_stays_cheap(self):
        for path in ('.github/README.md', '.github/actions/sdl3/README.md',
                     'scons/README.md', 'test/build_system/README.md'):
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
