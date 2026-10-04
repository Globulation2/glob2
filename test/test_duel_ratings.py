"""Statistical invariants of the offline all-outcome duel fit."""
import math
import random
import unittest
from tools.tournaments.duel_ratings import fit, summarize, report


def game(i, placement, players=None):
    return {'job_id':str(i), 'block':str(i//2), 'generator':1, 'build':'same',
            'format':'1v1', 'competitors':players or ['a','b'], 'placements':placement}


class DuelRatingsTest(unittest.TestCase):
    def test_known_two_player_odds_and_equal_weight(self):
        games=[game(i,[1,2] if i<9 else [2,1]) for i in range(10)]
        ratings=fit(games)
        self.assertAlmostEqual(ratings['a']-ratings['b'],400*math.log10(9),places=7)
        self.assertAlmostEqual(sum(ratings.values()),3000)
        self.assertEqual(ratings,fit(games[::-1]))
        self.assertAlmostEqual(fit(games*10)['a'],ratings['a'],places=7)

    def test_draw_is_half_score(self):
        games=[game(0,[1,2]),game(1,[1,2]),game(2,[2,1]),game(3,[1.5,1.5])]
        ratings=fit(games)
        self.assertAlmostEqual(ratings['a']-ratings['b'],400*math.log10(2.5/1.5),places=7)

    def test_balanced_cycle(self):
        games=[game(0,[1,2],['a','b']),game(1,[1,2],['b','c']),game(2,[1,2],['c','a'])]
        self.assertEqual(fit(games),{'a':1500,'b':1500,'c':1500})

    def test_report_uses_batch_and_reports_missing_intervals(self):
        games=[game(0,[1,2]),game(1,[2,1])]
        result=report(games,draws=2)
        self.assertEqual(result['ratings'],{'1v1':{'a':1500,'b':1500}})
        self.assertIn('uncertainty_unavailable_reason',result['cohorts']['1v1'])
        result=report(games[:1],draws=2)
        self.assertEqual(result['ratings'],{})
        self.assertIn('unavailable_reason',result['cohorts']['1v1'])

    def test_nonfinite_fit_is_explicit(self):
        for games in ([game(0,[1,2])], [game(0,[1,1]),game(1,[1,1],['c','d'])]):
            with self.assertRaisesRegex(ValueError,'No finite batch rating'): fit(games)

    def test_bootstrap_keeps_pairs_and_is_order_independent(self):
        games=[game(i,[1,2],['a','b'] if i%2==0 else ['b','a']) for i in range(12)]
        for g in games: g['generator']=int(g['block'])%2
        expected=summarize(games,draws=20)
        random.Random(7).shuffle(games)
        self.assertEqual(expected,summarize(games,draws=20))
        self.assertEqual(expected['intervals_95_percent'],{'a':[1500,1500],'b':[1500,1500]})
        with self.assertRaisesRegex(ValueError,'Duplicate'): summarize(games+games,draws=1)
        with self.assertRaisesRegex(ValueError,'exactly two'): summarize(games[:-1],draws=1)


if __name__=='__main__': unittest.main()
