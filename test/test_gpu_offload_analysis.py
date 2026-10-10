import unittest
from gpu_offload_analysis import paired_interval, summarize, cpu_ceiling


class OffloadAnalysisTest(unittest.TestCase):
    def row(self, variant, n, cpu=100, wait=10):
        return dict(scenario='map-early', variant=variant, round=n, valid=True,
                    result=dict(benchmark_measured_ticks=10, benchmark_run_cpu_ns=cpu,
                                benchmark_run_wall_ns=100, tick_p99_ns=20,
                                benchmark_publication_wait_ns=wait))

    def test_cpu_saved_work_is_primary(self):
        rows = [r for n in range(10) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        result = summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']
        self.assertTrue(result['qualified'])  # unchanged wall time is acceptable

    def test_missing_or_invalid_samples_cannot_pass(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        self.assertFalse(summarize(rows[:-1], 'cpu', ['gpu'])['map-early']['gpu']['qualified'])
        rows[-1]['valid'] = False
        self.assertFalse(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['qualified'])

    def test_tail_regression_blocks_cpu_win(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n), self.row('gpu', n, 60))]
        for r in rows:
            if r['variant'] == 'gpu': r['result']['tick_p99_ns'] = 30
        self.assertFalse(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['qualified'])

    def test_outlier_is_retained(self):
        self.assertGreater(paired_interval([.6] * 9 + [10])['upper_one_sided95'], .7)

    def test_duplicate_pair_is_rejected(self):
        with self.assertRaises(ValueError): summarize([self.row('cpu', 0)] * 2, 'cpu', ['gpu'])

    def test_zero_wait_improvement_and_new_wait(self):
        rows = [r for n in range(5) for r in (self.row('cpu', n, wait=0), self.row('gpu', n, 60, wait=0))]
        self.assertTrue(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['qualified'])
        rows[-1]['result']['benchmark_publication_wait_ns'] = 10
        self.assertFalse(summarize(rows, 'cpu', ['gpu'])['map-early']['gpu']['qualified'])

    def test_wall_time_is_not_an_offload_ceiling(self):
        result = self.row('cpu', 0)['result']
        result['benchmark_gradient_at_start'] = dict(sample_execution_ns=0)
        result['benchmark_gradient_at_end'] = dict(sample_execution_ns=50)
        self.assertFalse(cpu_ceiling(result)['available'])
        result['benchmark_gradient_at_start'] = dict(required_seed_cpu_ns=10, required_propagation_cpu_ns=20)
        result['benchmark_gradient_at_end'] = dict(required_seed_cpu_ns=20, required_propagation_cpu_ns=30)
        ceiling = cpu_ceiling(result)
        self.assertEqual(ceiling['ideal_cpu_removal_fraction'], .2)
        self.assertFalse(ceiling['periodic_only_can_reach_30_percent'])


if __name__ == '__main__': unittest.main()
