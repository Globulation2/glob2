"""Causal trace analysis must preserve missing evidence and early resolution semantics."""
import unittest
from compute_trace import analyze


class ComputeTraceTest(unittest.TestCase):
    def trace(self, events, loss=0):
        defaults = {'serial': 1, 'execution_kind': 0, 'job': 0, 'lane': 0,
                    'lane_sequence': 0, 'producer': 'hiring'}
        return {'schema_version': 1, 'mode': 'trace', 'trace_included': True,
                'trace_streams': [{'stream': 0, 'lost_events': loss,
                                  'events': [defaults | event for event in events]}]}

    def test_lane_queue_external_completion_and_wakeup(self):
        trace = self.trace([
            {'event': 'submit', 'timestamp_ns': 10},
            {'event': 'claim', 'timestamp_ns': 15},
            {'event': 'callback_start', 'timestamp_ns': 20},
            {'event': 'callback_end', 'timestamp_ns': 30},
            {'event': 'complete', 'timestamp_ns': 40},
            {'event': 'claim', 'timestamp_ns': 50, 'job': 1, 'lane_sequence': 1},
            {'event': 'callback_start', 'timestamp_ns': 55, 'job': 1, 'lane_sequence': 1},
            {'event': 'suspend', 'timestamp_ns': 60, 'job': 1, 'lane_sequence': 1},
            {'event': 'callback_end', 'timestamp_ns': 70, 'job': 1, 'lane_sequence': 1},
            {'event': 'external_resolution', 'timestamp_ns': 90, 'job': 1, 'lane_sequence': 1},
            {'event': 'complete', 'timestamp_ns': 95, 'job': 1, 'lane_sequence': 1},
            {'event': 'join_inspection', 'timestamp_ns': 96},
            {'event': 'join_end', 'timestamp_ns': 100},
        ])
        result = analyze(trace)
        job = result['jobs'][1]
        self.assertEqual(job['lane_dependency_ns'], 30)
        self.assertEqual(job['eligible_queue_ns'], 10)
        self.assertEqual(job['callback_elapsed_ns'], 15)
        self.assertEqual(job['external_completion_ns'], 20)
        self.assertEqual(result['joins'][0]['completion_to_join_end_ns'], 5)

    def test_continuation_preserves_external_wait_before_the_final_callback(self):
        result = analyze(self.trace([
            {'event': 'callback_start', 'timestamp_ns': 1},
            {'event': 'suspend', 'timestamp_ns': 2},
            {'event': 'callback_end', 'timestamp_ns': 3},
            {'event': 'external_resolution', 'timestamp_ns': 13},
            {'event': 'callback_start', 'timestamp_ns': 14},
            {'event': 'suspend', 'timestamp_ns': 15},
            {'event': 'external_resolution', 'timestamp_ns': 16},
            {'event': 'callback_end', 'timestamp_ns': 17},
            {'event': 'complete', 'timestamp_ns': 18},
        ]))
        self.assertEqual(result['jobs'][0]['external_completion_ns'], 10)
        self.assertEqual(result['jobs'][0]['callback_count'], 2)

    def test_loss_and_missing_predecessor_are_not_zero(self):
        result = analyze(self.trace([{'event': 'submit', 'timestamp_ns': 1},
                                     {'event': 'claim', 'timestamp_ns': 10, 'lane_sequence': 2}], 3))
        self.assertTrue(result['incomplete'])
        self.assertIsNone(result['jobs'][0]['eligible_queue_ns'])
        self.assertIsNone(result['jobs'][0]['lane_dependency_ns'])

    def test_coverage_gap_is_incomplete_even_without_dropped_events(self):
        trace = self.trace([])
        trace['trace_incomplete'] = True
        result = analyze(trace)
        self.assertTrue(result['incomplete'])
        self.assertEqual(result['lost_events'], 0)

    def test_summary_is_not_a_trace(self):
        with self.assertRaises(ValueError):
            analyze({'schema_version': 1, 'mode': 'waits'})


if __name__ == '__main__':
    unittest.main()
