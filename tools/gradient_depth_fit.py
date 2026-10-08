#!/usr/bin/env python3
"""Fit the building-field depth model from --telemetry gradient-stats games.

A building walking field is a lazy multi-source search: it settles cost layers
only as far as readers need. A scheduled build settles a predicted depth D on a
worker instead; readers past D still resolve the rest synchronously, so D moves
CPU and never changes a result. Each gradient-stats.csv row ends one field
lifetime, and its `prev_settled_cost` is the depth that lifetime actually needed.

The model is a quantile table. Lifetimes are grouped by selected keys (see
KEYS); D is the group's quantile of actual depth, optionally per tile of map
size (w + h). Groups thinner than MIN_GROUP back off to a shorter key prefix.

Subcommands, mirroring tools/win_probability_model.py:

* ``dataset``: tournament results or plain run directories -> dataset.json.gz;
* ``screen``: what each key buys alone and in forward selection, cross-validated
  with whole games held out;
* ``fit``: select keys, write the summary (tools/gradient_depth_model.json) and
  regenerate src/map/gradient/BuildingGradientDepthPolicy.h;
* ``evaluate``: the committed model's metrics, overall and per map size.

Metrics, over held-out lifetimes:

* hit rate: share with actual <= D;
* query-weighted coverage: the same, weighted by resolve calls;
* extra CPU: sum popped(max(D, actual)) / sum popped(actual);
* owner saving: sum popped(min(D, actual)) / sum popped(actual), the share of the
  lazy search that a scheduled build would move off the simulation owner.

popped(c) interpolates the 80-cost depth histogram. Beyond a partial lifetime's
reach, the mean profile of complete lifetimes of the same route and map size
estimates the work.

The pipeline needs only the standard library. Every emitted constant is an
integer.
"""
import argparse
import csv
import gzip
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'src/map/gradient/BuildingGradientDepthPolicy.h'
SUMMARY = ROOT / 'tools/gradient_depth_model.json'

BIN_COST = 80
BINS = 32
LAST_BIN_WIDTH = 8 * BIN_COST  # extrapolation width of the open-ended last bin
MIN_DEPTH = 32
MAX_DEPTH = 65535
MIN_GROUP = 40
BUCKET = 16  # target histogram resolution
PER_SIZE_SCALE = 256  # per-size targets are Q8 cost per tile of (w + h)
FOLDS = 5
ROUTES = ('footprint', 'clearing', 'combat')
CONSTRUCTION = ('none', 'new', 'upgrade', 'repair')
# Forward selection keeps adding keys while the held-out owner saving at the CPU
# budget rises by at least this much.
MIN_GAIN = 0.01
MAX_KEYS = 4


def log2_bucket(value):
    """floor(log2(value + 1)) for value >= 0, else -1; the C++ helper matches."""
    return -1 if value is None or value < 0 else int(value + 1).bit_length() - 1


