#!/usr/bin/env python3
"""Check the building-field depth model's fitter, emitter and committed header.

Needs no game build: rows are written in the layout BuildingGradientStats emits.
Fitting needs numpy; the header checks need only the standard library, and the
C++ check compiles the committed header when a C++ compiler is present.
"""
import io
import json
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import gradient_depth_fit as F

try:
    import numpy as np
    FITTING = True
except ImportError:
    FITTING = False

COLUMNS = (['tick', 'team', 'gid', 'type', 'route', 'swim', 'event', 'reason',
            'lifetime_reason', 'age', 'prev_search', 'prev_locked', 'prev_complete',
            'prev_settled_cost', 'prev_settled_tiles', 'prev_popped', 'prev_queries', 'prev_extensions']
           + [f'popped_at_depth_{i}' for i in range(F.BINS)]
           + ['width', 'height', 'level', 'is_site', 'construction_state', 'progress', 'team_units', 'team_buildings',
              'staged', 'previous_hint', 'serving_settled'])


def bins_for(actual, per_cost=1):
    bins = [0] * F.BINS
    for cost in range(actual):
        bins[min(cost // F.BIN_COST, F.BINS - 1)] += per_cost
    return bins


def row(tick, gid, actual, route='footprint', complete=False, queries=10, size=128, staged=1, hint=-1,
        serving=-1, search=1):
    bins = bins_for(actual)
    values = dict(tick=tick, team=0, gid=gid, type='inn', route=route, swim=0, event='rebuild', reason='scheduled',
                  lifetime_reason='scheduled', age=100, prev_search=search, prev_locked=0,
                  prev_complete=int(complete), prev_settled_cost=actual, prev_settled_tiles=actual // 10,
                  prev_popped=sum(bins), prev_queries=queries, prev_extensions=1, width=size, height=size, level=0,
                  is_site=0, construction_state='none', progress='', team_units=20, team_buildings=5,
                  staged=staged, previous_hint=hint, serving_settled=serving)
    values.update({f'popped_at_depth_{i}': v for i, v in enumerate(bins)})
    return values


def write(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w') as out:
        out.write(','.join(COLUMNS) + '\n')
        for r in rows:
            out.write(','.join(str(r[c]) for c in COLUMNS) + '\n')


def life(actual, complete=True, bins=None, game='g', serving=None, hint=None, queries=1, size=128):
    bins = bins or bins_for(actual)
    return dict(game=game, w=size, h=size, actual=actual, complete=complete, popped=sum(bins), queries=queries,
                bins=bins, serving=serving, hint=hint)


def history_lives(games=20, per_game=300, seed=9):
    """Lifetimes whose depth follows their deeper past depth, with a long tail."""
    rng = random.Random(seed)
    lives = []
    for game in range(games):
        for _ in range(per_game):
            serving = rng.choice((60, 200, 600, 1500))
            actual = serving + rng.randrange(0, 80) if rng.random() < 0.9 else rng.randrange(40, 3000)
            lives.append(life(actual, complete=False, serving=serving, hint=rng.choice((None, serving // 2)),
                              game=f'g{game}'))
        lives.append(life(rng.randrange(100, 900), complete=False, game=f'g{game}'))  # no history
    lives.append(life(4000, game='g0'))  # a complete field: the extrapolation profile
    return lives


def single(*lives):
    """Grid arrays of all lifetimes in one cell."""
    data = F.from_lifetimes(list(lives))
    curves = F.cell_curves(data, np.zeros(len(lives), dtype=np.int64), 1)
    return {k: v[0] for k, v in curves.items()}, data


@unittest.skipUnless(FITTING, 'fitting needs numpy')
class Work(unittest.TestCase):
    def test_complete_lifetime_integrates_its_histogram(self):
        c, _ = single(life(128, bins=[80, 48] + [0] * (F.BINS - 2)))
        for k, saved in ((3, 48), (5, 80), (6, 96), (8, 128), (F.EAGER, 128)):
            self.assertAlmostEqual(c['saved'][k], saved)
        # A complete field has nothing beyond its reach: settling deeper is free.
        self.assertEqual(c['cpu'][F.GRID], 128)
        self.assertEqual((c['hits'][7], c['hits'][8]), (0, 1))

    def test_partial_lifetime_extrapolates_from_complete_profiles(self):
        partial = life(80, complete=False, bins=[80] + [0] * (F.BINS - 1))
        whole = life(160, bins=[80, 160] + [0] * (F.BINS - 2))
        data = F.from_lifetimes([partial, whole])
        c = {k: v[0] for k, v in F.cell_curves(data, np.array([0, 1]), 2).items()}
        self.assertAlmostEqual(c['cpu'][5], 80)
        self.assertAlmostEqual(c['cpu'][8], 176)  # 80 + 160 * 48 / 80
        self.assertAlmostEqual(c['cpu'][10], 240)
        self.assertEqual(c['saved'][10], 80)

    def test_metrics(self):
        # Partial lifetimes, extrapolated by a complete field popping one entry per cost.
        lives = [life(a, complete=False, queries=q) for a, q in ((100, 1), (200, 1), (300, 1), (400, 7))]
        data = F.from_lifetimes(lives + [life(2600)])
        m = F.metrics(data, np.full(4, 304), members=np.arange(4))
        self.assertEqual(m['hit_rate'], 0.75)
        self.assertAlmostEqual(m['query_weighted_coverage'], 0.3)
        self.assertAlmostEqual(m['extra_cpu'], 1.312)  # 304 * 3 + 400 over 1000
        self.assertAlmostEqual(m['owner_saving'], 0.904)  # 100 + 200 + 300 + 304 over 1000


@unittest.skipUnless(FITTING, 'fitting needs numpy')
class Dataset(unittest.TestCase):
    def test_only_scheduled_footprint_lifetimes_with_a_search_are_read(self):
        with tempfile.TemporaryDirectory() as directory:
            write(Path(directory) / 'run' / 'gradient-stats.csv',
                  [row(100, 1, 200, serving=150, hint=90), row(150, 2, 200, staged=0),
                   row(200, 3, 200, route='clearing'), row(250, 4, 300, search=0), row(300, 5, 50)])
            F.save_dataset(F.build_dataset([Path(directory)]), Path(directory) / 'd.npz')
            data = F.load_dataset(Path(directory) / 'd.npz')
        self.assertEqual(list(zip(data['actual'].tolist(), data['serving'].tolist(), data['hint'].tolist(),
                                  data['m'].tolist())), [(200, 150, 90, 150), (50, -1, -1, -1)])
        self.assertEqual(data['group'].tolist(), [150 // F.STEP, F.UNKNOWN])
        self.assertEqual(data['games'], [f'{Path(directory).name}/run'])

    def test_cross_validation_keeps_a_game_whole(self):
        games = [f'game-{i}' for i in range(50)]
        folds = {F.fold_of(g, F.FOLDS) for g in games}
        self.assertEqual(folds, set(range(F.FOLDS)))
        self.assertEqual(F.fold_of('game-3', 5), F.fold_of('game-3', 5))


class Formula(unittest.TestCase):
    def test_the_formula_follows_the_deeper_past_depth(self):
        point = {'offset': 40, 'slope256': 256 + 64}
        self.assertEqual(F.depth(point, None), F.MIN_DEPTH)
        self.assertEqual(F.depth(point, 0), 40)
        self.assertEqual(F.depth(point, 100), 165)  # 40 + 125
        self.assertEqual(F.depth({**point, 'offset': -90}, 10), F.MIN_DEPTH)
        self.assertEqual(F.depth(point, 60000), F.MAX_DEPTH)

    @unittest.skipUnless(FITTING, 'fitting needs numpy')
    def test_vectorised_predictions_match_the_formula(self):
        lives = [life(100, serving=s, hint=h) for s, h in ((None, None), (30, 90), (300, None), (None, 5000))]
        data = F.from_lifetimes(lives)
        point = {'offset': 38, 'slope256': 257}
        expected = [F.depth(point, None), F.depth(point, 90), F.depth(point, 300), F.depth(point, 5000)]
        self.assertEqual(F.predict(data, point).tolist(), expected)


@unittest.skipUnless(FITTING, 'fitting needs numpy')
class Model(unittest.TestCase):
    def test_the_fit_recovers_a_proportional_rule(self):
        data = F.from_lifetimes(history_lives())
        for point in F.cross_validate(data, [0.3])[0]['fold_points']:
            self.assertLess(abs(point['slope256'] - 256), 40)  # depth tracks the past depth
            self.assertLess(point['offset'], 200)
        held = F.under_budget(F.cross_validate(data), 1.25)['score']
        flat = F.from_lifetimes([{**l, 'serving': None, 'hint': None} for l in history_lives()])
        self.assertGreater(held, F.under_budget(F.cross_validate(flat), 1.25)['score'] + 0.05)

    def test_the_vectorised_search_matches_scoring_each_point(self):
        data = F.from_lifetimes(history_lives(games=4))
        curves = F.cell_curves(data, data['group'], F.GROUPS)
        point = F.fit_points(curves, [0.3])[0]

        def score(offset, slope):
            index = F.point_indices({'offset': offset, 'slope256': slope})
            rows = np.arange(F.GROUPS)
            return curves['saved'][rows, index].sum() - 0.3 * curves['cpu'][rows, index].sum()
        found = score(point['offset'], point['slope256'])
        for o in range(point['offset'] - 2, point['offset'] + 3):
            for s in range(point['slope256'] - 2, point['slope256'] + 3):
                self.assertLessEqual(score(o, s), found + 1e-6)

    def test_a_higher_lambda_trades_saving_for_less_cpu(self):
        held = F.cross_validate(F.from_lifetimes(history_lives(games=6)), [0.1, 0.3, 1.0])
        cpu = [m['extra_cpu'] for m in held]
        saving = [m['owner_saving'] for m in held]
        self.assertEqual(cpu, sorted(cpu, reverse=True))
        self.assertEqual(saving, sorted(saving, reverse=True))


def synthetic_summary():
    return {'version': 5, 'games': 7, 'lifetimes': 1400, 'default': 'l0300',
            'points': [{'name': 'l0100', 'lambda': 0.1, 'offset': 160, 'slope256': 290},
                       {'name': 'l0300', 'lambda': 0.3, 'offset': 56, 'slope256': 266},
                       {'name': 'l1000', 'lambda': 1.0, 'offset': -10, 'slope256': 250}]}


class Header(unittest.TestCase):
    def test_the_header_carries_two_integers_per_point(self):
        text = F.emit_header(synthetic_summary())
        self.assertIn('inline constexpr int DEFAULT_POINT = 1; // l0300', text)
        rows = [l for l in text.splitlines() if l.startswith('\t{"l')]
        self.assertEqual(rows, ['\t{"l0100", 160, 290},', '\t{"l0300", 56, 266},', '\t{"l1000", -10, 250},'])

    def test_the_committed_header_is_what_the_summary_produces(self):
        # Regenerating must be a no-op, so the checked-in header can be trusted
        # to be the model the committed summary describes.
        summary = json.loads(F.SUMMARY.read_text())
        produced = F.emit_header(summary)

        def body(text):
            return [line for line in text.splitlines() if not line.startswith('//')]
        self.assertEqual(body(produced), body(F.HEADER.read_text()))

    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_the_engine_formula_agrees_with_the_fitter(self):
        for summary, header in ((json.loads(F.SUMMARY.read_text()), F.HEADER.read_text()),
                                (synthetic_summary(), None)):
            header = header or F.emit_header(summary)
            rng = random.Random(5)
            depths = [-1, 0, 1, 255, 256, F.MAX_DEPTH - 1] + [rng.randrange(0, F.MAX_DEPTH) for _ in range(300)]
            pairs = [(rng.choice(depths), rng.choice(depths)) for _ in range(3000)]
            points = summary['points']
            expected = [F.depth(p, max(s, h)) for p in points for s, h in pairs]
            expected += [F.depth(F.point_of(summary), max(s, h)) for s, h in pairs]
            expected += list(range(len(points))) + [-1]
            calls = [f'std::printf("%d\\n", target({s}, {h}, {i}));' for i in range(len(points)) for s, h in pairs]
            calls += [f'std::printf("%d\\n", target({s}, {h}));' for s, h in pairs]
            calls += [f'std::printf("%d\\n", pointIndex("{p["name"]}"));' for p in points]
            calls += ['std::printf("%d\\n", pointIndex("nonsense"));']
            source = ['#include "BuildingGradientDepthPolicy.h"', '#include <cstdio>',
                      'using namespace BuildingGradientDepth;', 'int main() {', *calls, '}']
            with tempfile.TemporaryDirectory() as directory:
                program = Path(directory) / 'lookup'
                (Path(directory) / 'BuildingGradientDepthPolicy.h').write_text(header)
                (Path(directory) / 'lookup.cpp').write_text('\n'.join(source) + '\n')
                subprocess.run(['c++', '-std=c++20', '-O0', '-I', directory, str(Path(directory) / 'lookup.cpp'),
                                '-o', str(program)], check=True)
                produced = [int(v) for v in subprocess.check_output([str(program)], text=True).split()]
            self.assertEqual(produced, expected)


@unittest.skipUnless(FITTING, 'fitting needs numpy')
class CommandLine(unittest.TestCase):
    def test_dataset_fit_and_evaluate(self):
        rng = random.Random(2)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            for game in range(6):
                rows = []
                for gid in range(150):
                    serving = rng.choice((60, 300, 1200))
                    rows.append(row(100 + gid, gid, serving + rng.randrange(0, 100), serving=serving,
                                    hint=serving, size=(64, 128, 256)[game % 3], complete=serving > 1000))
                write(base / f'run-{game}' / 'gradient-stats.csv', rows)
            with redirect_stdout(io.StringIO()):
                F.main(['dataset', str(base), '--output', str(base / 'd.npz'), '--jobs', '2'])
                F.main(['fit', str(base / 'd.npz'), '--operating-point', '0.3', '--points', '0.1', '1.0',
                        '--header', str(base / 'p.h'), '--summary', str(base / 's.json')])
                F.main(['evaluate', str(base / 'd.npz'), '--summary', str(base / 's.json'), '--curve',
                        '--share', 'tournament=0.1', '--report', str(base / 'curve.json')])
                out = io.StringIO()
                with redirect_stdout(out):
                    F.main(['evaluate', str(base / 'd.npz'), '--summary', str(base / 's.json')])
            rows = json.loads((base / 'curve.json').read_text())['curve']
            self.assertEqual([r['point'] for r in rows if r['scenario'] == 'all'][-1], 'eager')
            self.assertEqual(len([r for r in rows if r['scenario'] == 'all']), len(F.LAMBDAS) + 1)
            eager = next(r for r in rows if r['point'] == 'eager' and r['scenario'] == 'tournament')
            self.assertEqual(eager['owner_saving'], 1.0)
            self.assertAlmostEqual(eager['process_cpu_ratio'], round(1 + 0.1 * (eager['extra_cpu'] - 1), 4))
            report = json.loads(out.getvalue())
            self.assertEqual(set(report['in_sample_by_size']), {'64x64', '128x128', '256x256'})
            for metric in ('hit_rate', 'query_weighted_coverage', 'extra_cpu', 'owner_saving'):
                self.assertIn(metric, report['cross_validated'])
            self.assertIn('{"l0300", ', (base / 'p.h').read_text())


if __name__ == '__main__':
    unittest.main()
