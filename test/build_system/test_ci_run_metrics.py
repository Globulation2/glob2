import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('metrics', Path(__file__).resolve().parents[2] / '.github/scripts/ci_run_metrics.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class MetricsTest(unittest.TestCase):
    def test_queue_and_execution_are_separate_and_empty_cancelled_jobs_cost_zero(self):
        run = dict(id=1, head_sha='abc', event='pull_request', conclusion='success', created_at='2026-10-02T00:00:00Z')
        jobs = [dict(name='test', started_at='2026-10-02T00:01:00Z', completed_at='2026-10-02T00:05:00Z', conclusion='success', steps=[dict(started_at='2026-10-02T00:03:00Z')]), dict(name='unused', conclusion='cancelled', steps=[])]
        result = m.measure(run, jobs, [{'selection': {'native': True}}])
        self.assertEqual(result['queue_seconds'], 180)
        self.assertEqual(result['execution_seconds'], 120)
        self.assertEqual(result['runner_minutes'], 2)
        self.assertEqual(result['time_to_result_seconds'], 300)
        self.assertEqual(result['jobs'][0]['queue_seconds'], 120)
        self.assertEqual(result['cancelled_jobs'], 1)

    def test_comparison_requires_ten_successes_and_matching_coverage(self):
        run = dict(event='pull_request', selection={'native': True}, inventory_fingerprint='exact', conclusion='success', queue_seconds=10, execution_seconds=20, runner_minutes=30, time_to_result_seconds=40)
        self.assertEqual(m.compare([dict(run, inventory_fingerprint=None)]*10, [dict(run, inventory_fingerprint=None)]*10), [])
        self.assertEqual(m.compare([run]*9, [run]*10), [])
        self.assertEqual(m.compare([run]*10, [dict(run, selection={'native': False})]*10), [])
        self.assertEqual(m.compare([run]*10, [dict(run, conclusion='failure')]*10), [])
        self.assertEqual(m.compare([run]*10, [run]*10)[0]['samples_per_side'], 10)
        for mode in ('cheap-contracts', 'nightly-reused'):
            self.assertEqual(m.compare([dict(run,verification_mode=mode)]*10, [dict(run,verification_mode=mode)]*10), [])

    def test_master_and_nightly_are_full_not_last_push_diff(self):
        workflow = (Path(__file__).resolve().parents[2] / '.github/workflows/build.yml').read_text()
        self.assertIn("cron: '0 6 * * *'", workflow)
        self.assertIn('if [ "$GITHUB_EVENT_NAME" = pull_request ]', workflow)
        self.assertIn('cancel-in-progress: ${{ github.event_name == \'pull_request\' ||', workflow)

    def test_idle_gaps_do_not_count_as_execution_and_overlap_is_not_double_counted(self):
        jobs = [dict(steps=[dict(started_at='2026-10-02T00:00:00Z')],completed_at='2026-10-02T00:00:10Z'),
                dict(steps=[dict(started_at='2026-10-02T00:00:05Z')],completed_at='2026-10-02T00:00:15Z'),
                dict(steps=[dict(started_at='2026-10-02T00:02:00Z')],completed_at='2026-10-02T00:02:10Z')]
        self.assertEqual(m.active_seconds(jobs),25)

    def test_all_artifact_pages_are_read_for_large_full_matrix(self):
        calls=[]
        def read(path,token):
            calls.append(path)
            self.assertLessEqual(len(calls),2)
            return {'artifacts': list(range(100)) if path.endswith('&page=1') else ['selection']}
        self.assertEqual(len(list(m.run_artifacts('run','token',read))),101)
        self.assertEqual(len(calls),2)
        self.assertTrue(calls[1].endswith('page=2'))

class FeedbackTest(unittest.TestCase):
    def test_p90_requires_ten_exact_coverage_samples(self):
        row = dict(run_id=1, event='pull_request', conclusion='success', draft=False,
                   inventory_fingerprint='same', queue_seconds=20, execution_seconds=100,
                   runner_minutes=30, time_to_result_seconds=600)
        self.assertIsNone(m.feedback([row]*9)[0]['targets_met'])
        rows = [dict(row, run_id=n, time_to_result_seconds=600+n*50) for n in range(10)]
        result = m.feedback(rows)[0]
        self.assertEqual(result['p90']['time_to_result_seconds'],1000)
        self.assertFalse(result['targets_met'])
        self.assertTrue(m.feedback([row]*10)[0]['targets_met'])
        self.assertEqual(m.feedback([dict(row,draft=True)]*10),[])
        self.assertTrue(m.feedback([dict(row,draft=True,verification_mode='affected')]*10))
        for mode in ('cheap-contracts', 'nightly-reused'):
            self.assertEqual(m.feedback([dict(row,verification_mode=mode)]*10), [])
