#!/usr/bin/env python3
"""Check the building-field depth model's fitter, emitter and committed header.

Needs no game build: rows are written in the layout BuildingGradientStats emits.
The header check compiles the committed header when a C++ compiler is present.
"""
import gzip
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

COLUMNS = (['tick', 'team', 'gid', 'type', 'route', 'swim', 'event', 'reason', 'shadow_verdict',
            'lifetime_reason', 'lifetime_verdict', 'age', 'prev_search', 'prev_locked', 'prev_complete',
            'prev_settled_cost', 'prev_settled_tiles', 'prev_popped', 'prev_queries', 'prev_extensions']
           + [f'popped_at_depth_{i}' for i in range(F.BINS)]
           + ['width', 'height', 'level', 'is_site', 'construction_state', 'progress', 'team_units', 'team_buildings'])


def bins_for(actual, per_cost=1):
    bins = [0] * F.BINS
    for cost in range(actual):
        bins[min(cost // F.BIN_COST, F.BINS - 1)] += per_cost
    return bins


def row(tick, gid, actual, event='rebuild', reason='generation', complete=False, kind='inn', route='footprint',
        swim=0, queries=10, size=128, units=20, site=0):
    bins = bins_for(actual)
    values = dict(tick=tick, team=0, gid=gid, type=kind, route=route, swim=swim, event=event,
                  reason=reason if event == 'rebuild' else '', shadow_verdict='', lifetime_reason='',
                  lifetime_verdict='', age=100, prev_search=1, prev_locked=0, prev_complete=int(complete),
                  prev_settled_cost=actual, prev_settled_tiles=actual // 10, prev_popped=sum(bins),
                  prev_queries=queries, prev_extensions=1, width=size, height=size, level=0, is_site=site,
                  construction_state='new' if site else 'none', progress=2 if site else '', team_units=units,
                  team_buildings=5)
    values.update({f'popped_at_depth_{i}': v for i, v in enumerate(bins)})
    return values


def write(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w') as out:
        out.write(','.join(COLUMNS) + '\n')
        for r in rows:
            out.write(','.join(str(r[c]) for c in COLUMNS) + '\n')


def life(actual, complete=True, bins=None, **values):
    item = F.Lifetime(game=values.pop('game', 'g'), route=0, swim=0, type='inn', level=0, site=0, construction=0,
                      progress=-1, w=128, h=128, units=10, buildings=5, previous=None, actual=actual,
                      complete=complete, popped=sum(bins or bins_for(actual)), queries=1,
                      bins=bins or bins_for(actual))
    for key, value in values.items():
        setattr(item, key, value)
    return item


def synthetic_runs(directory, games=10, rule=None, seed=1):
    """Plain run directories whose depth follows `rule(kind, size, rng)`."""
    rng = random.Random(seed)
    for game in range(games):
        rows = []
        for gid in range(60):
            kind = ('inn', 'swarm', 'clearingflag')[gid % 3]
            size = (64, 128, 256)[game % 3]
            route = 'clearing' if kind == 'clearingflag' else 'footprint'
            for tick in range(3):
                actual = rule(kind, size, rng)
                rows.append(row(100 * (tick + 1), gid, actual, kind=kind, route=route, size=size,
                                complete=actual > 1500))
        write(Path(directory) / f'run-{game}' / 'gradient-stats.csv', rows)


class Work(unittest.TestCase):
    def test_complete_lifetime_interpolates_its_histogram(self):
        item = life(120, bins=[80, 40] + [0] * (F.BINS - 2))
        self.assertEqual(F.popped_to(item, 40), 40)
        self.assertEqual(F.popped_to(item, 80), 80)
        self.assertAlmostEqual(F.popped_to(item, 100), 100)
        self.assertEqual(F.popped_to(item, 120), 120)
        # A complete field has nothing beyond its reach: settling deeper is free.
        self.assertEqual(F.popped_to(item, 4000), 120)

    def test_partial_lifetime_extrapolates_from_complete_profiles(self):
        partial = life(80, complete=False, bins=[80] + [0] * (F.BINS - 1))
        whole = life(160, bins=[80, 160] + [0] * (F.BINS - 2))
        F.prepare([partial, whole])
        self.assertEqual(F.popped_to(partial, 80), 80)
        self.assertAlmostEqual(F.popped_to(partial, 120), 160)
        self.assertAlmostEqual(F.popped_to(partial, 160), 240)

    def test_metrics(self):
        # Partial lifetimes, extrapolated by a complete field popping one entry per cost.
        lives = [life(a, complete=False, queries=q) for a, q in ((100, 1), (200, 1), (300, 1), (400, 7))]
        whole = life(2600)
        F.prepare(lives + [whole])
        m = F.metrics([(l, 300) for l in lives])
        self.assertEqual(m['hit_rate'], 0.75)
        self.assertAlmostEqual(m['query_weighted_coverage'], 0.3)
        self.assertAlmostEqual(m['extra_cpu'], 1.3)  # 300+300+300+400 over 1000
        self.assertAlmostEqual(m['owner_saving'], 0.9)  # 100+200+300+300 over 1000


class Dataset(unittest.TestCase):
    def test_rows_link_predecessors_and_read_owner_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'run' / 'gradient-stats.csv'
            write(path, [row(100, 1, 200), row(200, 1, 300, site=1), row(250, 2, 50),
                         row(300, 1, 120, event='drop'), row(400, 1, 90), dict(row(450, 3, 10), prev_search=0)])
            data = F.build_dataset([Path(directory)])
        lives = [F.Lifetime.from_record(v) for v in data['lifetimes']]
        self.assertEqual([(l.actual, l.previous) for l in lives],
                         [(200, None), (300, 200), (50, None), (120, 300), (90, None)])
        self.assertEqual((lives[1].site, lives[1].construction, lives[1].progress, lives[1].w), (1, 1, 2, 128))
        self.assertEqual((lives[0].site, lives[0].progress, lives[0].units), (0, -1, 20))
        self.assertEqual(list(data['games']), [f'{Path(directory).name}/run'])

    def test_cross_validation_keeps_a_game_whole(self):
        games = [f'game-{i}' for i in range(50)]
        folds = {F.fold_of(g, F.FOLDS) for g in games}
        self.assertEqual(folds, set(range(F.FOLDS)))
        self.assertEqual(F.fold_of('game-3', 5), F.fold_of('game-3', 5))


class Selection(unittest.TestCase):
    def load(self, directory):
        data = F.build_dataset([Path(directory)])
        dataset = Path(directory) / 'dataset.json.gz'
        with gzip.open(dataset, 'wt') as out:
            json.dump(data, out)
        return F.load_dataset(dataset)

    def test_a_key_that_determines_depth_is_selected(self):
        depth = {'inn': 1800, 'swarm': 300, 'clearingflag': 900}
        with tempfile.TemporaryDirectory() as directory:
            synthetic_runs(directory, rule=lambda kind, size, rng: depth[kind] + rng.randrange(0, 60))
            _, lives = self.load(directory)
        report = F.screen(lives, candidates=['type', 'units', 'level'])
        self.assertEqual(report['selected']['keys'][0], 'type')
        alone = report['alone']['raw']
        self.assertGreater(alone['type']['score'], alone['units']['score'] + 0.05)

    def test_size_proportional_depth_selects_the_per_size_target(self):
        with tempfile.TemporaryDirectory() as directory:
            synthetic_runs(directory, games=12, rule=lambda kind, size, rng: size * 6 + rng.randrange(0, 40))
            _, lives = self.load(directory)
        report = F.screen(lives, candidates=['units', 'level'])
        self.assertTrue(report['selected']['per_size'])


class Header(unittest.TestCase):
    def summary(self):
        lives = [life(a, type=('inn', 'swarm')[i % 2], game=f'g{i % 7}') for i, a in
                 enumerate(random.Random(3).choices(range(100, 3000), k=400))]
        return F.summarise(lives, {f'g{i}': {} for i in range(7)}, ['type'], False, 0.8, [0.5, 0.95])

    def test_the_header_carries_one_rule_per_supported_cell(self):
        summary = self.summary()
        text = F.emit_header(summary)
        table = F.table_from_summary(summary)
        self.assertEqual(text.count('\t{1, {0}, "'), len([c for c in table if c]))
        self.assertIn('\t{0, {0}, "", {', text)
        self.assertNotIn('.', ''.join(l for l in text.splitlines() if l.startswith('\t{')))  # integers only

    def test_operating_points_are_ordered_and_the_default_is_named(self):
        summary = self.summary()
        self.assertEqual([p['name'] for p in summary['points']], ['p50', 'p80', 'p95'])
        self.assertEqual(summary['default'], 'p80')
        text = F.emit_header(summary)
        self.assertIn('inline constexpr int DEFAULT_POINT = 1; // p80', text)
        self.assertIn('POINT_QUANTILE_PERMILLE[POINT_COUNT] = {500, 800, 950}', text)
        low, mid, high = (F.table_from_summary(summary, q) for q in (0.5, 0.8, 0.95))
        self.assertEqual(set(low), set(high))  # the same cells at every point
        self.assertTrue(all(low[c] <= mid[c] <= high[c] for c in low))

    def test_the_committed_header_is_what_the_summary_produces(self):
        # Regenerating must be a no-op, so the checked-in header can be trusted
        # to be the table the committed summary describes.
        summary = json.loads(F.SUMMARY.read_text())
        produced = F.emit_header(summary)
        def body(text):
            return [line for line in text.splitlines() if not line.startswith('//')]
        self.assertEqual(body(produced), body(F.HEADER.read_text()))

    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_the_engine_lookup_agrees_with_the_fitter(self):
        summary = json.loads(F.SUMMARY.read_text())
        points = F.summary_points(summary)
        table = F.table_from_summary(summary)
        rng = random.Random(5)
        kinds = sorted({c[summary['keys'].index('type')] for c in table if 'type' in summary['keys']
                        and len(c) > summary['keys'].index('type')} | {'inn', 'unknown'})
        queries = []
        for _ in range(300):
            size = rng.choice((64, 128, 256, 512))
            queries.append(life(1, route=rng.randrange(3), swim=rng.choice((0, 4)), type=rng.choice(kinds),
                                level=rng.randrange(3), site=rng.randrange(2), construction=rng.randrange(4),
                                progress=rng.randrange(-1, 4), w=size, h=size, units=rng.randrange(0, 300),
                                buildings=rng.randrange(0, 80),
                                previous=rng.choice((None, rng.randrange(0, 4000)))))
        expected = [F.predict(F.table_from_summary(summary, point['quantile']), summary['keys'],
                              summary['per_size'], q) for point in points for q in queries]
        # The default target() is the committed default point.
        expected += [F.predict(table, summary['keys'], summary['per_size'], q) for q in queries]
        source = ['#include "BuildingGradientDepthPolicy.h"', '#include <cstdio>', 'int main() {',
                  'using namespace BuildingGradientDepth;']
        calls = [(index, q) for index in range(len(points)) for q in queries] + [(None, q) for q in queries]
        for index, q in calls:
            point = '' if index is None else f', {index}'
            source.append(f'std::printf("%d\\n", target(Query{{{q.route}, {q.swim}, "{q.type}", {q.level}, {q.site}, '
                          f'{q.construction}, {q.progress}, {q.w}, {q.h}, {q.units}, {q.buildings}, '
                          f'{-1 if q.previous is None else q.previous}}}{point}));')
        source.append('}')
        with tempfile.TemporaryDirectory() as directory:
            program = Path(directory) / 'lookup'
            (Path(directory) / 'lookup.cpp').write_text('\n'.join(source) + '\n')
            subprocess.run(['c++', '-std=c++20', '-I', str(F.HEADER.parent), str(Path(directory) / 'lookup.cpp'),
                            '-o', str(program)], check=True)
            produced = [int(v) for v in subprocess.check_output([str(program)], text=True).split()]
        self.assertEqual(produced, expected)


class CommandLine(unittest.TestCase):
    def test_dataset_fit_and_evaluate(self):
        depth = {'inn': 1800, 'swarm': 300, 'clearingflag': 900}
        with tempfile.TemporaryDirectory() as directory:
            synthetic_runs(directory, games=6, rule=lambda kind, size, rng: depth[kind] + rng.randrange(0, 200))
            base = Path(directory)
            with redirect_stdout(io.StringIO()):
                F.main(['dataset', str(base), '--output', str(base / 'd.json.gz')])
                F.main(['fit', str(base / 'd.json.gz'), '--keys', 'type', '--operating-point', '0.8',
                        '--points', '0.5', '0.95', '--header', str(base / 'p.h'), '--summary', str(base / 's.json')])
                F.main(['evaluate', str(base / 'd.json.gz'), '--summary', str(base / 's.json'), '--curve',
                        '--share', 'tournament=0.1', '--report', str(base / 'curve.json')])
                out = io.StringIO()
                with redirect_stdout(out):
                    F.main(['evaluate', str(base / 'd.json.gz'), '--summary', str(base / 's.json')])
            rows = json.loads((base / 'curve.json').read_text())['curve']
            self.assertEqual([r['point'] for r in rows if r['scenario'] == 'all'],
                             ['p50', 'p60', 'p70', 'p80', 'p85', 'p90', 'p95', 'eager'])
            eager = next(r for r in rows if r['point'] == 'eager' and r['scenario'] == 'tournament')
            self.assertEqual(eager['owner_saving'], 1.0)
            self.assertAlmostEqual(eager['process_cpu_ratio'], round(1 + 0.1 * (eager['extra_cpu'] - 1), 4))
            report = json.loads(out.getvalue())
            self.assertEqual(set(report['in_sample_by_size']), {'64x64', '128x128', '256x256'})
            for metric in ('hit_rate', 'query_weighted_coverage', 'extra_cpu', 'owner_saving'):
                self.assertIn(metric, report['cross_validated'])
            self.assertIn('Key::Type', (base / 'p.h').read_text())


if __name__ == '__main__':
    unittest.main()
