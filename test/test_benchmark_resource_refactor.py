import math
import unittest

from benchmark_resource_refactor import aggregate_interval


class AggregateIntervalTest(unittest.TestCase):
    def test_independent_repeat_permutations_do_not_create_timing_blocks(self):
        # Opposite repeat ordering must not cancel independent measurement noise.
        first = [-0.12, -0.06, 0.0, 0.06, 0.12]
        second = [-value for value in first]
        original = aggregate_interval([first, second])
        permuted = aggregate_interval([second[2:] + second[:2], first[::-1]])
        self.assertEqual(original, permuted)
        self.assertAlmostEqual(original['ratio'], 1.0)
        self.assertLess(original['ci95'][0], 0.99)
        self.assertGreater(original['ci95'][1], 1.01)

    def test_constant_scenarios_keep_equal_weight_with_unequal_repeat_counts(self):
        result = aggregate_interval([[math.log(1.21)] * 3, [0.0] * 9])
        self.assertAlmostEqual(result['ratio'], 1.1)
        for bound in result['ci95']:
            self.assertAlmostEqual(bound, 1.1)

    def test_empty_strata_are_rejected(self):
        for values in ([], [[]], [[0.0], []]):
            with self.assertRaises(ValueError):
                aggregate_interval(values)


if __name__ == '__main__':
    unittest.main()
