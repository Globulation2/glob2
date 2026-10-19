"""Portable model export and causal training/publication contracts."""
import gzip
import importlib.util
import json
import os
import re
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.github/scripts'))
import ci_train_model as train
import ci_predictive as predict

DEPENDENCIES = all(importlib.util.find_spec(name) is not None
                   for name in ('numpy', 'scipy', 'sklearn'))


def observation(name, run, start, end, failed, **extra):
    return dict(name=name, run_id=run, attempt=1, created_at=start, end=end,
                branch='feature', event='pull_request', failed=failed,
                paths=['src/game/Foo.cpp'], **extra)


class PreparationTest(unittest.TestCase):
    def test_completed_labels_and_history_are_both_causal(self):
        records = [observation('a', 1, 1, 5, 1),
                   observation('a', 2, 3, 6, 0),
                   observation('a', 3, 7, 8, 0),
                   observation('b', 3, 7, 12, 1)]
        seen = []
        def features(name, paths, history, event, branch, now):
            seen.append((now, [record['run_id'] for record in history['records']]))
            return {'unknown': int(not history['records'])}
        runs = train.prepare_runs({'schema': 1, 'records': records}, 10, features)
        self.assertEqual([run['id'] for run in runs], [('1', 1), ('2', 1)])
        self.assertEqual(seen, [(1, []), (3, [])])
        # If either job's label arrives after cutoff, the whole run is excluded.
        self.assertNotIn(('3', 1), [run['id'] for run in runs])

    def test_shared_constructor_uses_past_completions_not_future_or_overlapping_runs(self):
        records = [observation('a', 1, 1, 5, 1), observation('a', 2, 3, 6, 0),
                   observation('a', 3, 7, 8, 1)]
        history = {'schema': 1, 'records': records}
        runs = train.prepare_runs(history, 10)
        self.assertEqual(runs[0]['features'][0]['unknown'], 1)
        self.assertEqual(runs[1]['features'][0]['unknown'], 1)
        self.assertEqual(runs[2]['features'][0]['last_failed'], 0)
        self.assertEqual(runs[2]['features'][0], predict.make_features(
            'a', records[2]['paths'], history, 'pull_request', 'feature', 7))

    def test_canonical_history_and_features_match_inference_across_renamed_jobs(self):
        from ci_check_inventory import inventory
        check = next(check for check in inventory() if check['id'] == 'macos')
        records = [observation('Old parent / native', 1, 1, 2, 1),
                   observation('Renamed parent / macOS build and tests', 2, 3, 4, 0),
                   observation('Current parent / macOS native build and checks', 3, 5, 6, 1)]
        for record in records:
            record['paths'] = ['platform/macos/native.mm']
        history = {'schema': 1, 'records': records}
        runs = train.prepare_runs(history, 10)
        expected = predict.make_features(check, records[2]['paths'], history,
                                         'pull_request', 'feature', 5)
        self.assertEqual(runs[2]['features'][0], expected)
        self.assertEqual(runs[2]['check_ids'], ['macos'])
        self.assertEqual(expected['observations_log'], __import__('math').log1p(2))
        self.assertEqual(expected['last_failed'], 0)
        self.assertEqual(expected['branch_known'], 1)
        self.assertEqual(runs[1]['features'][0]['unknown'], 0)

    def test_unknown_source_paths_remain_unknown_in_both_feature_paths(self):
        record = observation('a', 1, 1, 2, 1, paths_unknown=True)
        record['paths'] = []
        history = {'schema': 1, 'records': [record]}
        actual = train.prepare_runs(history, 10)[0]['features'][0]
        self.assertEqual(actual['paths_unknown'], 1)
        self.assertEqual(actual['changed_files_log'], 0)
        seen = []
        def features(name, paths, available, event, branch, now):
            seen.append(paths)
            return {'unknown': 1}
        train.prepare_runs(history, 10, features)
        self.assertEqual(seen, [None])
        record['paths_unknown'] = False
        known = train.prepare_runs(history, 10)[0]['features'][0]
        self.assertEqual(known['paths_unknown'], 0)

    def test_invalid_data_rejected(self):
        for history in ({}, {'schema': 1, 'records': [{}]},
                        {'schema': 1, 'records': [observation('a', 1, 5, 2, 1)]}):
            with self.subTest(history=history), self.assertRaises(ValueError):
                train.prepare_runs(history, 10)
        with self.assertRaises(ValueError):
            train.train({'schema': 1, 'records': []}, 10)

    def test_publication_guard_rejects_clear_regression_or_sparse_evidence(self):
        baseline = dict(runs=30, failing_runs=10, objective=.8,
                        failure_run_recall_12=.9, failure_run_recall_25=.9)
        candidate = dict(baseline)
        self.assertTrue(train.publication_guard(candidate, baseline)[0])
        self.assertFalse(train.publication_guard(dict(candidate, objective=.7), baseline)[0])
        self.assertFalse(train.publication_guard(dict(candidate, failure_run_recall_25=.7), baseline)[0])
        self.assertFalse(train.publication_guard(dict(candidate, failing_runs=2), baseline)[0])

    def test_operating_budget_regression_rejected_even_if_other_metrics_improve(self):
        baseline = dict(runs=30, failing_runs=10, objective=.8,
                        failure_run_recall_12=.9, failure_run_recall_25=.8)
        candidate = dict(baseline, objective=.85, failure_run_recall_12=.7,
                         failure_run_recall_25=.9)
        accepted, reason = train.publication_guard(candidate, baseline)
        self.assertFalse(accepted)
        self.assertIn('12%', reason)
        for key in ('objective', 'failure_run_recall_12', 'failure_run_recall_25'):
            for invalid in (None, float('nan'), -.1, 1.1):
                with self.subTest(key=key, invalid=invalid):
                    self.assertFalse(train.publication_guard(dict(baseline, **{key: invalid}), baseline)[0])

    def test_rejected_update_retains_model_but_publishes_fresh_history(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = {'schema': 1, 'feature_schema': 'history54-v1',
                     'model_id': 'previous', 'training_cutoff': 1,
                     'feature_names': ['unknown'], 'scales': [1],
                     'trees': [{'left': [-1], 'right': [-1], 'feature': [-2],
                                'threshold': [-2.0], 'probability': [.1]}]}
            train.write_json(root / 'model.json.gz', model)
            train.write_json(root / 'history.json.gz',
                             {'schema': 1, 'records': [], 'updated_at': 100})
            previous = (root / 'model.json.gz').read_bytes()
            report = {'accepted': False, 'reason': 'guard rejected'}
            with patch.object(train, 'train', return_value=(None, report)), patch('builtins.print'):
                train.main(['--history', str(root / 'history.json.gz'),
                            '--model', str(root / 'model.json.gz'),
                            '--manifest', str(root / 'manifest.json')])
            self.assertEqual((root / 'model.json.gz').read_bytes(), previous)
            manifest = train.read_json(root / 'manifest.json')
            self.assertEqual(manifest['training_update'], report)
            self.assertEqual(manifest['history_updated_at'], 100)
            self.assertEqual(manifest['model_id'], 'previous')

    def test_requested_refresh_still_backfills_missing_runs_and_new_attempts(self):
        history = {'records': [{'run_id': 1, 'attempt': 1}, {'run_id': 2, 'attempt': 1}]}
        runs = [{'id': 5, 'run_attempt': 1, 'event': 'push', 'conclusion': 'cancelled'},
                {'id': 4, 'run_attempt': 1, 'event': 'pull_request', 'conclusion': 'success'},
                {'id': 3, 'run_attempt': 1, 'event': 'push', 'conclusion': 'failure'},
                {'id': 2, 'run_attempt': 2, 'event': 'push', 'conclusion': 'success'},
                {'id': 1, 'run_attempt': 1, 'event': 'push', 'conclusion': 'success'}]
        self.assertEqual(train.refresh_run_ids(history, runs, requested='1'), [2, 3, 1])
        self.assertEqual(train.refresh_run_ids(history, runs, requested='3'), [2, 3])
        self.assertEqual(train.refresh_run_ids(history, runs), [2, 3])
        self.assertEqual(train.refresh_run_ids(history, [], requested='6'), [6])

    def test_actual_refresh_workflow_collects_missing_runs_with_event_run(self):
        workflow = (ROOT / '.github/workflows/ci-model.yml').read_text()
        body = re.search(r"          python - <<'PY'\n(.*?)          PY\n", workflow, re.S).group(1)
        script = '\n'.join(line[10:] for line in body.splitlines())
        history = {'schema': 1, 'records': [{'run_id': 1, 'attempt': 1}]}
        api = Mock()
        api.json.return_value = {'workflow_runs': [
            {'id': 2, 'run_attempt': 1, 'event': 'push', 'conclusion': 'failure'},
            {'id': 1, 'run_attempt': 1, 'event': 'push', 'conclusion': 'success'}]}
        helper = Mock()
        helper.fetch_latest_bundle.return_value = True
        helper.load_history.return_value = history
        helper.GitHub.return_value = api
        helper.collect_history.side_effect = lambda repo, identity, value: value
        with tempfile.TemporaryDirectory() as directory:
            previous = os.getcwd()
            try:
                os.chdir(directory)
                with patch.dict(sys.modules, {'ci_history': helper}), patch.object(sys, 'path', list(sys.path)), \
                        patch.dict(os.environ, {'REPOSITORY': 'owner/repo', 'COMPLETED_RUN': '1'}), \
                        patch('builtins.print'):
                    exec(compile(script, '<refresh-workflow>', 'exec'), {})
            finally:
                os.chdir(previous)
        self.assertEqual([call.args[1] for call in helper.collect_history.call_args_list], [2, 1])
        self.assertIn('per_page=100', api.json.call_args.args[0])

    def test_manifest_covers_both_inert_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            train.write_json(root / 'model.json.gz', {'schema': 1})
            train.write_json(root / 'history.json.gz', {'schema': 1, 'records': []})
            result = train.make_bundle_manifest(root)
            self.assertEqual(set(result['files']), {'model.json.gz', 'history.json.gz'})
            self.assertTrue(all(len(value) == 64 for value in result['files'].values()))
            self.assertEqual(train.read_json(root / 'model.json.gz'), {'schema': 1})


@unittest.skipUnless(DEPENDENCIES, 'optional model-training dependencies unavailable')
class ExportTest(unittest.TestCase):
    def test_stdlib_probabilities_match_sklearn_including_float32_thresholds(self):
        import numpy as np
        from sklearn.ensemble import ExtraTreesClassifier
        from sklearn.feature_extraction import DictVectorizer
        from sklearn.preprocessing import MaxAbsScaler
        rows = [{'failure_rate': float(i) / 99, 'branch_known': i % 2}
                for i in range(100)]
        vectorizer = DictVectorizer(sparse=False)
        matrix = vectorizer.fit_transform(rows)
        scaler = MaxAbsScaler()
        matrix = scaler.fit_transform(matrix)
        estimator = ExtraTreesClassifier(n_estimators=7, max_depth=4,
                                         random_state=42).fit(matrix, [i % 3 == 0 for i in range(100)])
        portable = predict.validate_model(train.export_model(
            (vectorizer, scaler, estimator), cutoff=1000))
        self.assertEqual(portable['parameters']['n_estimators'], 7)
        expected = estimator.predict_proba(matrix)[:, 1]
        actual = [predict._probability(row, portable) for row in rows]
        np.testing.assert_allclose(actual, expected, rtol=0, atol=1e-14)

    def test_fit_rejects_single_class_or_unknown_only(self):
        with self.assertRaises(ValueError):
            train.fit_model([])
        run = {'id': ('1', 1), 'features': [{'unknown': 0}] * 30,
               'records': [{'failed': 0}] * 30}
        with self.assertRaises(ValueError):
            train.fit_model([run])


if __name__ == '__main__':
    unittest.main()
