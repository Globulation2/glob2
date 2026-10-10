import unittest
from analyze_gpu_offload_diagnostics import analyze


class DiagnosticsTest(unittest.TestCase):
    def test_ai_wait_and_cpu_lifetimes_are_explicitly_separate(self):
        result, diagnostics = self.fixture()
        diagnostics['ticks'][0].update(ai_deadline_wait_ns=0, ai_deadline_misses=0)
        diagnostics['ticks'][1].update(ai_deadline_wait_ns=90, ai_deadline_misses=1)
        diagnostics['ai_metrics_at_start'] = dict(job_cpu_diagnostics=1, decision_and_command_capture_cpu_ns=10, jobs_completed=1)
        diagnostics['ai_metrics_at_end'] = dict(job_cpu_diagnostics=1, decision_and_command_capture_cpu_ns=70, jobs_completed=3)
        report = analyze(result, diagnostics)
        self.assertEqual(report['ai_counters']['deltas']['decision_and_command_capture_cpu_ns'], 60)
        self.assertTrue(report['ai_counters']['cpu_diagnostics_enabled'])
        self.assertEqual(report['slowest_one_percent']['ai_deadline_majority_ticks'], 1)
        diagnostics['ai_metrics_at_end']['jobs_completed'] = 0
        with self.assertRaises(ValueError):
            analyze(result, diagnostics)

    def test_sampled_scope_cpu_never_claims_complete_call_coverage(self):
        result, diagnostics = self.fixture()
        initial = dict(scope='Sampled', inclusive_cpu_ns=10, self_cpu_ns=5,
                       calls=64, wall_samples=1, cpu_samples=1, self_complete=True)
        diagnostics['owner_scopes_at_start'] = [initial]
        diagnostics['owner_scopes_at_end'] = [dict(initial, calls=128, wall_samples=2,
            cpu_samples=2, inclusive_cpu_ns=40, self_cpu_ns=25)]
        scope = analyze(result, diagnostics)['owner_scopes'][0]
        self.assertEqual(scope['inclusive_cpu_ns'], 30)
        self.assertEqual(scope['cpu_sample_fraction'], 1/64)
        self.assertFalse(scope['self_complete'])

    def fixture(self):
        def thread(tid, cpu, started=1):
            return dict(tid=tid, start_ticks=started, user_cpu_ns=cpu, system_cpu_ns=0, name='same-inherited-name')
        def boundary(threads):
            return dict(available=True, owner_tid=1, threads=threads, clock_ticks_per_second=100, failed_threads=0)
        result = dict(benchmark_run_cpu_ns=60, benchmark_measured_ticks=2,
                      benchmark_gradient_at_start={'coordinator_tid': 2},
                      benchmark_gradient_at_end={'coordinator_tid': 2, 'compute_worker_tid_1': 3})
        diagnostics = dict(threads_at_start=boundary([thread(1, 10), thread(2, 20), thread(3, 30), thread(4, 40)]),
                           threads_at_end=boundary([thread(1, 20), thread(2, 40), thread(3, 60), thread(4, 90, started=2)]),
                           owner_scopes_at_start=[], owner_scopes_at_end=[],
                           ticks=[dict(tick=1, wall_ns=10, publication_wait_ns=1, gpu_publication_wait_ns=0, gpu_backend_overlap_wait_ns=0, building_wait_ns=0),
                                  dict(tick=2, wall_ns=100, publication_wait_ns=70, gpu_publication_wait_ns=60, gpu_backend_overlap_wait_ns=50, building_wait_ns=0)])
        return result, diagnostics

    def test_tid_roles_and_reuse_require_actual_identity(self):
        report = analyze(*self.fixture())
        roles = {t['tid']: t['role'] for t in report['threads']}
        self.assertEqual(roles, {1: 'owner', 2: 'coordinator', 3: 'compute_worker'})
        self.assertEqual(report['matched_thread_cpu_ns'], 60)
        self.assertEqual(len(report['unmatched_threads']['started']), 1)
        self.assertEqual(len(report['unmatched_threads']['exited']), 1)
        self.assertFalse(report['acceptance_eligible'])
        self.assertEqual(report['slowest_one_percent']['gpu_publication_majority_ticks'], 1)

    def test_partial_tick_coverage_cannot_be_interpreted(self):
        result, diagnostics = self.fixture()
        diagnostics['ticks'].pop()
        with self.assertRaises(ValueError):
            analyze(result, diagnostics)

    def test_counter_regression_is_rejected(self):
        result, diagnostics = self.fixture()
        diagnostics['threads_at_end']['threads'][0]['user_cpu_ns'] = 0
        with self.assertRaises(ValueError):
            analyze(result, diagnostics)


if __name__ == '__main__':
    unittest.main()
