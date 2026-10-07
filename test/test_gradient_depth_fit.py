#!/usr/bin/env python3
"""Check the building-field depth fitter on synthetic gradient-stats rows.

Needs no build: rows are written in the CSV layout BuildingGradientStats emits.
"""
import io
import json
import random
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import gradient_depth_fit as F

COLUMNS = ['tick', 'team', 'gid', 'type', 'route', 'swim', 'event', 'reason', 'shadow_verdict',
           'lifetime_reason', 'lifetime_verdict', 'age', 'prev_search', 'prev_locked', 'prev_complete',
           'prev_settled_cost', 'prev_settled_tiles', 'prev_popped', 'prev_queries', 'prev_extensions'] + \
          [f'popped_at_depth_{i}' for i in range(F.BINS)]


def bins_for(actual, per_cost=1):
    """A lifetime popping `per_cost` entries per settled cost layer."""
    bins = [0] * F.BINS
    for cost in range(actual):
        bins[min(cost // F.BIN_COST, F.BINS - 1)] += per_cost
    return bins


def row(tick, gid, actual, event='rebuild', reason='generation', complete=False, kind='inn', route='footprint',
        swim=0, queries=10, search=True):
    bins = bins_for(actual)
    values = dict(tick=tick, team=0, gid=gid, type=kind, route=route, swim=swim, event=event,
                  reason=reason if event == 'rebuild' else '', shadow_verdict='', lifetime_reason='',
                  lifetime_verdict='', age=100, prev_search=int(search), prev_locked=0,
                  prev_complete=int(complete), prev_settled_cost=actual, prev_settled_tiles=actual // 10,
                  prev_popped=sum(bins), prev_queries=queries, prev_extensions=1)
    values.update({f'popped_at_depth_{i}': v for i, v in enumerate(bins)})
    return values


def write(path, rows):
    with open(path, 'w') as out:
        out.write(','.join(COLUMNS) + '\n')
        for r in rows:
            out.write(','.join(str(r[c]) for c in COLUMNS) + '\n')


def chain(rng, gid, lifetimes, rule):
    """One field's successive lifetimes, each actual depth derived from the previous one."""
    rows = [dict(row(0, gid, 0, reason='null'), prev_search='', prev_complete='', prev_settled_cost='')]
    actual = rng.randrange(100, 400)
    for i in range(lifetimes):
        actual = rule(actual, rng)
        # The deepest lifetimes explored their whole field.
        rows.append(row(100 * (i + 1), gid, actual, complete=actual >= 2000))
    return rows


class PoppedIntegral(unittest.TestCase):
    def test_complete_lifetime_interpolates_its_histogram(self):
        bins = [80, 40] + [0] * (F.BINS - 2)
        life = F.Lifetime('s', ('footprint', 0, 'inn'), 120, None, True, 120, 1, bins)
        self.assertEqual(F.popped_to(life, 40, None), 40)
        self.assertEqual(F.popped_to(life, 80, None), 80)
        self.assertAlmostEqual(F.popped_to(life, 100, None), 100)
        self.assertEqual(F.popped_to(life, 120, None), 120)
        # A complete field has nothing beyond its reach.
        self.assertEqual(F.popped_to(life, 400, None), 120)
        self.assertEqual(F.popped_to(life, F.FULL, None), 120)

    def test_partial_lifetime_extrapolates_from_the_profile(self):
        bins = [80] + [0] * (F.BINS - 1)
        life = F.Lifetime('s', ('footprint', 0, 'inn'), 80, None, False, 80, 1, bins)
        profile = [80.0, 160.0] + [0.0] * (F.BINS - 2)
        self.assertEqual(F.popped_to(life, 80, profile), 80)
        self.assertAlmostEqual(F.popped_to(life, 120, profile), 160)
        self.assertAlmostEqual(F.popped_to(life, 160, profile), 240)


class Loading(unittest.TestCase):
    def test_predecessors_follow_each_field_and_reset_on_drop(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'gradient-stats.csv'
            write(path, [row(100, 1, 200), row(200, 1, 300), row(250, 2, 50), row(300, 1, 120, event='drop'),
                         row(400, 1, 90), dict(row(450, 3, 10), prev_search=0)])
            lives = F.load(path, 'seed')
        self.assertEqual([(l.actual, l.previous) for l in lives], [(200, None), (300, 200), (50, None),
                                                                     (120, 300), (90, None)])


class Fitting(unittest.TestCase):
    def runs(self, rule, seeds=(1, 2, 3), fields=40, lifetimes=8):
        result = {}
        for seed in seeds:
            rng = random.Random(seed)
            rows = []
            for gid in range(fields):
                rows += chain(rng, gid, lifetimes, rule)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'gradient-stats.csv'
                write(path, rows)
                result[str(seed)] = F.load(path, str(seed))
        return result

    def test_constant_quantile_metrics(self):
        lives = [F.Lifetime('s', ('footprint', 0, 'inn'), a, None, False, a, q, bins_for(a))
                 for a, q in ((100, 1), (200, 1), (300, 1), (400, 7))]
        model = F.fit_constant(lives, 0.75)
        self.assertEqual(model.rule(('footprint', 0, 'inn'))[1], (0.0, 300))
        # Partial lifetimes are extrapolated by a complete field popping one entry per cost.
        score = F.evaluate(model, lives, {('*',): [float(v) for v in bins_for(2600)]})
        self.assertEqual(score['hit_rate'], 0.75)
        self.assertAlmostEqual(score['query_weighted_coverage'], 3 / 10)
        # Work at max(D, actual): 300 + 300 + 300 + 400 over 1000 actual.
        self.assertAlmostEqual(score['extra_cpu'], 1300 / 1000)
        # Work moved off the owner at min(D, actual): 100 + 200 + 300 + 300.
        self.assertAlmostEqual(score['owner_saving'], 900 / 1000)

    def test_linear_family_recovers_a_predictable_depth_and_is_adopted(self):
        runs = self.runs(lambda previous, rng: max(60, min(2000, int(previous * 1.0) + rng.randrange(0, 40))))
        linear = F.cross_validate(runs, 'linear')
        self.assertGreaterEqual(linear['pooled']['hit_rate'], F.ADOPT_HIT_RATE)
        self.assertLessEqual(linear['pooled']['extra_cpu'], 1.25)
        self.assertTrue(F.adopt(linear['pooled']))
        model = F.fit_linear([l for lives in runs.values() for l in lives], {})
        alpha, beta = model.rule(('footprint', 0, 'inn'))[1]
        self.assertGreater(alpha, 0)
        text = F.header(model, list(runs))
        self.assertIn(f'{{0, 0, "inn", {int(round(alpha * 256))}, {int(beta)}}}', text)
        self.assertIn('BUILDING_GRADIENT_DEPTH_RULES', text)

    def test_unpredictable_depths_keep_the_constant_rule(self):
        runs = self.runs(lambda previous, rng: rng.choice((40, 60, 2000, 3000)))
        linear = F.cross_validate(runs, 'linear')
        self.assertFalse(F.adopt(linear['pooled']))

    def test_command_line_reports_and_emits_the_adopted_table(self):
        rng = random.Random(7)
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for seed in (19, 23):
                rows = []
                for gid in range(40):
                    rows += chain(rng, gid, 6, lambda previous, r: previous + r.randrange(0, 20))
                path = Path(directory) / f'seed{seed}.csv'
                write(path, rows)
                paths.append(f'{seed}={path}')
            report, header = Path(directory) / 'report.json', Path(directory) / 'policy.h'
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(F.main(paths + ['--report', str(report), '--header', str(header)]), 0)
            data = json.loads(report.read_text())
            self.assertEqual(data['seeds'], ['19', '23'])
            self.assertIn(data['adopted'], ('linear', 'constant'))
            for family in ('constant', 'linear'):
                for metric in ('hit_rate', 'query_weighted_coverage', 'extra_cpu', 'owner_saving'):
                    self.assertIn(metric, data[family]['pooled'])
            self.assertIn('#pragma once', header.read_text())
            self.assertIn('adopted', out.getvalue())


if __name__ == '__main__':
    unittest.main()
