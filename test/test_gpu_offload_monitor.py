import unittest
from monitor_gpu_offload import NOT_FOUND, UtilSample, utilization_record


class UtilizationStatusTest(unittest.TestCase):
    def test_supported_nonzero_entries_preserve_cursor_and_processes(self):
        samples = [UtilSample(12, 1001, 20, 1, 0, 0), UtilSample(33, 1001, 5, 0, 0, 0)]
        result = utilization_record(0, samples, 1000, False)
        self.assertTrue(result['supported_observed'])
        self.assertFalse(result['query_error'])
        self.assertEqual(result['next_cursor_us'], 1001)
        self.assertEqual([row['pid'] for row in result['samples']], [12, 33])

    def test_no_sample_is_distinct_from_zero_and_unsupported(self):
        missing = utilization_record(NOT_FOUND, [], 1000, False)
        self.assertTrue(missing['no_nonzero_samples_reported'])
        self.assertFalse(missing['supported_observed'])
        self.assertEqual(missing['samples'], [])
        supported = utilization_record(NOT_FOUND, [], 1000, True)
        self.assertTrue(supported['supported_observed'])
        unsupported = utilization_record(3, [], 1000, True)
        self.assertTrue(unsupported['query_error'])
        self.assertFalse(unsupported['no_nonzero_samples_reported'])
        self.assertEqual(unsupported['next_cursor_us'], 1000)

    def test_invalid_sample_cannot_advance_trust(self):
        for sample in (UtilSample(12, 1000, 20, 0, 0, 0), UtilSample(12, 1001, 101, 0, 0, 0)):
            with self.assertRaises(ValueError):
                utilization_record(0, [sample], 1000, False)


if __name__ == '__main__':
    unittest.main()
