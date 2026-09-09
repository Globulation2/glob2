import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import maxima_sequential_confirmation as seq


class SequentialConfirmation(unittest.TestCase):
    def setUp(self):
        self.alpha = .04 / 53
        self.looks = seq.schedule(self.alpha)

    def pairs(self, n, effect=False):
        return [{'scenario_id': str(i), 'on': 0,
                 'off': int(effect and i < n // 5)} for i in range(n)]

    def test_budget_and_final_look(self):
        seq.validate_schedule(self.looks, self.alpha, 21000)
        self.assertAlmostEqual(sum(r['alpha'] for r in self.looks), self.alpha)
        with self.assertRaises(ValueError):
            seq.validate_schedule(self.looks * 2, self.alpha, 21000)

    def test_no_unscheduled_peeking(self):
        with self.assertRaises(ValueError):
            seq.evaluate(self.pairs(500), self.looks, self.alpha, 21000)

    def test_clear_effect_stops_both_directions(self):
        rows = self.pairs(1000, True)
        result = seq.evaluate(rows, self.looks, self.alpha, 21000)
        self.assertTrue(result['stop'])
        self.assertEqual(result['overall']['direction'], 'harmful')
        reverse = [dict(r, on=r['off'], off=r['on']) for r in rows]
        self.assertEqual(seq.evaluate(reverse, self.looks, self.alpha, 21000)
                         ['overall']['direction'], 'helpful')

    def test_no_effect_continues_until_cap(self):
        self.assertFalse(seq.evaluate(self.pairs(1000), self.looks, self.alpha, 21000)['stop'])
        self.assertEqual(seq.evaluate(self.pairs(21000), self.looks, self.alpha, 21000)
                         ['reason'], 'maximum_sample')

    def test_missing_duplicate_or_repeat_rejected(self):
        for change in ({'on': None}, {'scenario_id': '1'}, {'repeat': True}):
            rows = self.pairs(1000)
            rows[0].update(change)
            with self.assertRaises(ValueError):
                seq.evaluate(rows, self.looks, self.alpha, 21000)


if __name__ == '__main__':
    unittest.main()
