import collections
import unittest
import tempfile
from pathlib import Path
from analyze_building_gradient_demand import required_cost, weighted_quantile, fit, predict, evaluate, read_match

F = ('inn', 100, 12, 0, 0, -1)


def match(name, cost, count=1):
    return {'directory': name, 'rows': collections.Counter({(F, cost): count}), 'epochs': {}}


class DemandAnalysisTest(unittest.TestCase):
    def test_reachable_cost_and_exhaustion_are_different(self):
        row = dict(forbidden=0, full=0, reachable=1, cost=100, complete=1, after_cost=501)
        self.assertEqual(required_cost(row), 100)
        row['full'] = 1
        self.assertEqual(required_cost(row), 500)
        row.update(full=0, reachable=0)
        self.assertEqual(required_cost(row), 500)
        row.update(complete=0)
        self.assertIsNone(required_cost(row))
        row['forbidden'] = 1
        self.assertEqual(required_cost(row), 0)

    def test_quantile_and_cardinal_cost_rounding(self):
        self.assertEqual(weighted_quantile({0: 94, 303: 6}, .95), 303)
        self.assertEqual(predict(fit([match(str(i), 303) for i in range(3)], .95, 'queries'), F)[0], 310)

    def test_training_equal_weights_whole_matches(self):
        model = fit([match('huge', 1000, 1000000), match('a', 10), match('b', 10)], .5, 'queries')
        self.assertEqual(predict(model, F)[0], 10)

    def test_holdout_never_trains_on_its_requests(self):
        report = evaluate([match(str(i), 10) for i in range(3)] + [match('outlier', 1000)], .95, 'queries')
        self.assertEqual(report['matches'][-1]['coverage'], 0)

    def test_unknown_costs_remain_in_denominator(self):
        report = evaluate([match(str(i), 10) for i in range(3)] + [match('unknown', None, 10)], .95, 'queries')
        self.assertEqual(report['matches'][-1]['observations'], 10)
        self.assertEqual(report['matches'][-1]['coverage'], 0)

    def test_dropped_queries_reject_incomplete_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'building-gradient-demand-ticks.csv').write_text('tick,requests,rows,dropped_requests\n1,3,2,1\n')
            with self.assertRaisesRegex(ValueError, 'dropped requests'):
                read_match(root)

    def test_insufficient_match_support_does_not_predict(self):
        self.assertEqual(predict(fit([match('a', 10)], .95, 'queries'), F), (None, None))


if __name__ == '__main__':
    unittest.main()