# Candidate keys. Each maps a lifetime to the integer (or type string) the C++
# query computes from the same O(1) owner inputs.
KEYS = {
    'route': lambda l: l.route,
    'swim': lambda l: l.swim,
    'type': lambda l: l.type,
    'level': lambda l: l.level,
    'is_site': lambda l: l.site,
    'construction': lambda l: l.construction,
    'progress': lambda l: l.progress,
    'size': lambda l: l.w + l.h,
    'units': lambda l: log2_bucket(l.units),
    'buildings': lambda l: log2_bucket(l.buildings),
    'previous': lambda l: log2_bucket(None if l.previous is None else l.previous // BIN_COST),
}
CPP_KEY = {'route': 'Route', 'swim': 'Swim', 'type': 'Type', 'level': 'Level', 'is_site': 'Site',
           'construction': 'Construction', 'progress': 'Progress', 'size': 'Size', 'units': 'Units',
           'buildings': 'Buildings', 'previous': 'Previous'}


class Lifetime:
    __slots__ = ('game', 'route', 'swim', 'type', 'level', 'site', 'construction', 'progress', 'w', 'h',
                 'units', 'buildings', 'previous', 'actual', 'complete', 'popped', 'queries', 'bins', 'cum',
                 'prefix')
    DERIVED = ('cum', 'prefix')

    def __init__(self, **values):
        for key in self.__slots__:
            setattr(self, key, values.get(key))

    def record(self):
        return [getattr(self, k) for k in self.__slots__ if k not in self.DERIVED]

    @classmethod
    def from_record(cls, values):
        return cls(**dict(zip([k for k in cls.__slots__ if k not in cls.DERIVED], values)))


def integer(text, default=-1):
    return int(text) if text not in (None, '') else default


def read_csv(lines, game):
    """Lifetimes with a search, each linked to the same field's previous lifetime."""
    rows = sorted(csv.DictReader(lines), key=lambda r: int(r['tick']))
    previous = {}
    lifetimes = []
    for row in rows:
        key = (row['team'], row['gid'], row['route'], row['swim'])
        if row['event'] == 'rebuild' and row['reason'] == 'null':
            previous.pop(key, None)
        if row.get('prev_search') != '1' or row.get('prev_locked') == '1':
            if row['event'] != 'rebuild':
                previous.pop(key, None)
            continue
        actual = int(row['prev_settled_cost'])
        construction = row.get('construction_state') or ''
        lifetimes.append(Lifetime(
            game=game, route=ROUTES.index(row['route']), swim=int(row['swim']), type=row['type'],
            level=integer(row.get('level')), site=integer(row.get('is_site')),
            construction=CONSTRUCTION.index(construction) if construction in CONSTRUCTION else -1,
            progress=integer(row.get('progress')), w=integer(row.get('width'), 0), h=integer(row.get('height'), 0),
            units=integer(row.get('team_units')), buildings=integer(row.get('team_buildings')),
            previous=previous.get(key), actual=actual, complete=row['prev_complete'] == '1',
            popped=int(row['prev_popped']), queries=int(row['prev_queries']),
            bins=[int(row[f'popped_at_depth_{i}']) for i in range(BINS)]))
        if row['event'] == 'rebuild':
            previous[key] = actual
        else:
            previous.pop(key, None)
    return lifetimes


def sources(root):
    """(game id, labels, CSV line iterator) for a tournament or a directory of runs."""
    root = Path(root)
    if (root / 'experiment.json').exists():
        sys.path.insert(0, str(ROOT))
        from tools.tournaments.results import Results
        results = Results(root)
        for record in results:
            if record.get('category') != 'success' or not any(a['path'] == 'gradient-stats.csv'
                                                               for a in record['artifacts']):
                continue
            labels = dict(record['job'].get('labels', {}))
            labels['players'] = len(record['job']['config'].get('players', []))
            with results.open_artifact(record, 'gradient-stats.csv') as lines:
                yield record['job']['id'], labels, lines
    else:
        for path in sorted(root.glob('*/gradient-stats.csv')):
            result = path.parent / 'result.json'
            labels = {'run': path.parent.name}
            if result.exists():
                value = json.loads(result.read_text())
                labels.update(map=value.get('resolved', {}).get('map'), players=len(value.get('players', [])))
            with open(path, newline='') as lines:
                yield f'{root.name}/{path.parent.name}', labels, lines


def build_dataset(roots):
    games, lifetimes = {}, []
    for root in roots:
        for game, labels, lines in sources(root):
            games[game] = labels
            lifetimes.extend(read_csv(lines, game))
    return {'version': 1, 'games': games, 'fields': [k for k in Lifetime.__slots__ if k not in Lifetime.DERIVED],
            'lifetimes': [l.record() for l in lifetimes]}


def load_dataset(path):
    with gzip.open(path, 'rt') as source:
        data = json.load(source)
    lifetimes = [Lifetime.from_record(values) for values in data['lifetimes']]
    prepare(lifetimes)
    return data['games'], lifetimes


# Work integration -----------------------------------------------------------

def edges():
    return [i * BIN_COST for i in range(BINS)]


def profiles(lifetimes):
    """Mean popped per bin of complete lifetimes, by (route, size) and by route."""
    sums, counts = defaultdict(lambda: [0.0] * BINS), Counter()
    for life in lifetimes:
        if life.complete:
            for key in ((life.route, life.w + life.h), (life.route,), ()):
                counts[key] += 1
                values = sums[key]
                for i, v in enumerate(life.bins):
                    values[i] += v
    result = {}
    for key, values in sums.items():
        cumulative, total = [0.0], 0.0
        for v in values:
            total += v / counts[key]
            cumulative.append(total)
        result[key] = cumulative
    return result


def profile_at(cumulative, cost):
    i = min(cost // BIN_COST, BINS - 1)
    width = BIN_COST if i < BINS - 1 else LAST_BIN_WIDTH
    fraction = min(1.0, (cost - i * BIN_COST) / width)
    return cumulative[i] + (cumulative[i + 1] - cumulative[i]) * fraction


def prepare(lifetimes):
    table = profiles(lifetimes)
    for life in lifetimes:
        life.cum = table.get((life.route, life.w + life.h)) or table.get((life.route,)) or table.get(())
        running, life.prefix = 0, [0]
        for v in life.bins:
            running += v
            life.prefix.append(running)


def popped_to(life, cost):
    """Entries popped settling every cost below `cost`."""
    if cost >= life.actual:
        if life.complete or cost == life.actual or not life.cum:
            return float(life.popped)
        return life.popped + max(0.0, profile_at(life.cum, cost) - profile_at(life.cum, life.actual))
    i = min(cost // BIN_COST, BINS - 1)
    low = i * BIN_COST
    high = min((i + 1) * BIN_COST if i < BINS - 1 else life.actual, life.actual)
    below = life.prefix[i] if life.prefix else sum(life.bins[:i])
    span = high - low
    return below + (life.bins[i] * (cost - low) / span if span > 0 else 0.0)


def metrics(pairs):
    """pairs: (lifetime, predicted depth)."""
    hits = queries = covered = 0
    actual_work = cpu_work = saved_work = 0.0
    for life, depth in pairs:
        hit = life.actual <= depth
        hits += hit
        queries += life.queries
        covered += life.queries if hit else 0
        actual_work += life.popped
        cpu_work += popped_to(life, max(depth, life.actual))
        saved_work += popped_to(life, min(depth, life.actual))
    count = len(pairs)
    return {'lifetimes': count, 'hit_rate': round(hits / count, 4) if count else 0.0,
            'query_weighted_coverage': round(covered / queries, 4) if queries else 0.0,
            'extra_cpu': round(cpu_work / actual_work, 4) if actual_work else 1.0,
            'owner_saving': round(saved_work / actual_work, 4) if actual_work else 0.0}


# Model ----------------------------------------------------------------------

def target(life, per_size):
    if not per_size:
        return life.actual
    return life.actual * PER_SIZE_SCALE // max(1, life.w + life.h)


def depth_of(value, life, per_size):
    if per_size:
        value = value * (life.w + life.h) // PER_SIZE_SCALE
    return max(MIN_DEPTH, min(MAX_DEPTH, value))


def histograms(lifetimes, keys, per_size):
    """Target histograms for every prefix cell of the selected keys."""
    cells = defaultdict(Counter)
    for life in lifetimes:
        bucket = min(target(life, per_size), MAX_DEPTH) // BUCKET
        values = tuple(KEYS[k](life) for k in keys)
        for n in range(len(keys) + 1):
            cells[values[:n]][bucket] += 1
    return cells


def quantile_value(histogram, q):
    total = sum(histogram.values())
    need = max(1, math.ceil(q * total))
    running = 0
    for bucket in sorted(histogram):
        running += histogram[bucket]
        if running >= need:
            return (bucket + 1) * BUCKET
    return MAX_DEPTH


def table_from(cells, q, minimum=MIN_GROUP):
    """Cells with enough evidence, each its quantile; the empty prefix always."""
    table = {}
    for cell, histogram in cells.items():
        if sum(histogram.values()) >= minimum or not cell:
            table[cell] = quantile_value(histogram, q)
    return table


def predict(table, keys, per_size, life):
    values = tuple(KEYS[k](life) for k in keys)
    for n in range(len(keys), -1, -1):
        value = table.get(values[:n])
        if value is not None:
            return depth_of(value, life, per_size)
    return MAX_DEPTH


def fold_of(game, folds):
    return int(hashlib.sha1(game.encode()).hexdigest(), 16) % folds


QUANTILES = (0.3, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95)
DEFAULT_BUDGET = 1.25


def cross_validate(lifetimes, keys, per_size, quantiles, folds=FOLDS, by=None):
    """Held-out metrics for each quantile, with whole games held out together."""
    single = isinstance(quantiles, float)
    quantiles = [quantiles] if single else list(quantiles)
    games = sorted({l.game for l in lifetimes})
    folds = max(2, min(folds, len(games)))
    assignment = {g: fold_of(g, folds) for g in games}
    pairs = {q: [] for q in quantiles}
    for fold in range(folds):
        training = [l for l in lifetimes if assignment[l.game] != fold]
        held = [l for l in lifetimes if assignment[l.game] == fold]
        if not training or not held:
            continue
        cells = histograms(training, keys, per_size)
        for q in quantiles:
            table = table_from(cells, q)
            pairs[q].extend((l, predict(table, keys, per_size, l)) for l in held)
    results = {}
    for q in quantiles:
        result = metrics(pairs[q])
        result['quantile'] = q
        if by:
            groups = defaultdict(list)
            for life, depth in pairs[q]:
                groups[by(life)].append((life, depth))
            result['by'] = {str(k): metrics(v) for k, v in sorted(groups.items())}
        results[q] = result
    return results[quantiles[0]] if single else results


def under_budget(curve, budget):
    """The quantile with the largest owner saving whose extra CPU fits the budget.

    Score = that saving, linearly interpolated to the budget between the
    neighbouring quantiles, so key sets are compared at equal CPU cost."""
    points = sorted(curve.values(), key=lambda m: m['quantile'])
    best, score = None, 0.0
    for i, point in enumerate(points):
        if point['extra_cpu'] <= budget:
            best, score = point, point['owner_saving']
            nxt = points[i + 1] if i + 1 < len(points) else None
            if nxt and nxt['extra_cpu'] > budget > point['extra_cpu']:
                f = (budget - point['extra_cpu']) / (nxt['extra_cpu'] - point['extra_cpu'])
                score = point['owner_saving'] + f * (nxt['owner_saving'] - point['owner_saving'])
    return {'score': round(score, 4), 'quantile': best['quantile'] if best else None,
            'at_quantile': {k: v for k, v in (best or {}).items() if k != 'by'}}


_SHARED = {}


def _evaluate(task):
    keys, per_size = task
    return under_budget(cross_validate(_SHARED['lifetimes'], keys, per_size, QUANTILES), _SHARED['budget'])


def evaluate_all(lifetimes, budget, tasks, jobs):
    """Score (keys, per_size) tasks, in parallel where fork is available."""
    _SHARED.update(lifetimes=lifetimes, budget=budget)
    if jobs > 1 and len(tasks) > 1:
        import multiprocessing
        try:
            context = multiprocessing.get_context('fork')
        except ValueError:
            context = None
        if context:
            with context.Pool(min(jobs, len(tasks))) as pool:
                return pool.map(_evaluate, tasks)
    return [_evaluate(t) for t in tasks]


def screen(lifetimes, budget=DEFAULT_BUDGET, candidates=None, max_keys=MAX_KEYS, jobs=1):
    """Owner saving at the CPU budget for each key alone and in forward selection."""
    candidates = list(candidates or KEYS)
    report = {'budget': budget, 'baseline': {}, 'alone': {}, 'selection': []}
    tasks = [(keys, per_size) for per_size in (False, True) for keys in [[]] + [[k] for k in candidates]]
    scores = dict(zip(((tuple(k), p) for k, p in tasks), evaluate_all(lifetimes, budget, tasks, jobs)))
    for per_size in (False, True):
        name = 'per_size' if per_size else 'raw'
        report['baseline'][name] = scores[((), per_size)]
        report['alone'][name] = {k: scores[((k,), per_size)] for k in candidates}
    per_size = report['baseline']['per_size']['score'] > report['baseline']['raw']['score']
    selected, current = [], report['baseline']['per_size' if per_size else 'raw']
    report['selection'].append({'keys': [], 'per_size': per_size, **current})
    while len(selected) < max_keys:
        options = [(selected + [key], normalise) for key in candidates if key not in selected
                   for normalise in ((per_size,) if selected else (False, True))]
        if not options:
            break
        trials = [(score, keys[-1], normalise) for score, (keys, normalise)
                  in zip(evaluate_all(lifetimes, budget, options, jobs), options)]
        score, key, normalise = max(trials, key=lambda t: t[0]['score'])
        if score['score'] - current['score'] < MIN_GAIN:
            report['rejected_next'] = {'key': key, 'per_size': normalise, **score}
            break
        selected.append(key)
        per_size, current = normalise, score
        report['selection'].append({'keys': list(selected), 'per_size': per_size, **score})
    report['selected'] = {'keys': selected, 'per_size': per_size, 'quantile': current['quantile']}
    return report


# Summary and header ---------------------------------------------------------

def summarise(lifetimes, games, keys, per_size, q):
    """Everything fit needs to regenerate the header, without the raw lifetimes:
    each prefix cell's lifetime count and its target at every grid quantile."""
    cells = histograms(lifetimes, keys, per_size)
    ladder = sorted(set(QUANTILES) | {q})
    return {'version': 1, 'keys': keys, 'per_size': per_size, 'quantile': q, 'min_group': MIN_GROUP,
            'bucket': BUCKET, 'games': len(games), 'lifetimes': len(lifetimes),
            'cells': [{'cell': list(cell), 'count': sum(h.values()),
                       'quantiles': {str(level): quantile_value(h, level) for level in ladder}}
                      for cell, h in sorted(cells.items(), key=lambda item: (len(item[0]), [str(v) for v in item[0]]))]}


def table_from_summary(summary):
    level = str(summary['quantile'])
    return {tuple(entry['cell']): entry['quantiles'][level] for entry in summary['cells']
            if entry['count'] >= summary['min_group'] or not entry['cell']}


def cpp_value(key, value):
    return f'"{value}"' if key == 'type' else str(int(value))


def emit_header(summary, provenance=()):
    keys, per_size = summary['keys'], summary['per_size']
    table = table_from_summary(summary)
    rules = sorted(table.items(), key=lambda item: (-len(item[0]), [str(v) for v in item[0]]))
    width = max(1, len(keys))
    lines = [
        '// SPDX-License-Identifier: GPL-3.0-or-later',
        '// Generated by tools/gradient_depth_fit.py -- do not edit by hand.',
        '//',
        '// Predicted settle depth for a building walking field (see',
        '// docs/building-gradient-depth-model.md). The depth only moves worker CPU:',
        '// readers still resolve unsettled cells synchronously, so a different table',
        '// never changes a simulation result and needs no SIM_REVISION bump.',
        '//',
        *[f'// {line}' for line in provenance],
        '#pragma once',
        '',
        '#include <string_view>',
        '',
        'namespace BuildingGradientDepth',
        '{',
        '/// The owner inputs a prediction may read, each O(1) at rebuild time.',
        'struct Query',
        '{',
        '\tint route = 0; // 0 footprint, 1 clearing, 2 combat',
        '\tint swim = 0;',
        '\tstd::string_view type; // BuildingType::type',
        '\tint level = 0, site = 0;',
        '\tint construction = 0; // BuildingStateRecord::ConstructionResultState',
        '\tint progress = -1; // delivered/needed material quartile on sites, else -1',
        '\tint width = 0, height = 0; // map tiles',
        '\tint units = 0, buildings = 0; // the team\'s live counts',
        '\tint previous = -1; // the field\'s previous settled cost, -1 if none',
        '};',
        '',
        'enum class Key { Route, Swim, Type, Level, Site, Construction, Progress, Size, Units, Buildings, Previous };',
        '',
        '/// floor(log2(value + 1)) for value >= 0, else -1.',
        'constexpr int log2Bucket(int value)',
        '{',
        '\tif (value < 0)',
        '\t\treturn -1;',
        '\tint bucket = 0;',
        '\tfor (unsigned v = unsigned(value) + 1; v > 1; v >>= 1)',
        '\t\t++bucket;',
        '\treturn bucket;',
        '}',
        '',
        'constexpr int keyValue(Key key, const Query &q)',
        '{',
        '\tswitch (key)',
        '\t{',
        '\tcase Key::Route: return q.route;',
        '\tcase Key::Swim: return q.swim;',
        '\tcase Key::Level: return q.level;',
        '\tcase Key::Site: return q.site;',
        '\tcase Key::Construction: return q.construction;',
        '\tcase Key::Progress: return q.progress;',
        '\tcase Key::Size: return q.width + q.height;',
        '\tcase Key::Units: return log2Bucket(q.units);',
        '\tcase Key::Buildings: return log2Bucket(q.buildings);',
        f'\tcase Key::Previous: return q.previous < 0 ? -1 : log2Bucket(q.previous / {BIN_COST});',
        '\tdefault: return 0;',
        '\t}',
        '}',
        '',
        f'inline constexpr int MIN_DEPTH = {MIN_DEPTH};',
        f'inline constexpr int MAX_DEPTH = {MAX_DEPTH};',
        '/// When true, a rule\'s value is Q8 cost per tile of (width + height).',
        f'inline constexpr bool PER_SIZE = {"true" if per_size else "false"};',
        f'inline constexpr int PER_SIZE_SCALE = {PER_SIZE_SCALE};',
        f'inline constexpr int KEY_COUNT = {len(keys)};',
        f'inline constexpr Key KEYS[{width}] = {{{", ".join("Key::" + CPP_KEY[k] for k in keys) or "Key::Route"}}};',
        '',
        '/// Matches when the first `matched` selected keys equal `values` (and `type`',
        '/// when Type is among them). Rules are most specific first; the last one',
        '/// matches everything.',
        'struct Rule',
        '{',
        '\tint matched;',
        f'\tint values[{width}];',
        '\tstd::string_view type;',
        '\tint value;',
        '};',
        '',
        'inline constexpr Rule RULES[] = {',
    ]
    for cell, value in rules:
        ints = [0 if k == 'type' else int(v) for k, v in zip(keys, cell)] + [0] * (width - len(cell))
        kind = next((v for k, v in zip(keys, cell) if k == 'type'), '')
        lines.append(f'\t{{{len(cell)}, {{{", ".join(map(str, ints))}}}, "{kind}", {value}}},')
    lines += [
        '};',
        '',
        'constexpr bool matches(const Rule &rule, const Query &q)',
        '{',
        '\tfor (int i = 0; i < rule.matched; ++i)',
        '\t{',
        '\t\tif (KEYS[i] == Key::Type ? rule.type != q.type : rule.values[i] != keyValue(KEYS[i], q))',
        '\t\t\treturn false;',
        '\t}',
        '\treturn true;',
        '}',
        '',
        '/// The cost to settle up front. Never affects results, only who pays.',
        'constexpr int target(const Query &q)',
        '{',
        '\tfor (const Rule &rule : RULES)',
        '\t\tif (matches(rule, q))',
        '\t\t{',
        '\t\t\tlong long depth = rule.value;',
        '\t\t\tif (PER_SIZE)',
        '\t\t\t\tdepth = depth * (q.width + q.height) / PER_SIZE_SCALE;',
        '\t\t\treturn int(depth < MIN_DEPTH ? MIN_DEPTH : depth > MAX_DEPTH ? MAX_DEPTH : depth);',
        '\t\t}',
        '\treturn MAX_DEPTH;',
        '}',
        '} // namespace BuildingGradientDepth',
    ]
    return '\n'.join(lines) + '\n'


def provenance(summary, report=None):
    lines = [f'Fitted on {summary["games"]} games, {summary["lifetimes"]} field lifetimes;',
             f'keys {", ".join(summary["keys"]) or "(none)"}, per_size {summary["per_size"]}, '
             f'quantile {summary["quantile"]}.']
    if report:
        lines.append(f'Held out by game: hit rate {report["hit_rate"]}, coverage {report["query_weighted_coverage"]}, '
                     f'extra CPU {report["extra_cpu"]}, owner saving {report["owner_saving"]}.')
    return lines


def by_size(life):
    return f'{life.w}x{life.h}'


# Commands -------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('dataset', help='tournament results or run directories -> dataset')
    p.add_argument('roots', nargs='+', type=Path)
    p.add_argument('--output', type=Path, required=True)
    for name in ('screen', 'fit', 'evaluate'):
        p = sub.add_parser(name)
        p.add_argument('dataset', type=Path)
        p.add_argument('--report', type=Path)
        if name in ('screen', 'fit'):
            p.add_argument('--jobs', type=int, default=1, help='parallel candidate evaluations')
            p.add_argument('--budget', type=float, default=DEFAULT_BUDGET,
                           help='extra-CPU ratio the owner saving is compared at')
            p.add_argument('--keys', nargs='*', choices=sorted(KEYS),
                           help='screen: candidates; fit: skip selection and use these keys, in order')
        if name == 'fit':
            p.add_argument('--per-size', action='store_true')
            p.add_argument('--quantile', type=float, help='with --keys: the quantile (default: best under budget)')
            p.add_argument('--header', type=Path, default=HEADER)
            p.add_argument('--summary', type=Path, default=SUMMARY)
        if name == 'evaluate':
            p.add_argument('--summary', type=Path, default=SUMMARY)
    args = parser.parse_args(argv)

    if args.command == 'dataset':
        data = build_dataset(args.roots)
        with gzip.open(args.output, 'wt') as out:
            json.dump(data, out, separators=(',', ':'))
        print(f'{len(data["games"])} games, {len(data["lifetimes"])} lifetimes -> {args.output}')
        return 0

    games, lifetimes = load_dataset(args.dataset)

    def line(label, m):
        a = m['at_quantile']
        return (f'{label:34s} saving@budget={m["score"]:.3f} q={m["quantile"]} hit={a.get("hit_rate", 0):.3f} '
                f'cov={a.get("query_weighted_coverage", 0):.3f} extra={a.get("extra_cpu", 0):.3f}')
    if args.command == 'screen':
        report = screen(lifetimes, args.budget, args.keys, jobs=args.jobs)
        for name in ('raw', 'per_size'):
            print(line(f'baseline {name}', report['baseline'][name]))
            for key, m in sorted(report['alone'][name].items(), key=lambda item: -item[1]['score']):
                print(line(f'  alone {name} {key}', m))
        for step in report['selection']:
            print(line(f'select {"+".join(step["keys"]) or "-"} per_size={step["per_size"]}', step))
        if 'rejected_next' in report:
            print(line(f'stop: next {report["rejected_next"]["key"]}', report['rejected_next']))
    elif args.command == 'fit':
        if args.keys is None:
            selection = screen(lifetimes, args.budget, jobs=args.jobs)['selected']
            keys, per_size, quantile = selection['keys'], selection['per_size'], selection['quantile']
        else:
            keys, per_size = args.keys, args.per_size
            quantile = args.quantile or under_budget(
                cross_validate(lifetimes, keys, per_size, QUANTILES), args.budget)['quantile']
        if quantile is None:
            raise SystemExit('no quantile meets the CPU budget')
        held_out = cross_validate(lifetimes, keys, per_size, quantile, by=by_size)
        summary = summarise(lifetimes, games, keys, per_size, quantile)
        summary['budget'] = args.budget
        summary['cross_validated'] = held_out
        args.summary.write_text(json.dumps(summary, indent=1) + '\n')
        args.header.write_text(emit_header(summary, provenance(summary, held_out)))
        report = {'keys': keys, 'per_size': per_size, 'quantile': quantile, 'cross_validated': held_out}
        print(json.dumps(report, indent=2))
    else:
        summary = json.loads(args.summary.read_text())
        table = table_from_summary(summary)
        keys, per_size = summary['keys'], summary['per_size']
        pairs = [(l, predict(table, keys, per_size, l)) for l in lifetimes]
        groups = defaultdict(list)
        for pair in pairs:
            groups[by_size(pair[0])].append(pair)
        held_out = cross_validate(lifetimes, keys, per_size, summary['quantile'], by=by_size)
        report = {'in_sample': metrics(pairs), 'in_sample_by_size': {k: metrics(v) for k, v in sorted(groups.items())},
                  'cross_validated': held_out}
        print(json.dumps(report, indent=2))
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
