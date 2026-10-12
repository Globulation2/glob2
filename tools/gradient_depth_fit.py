#!/usr/bin/env python3
"""Fit the building-field depth model from --telemetry gradient-stats games.

A building walking field is a lazy multi-source search: it settles cost layers
only as far as readers need. A scheduled refresh settles a predicted depth D on
a worker instead; readers past D still resolve the rest synchronously, so D
moves CPU and never changes a result. Each gradient-stats.csv row ends one field
lifetime, and its `prev_settled_cost` is the depth that lifetime actually needed.

The model predicts a field's next depth from its own past depths, read when the
refresh is staged: how deep readers of the field still serving required it
(`serving`), and the final reader-required depth of the lifetime it replaced (`hint`,
settledCostHint). With m the deeper of the two,

    D = offset + (slope256 * m) >> 8,  clamped to [MIN_DEPTH, MAX_DEPTH].

A field with neither depth (in practice, only after a save is loaded, since the
past depths are not saved) settles MIN_DEPTH: its readers decide, and it has a
history from its next refresh. Each operating point has its own two integers,
chosen to maximise owner saving minus lambda times worker search CPU over the
training lifetimes: one lambda for every field, so worker CPU goes where it
moves the most owner work. The engine evaluates the same integer expression
(BuildingGradientDepthPolicy.h).

Subcommands:

* ``dataset``: tournament results or plain run directories -> dataset.npz;
* ``fit``: write the summary (tools/gradient_depth_model.json) and regenerate
  src/map/gradient/BuildingGradientDepthPolicy.h;
* ``evaluate``: the committed model's metrics, overall and per map size, and
  with ``--curve`` owner saving against CPU per lambda.

Metrics, over held-out games (5 folds):

* hit rate: share of lifetimes with actual <= D;
* query-weighted coverage: the same, weighted by resolve calls;
* extra CPU: sum popped(max(D, actual)) / sum popped(actual);
* owner saving: sum popped(min(D, actual)) / sum popped(actual), the share of the
  lazy search that a scheduled build moves off the simulation owner.

popped(c) integrates the 80-cost depth histogram. Beyond a partial lifetime's
reach, the mean profile of complete lifetimes of the same map size estimates
the work. Depths are scored on a 16-cost grid up to GRID_LIMIT, beyond which a
depth counts as eager; fitting groups lifetimes by m on the same 16-cost grid.

Only scheduled footprint builds use the model (flag routes settle fully and
cold fields build synchronously), so only those lifetimes are read.

The dataset, fit and evaluate commands need numpy. Regenerating the header from
the committed summary (emit_header) needs only the standard library, and every
emitted constant is an integer.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'src/map/gradient/BuildingGradientDepthPolicy.h'
SUMMARY = ROOT / 'tools/gradient_depth_model.json'

BIN_COST = 80
BINS = 32
LAST_BIN_WIDTH = 8 * BIN_COST  # extrapolation width of the open-ended last bin
MIN_DEPTH = 32
MAX_DEPTH = 65535
STEP = 16  # depth grid resolution
GRID_LIMIT = 4096  # deepest finite grid depth; anything deeper is eager
GRID = GRID_LIMIT // STEP
EAGER = GRID + 1  # grid index of MAX_DEPTH
SIZE = GRID + 2
MIN_INDEX = MIN_DEPTH // STEP
FOLDS = 5
LAMBDAS = (0.02, 0.05, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.7, 1.0, 1.5, 2.0, 3.0)
DEFAULT_BUDGET = 1.25
# Coarse search grid for (offset, slope256), refined around each lambda's best.
OFFSETS = range(-200, 801, 40)
SLOPES = range(128, 1281, 16)
REFINE = ((8, 4), (2, 1))  # (offset step, slope step), five steps either side
# The formula depends on a lifetime only through m, so lifetimes are grouped by
# m on the 16-cost grid: group g holds m in [16g, 16g + 16) and is scored at
# its centre. Past 2 * GRID_LIMIT every slope on the search grid is eager, so
# deeper m share the last group; the extra group LAST_GROUP + 1 holds lifetimes
# without history.
LAST_GROUP = 2 * GRID_LIMIT // STEP
UNKNOWN = LAST_GROUP + 1
GROUPS = UNKNOWN + 1
# Search (gradient.building_resume) share of process CPU with greedy fetching,
# measured with the flag off (docs/ai/architecture/building-gradient-depth-model.md). Search work
# scales with popped entries, so a field extra-CPU ratio r costs about
# 1 + share * (r - 1) of process CPU.
SEARCH_SHARE = {'busy_oazis': 0.026, 'tournament': 0.007, 'all': 0.026}

# The per-lifetime columns a dataset holds; serving and hint are -1 when unknown.
COLUMNS = ('game', 'w', 'h', 'actual', 'complete', 'popped', 'queries', 'serving', 'hint')


def depth(point, m):
    """The engine's integer formula (BuildingGradientDepth::target) for one m."""
    if m is None or m < 0:
        return MIN_DEPTH
    return max(MIN_DEPTH, min(MAX_DEPTH, point['offset'] + ((point['slope256'] * m) >> 8)))


def grid_depth(k):
    return MAX_DEPTH if k >= EAGER else max(MIN_DEPTH, k * STEP)


def grid_index(d):
    return EAGER if d > GRID_LIMIT else max(MIN_INDEX, -(-d // STEP))


def fold_of(game, folds):
    return int(hashlib.sha1(game.encode()).hexdigest(), 16) % folds


def np():
    import numpy
    return numpy


# Dataset ----------------------------------------------------------------------

def parse_csv(text):
    """Scheduled footprint lifetimes with a search, as rows of ints:
    (w, h, actual, complete, popped, queries, serving, hint, bin 0..31)."""
    lines = text.splitlines()
    if not lines:
        return []
    index = {name: i for i, name in enumerate(lines[0].split(','))}
    route, staged = index['route'], index['staged']
    search, locked, complete = index['prev_search'], index['prev_locked'], index['prev_complete']
    numbers = [index[c] for c in ('width', 'height', 'prev_settled_cost')]
    popped, queries = index['prev_popped'], index['prev_queries']
    serving, hint = index['serving_settled'], index['previous_hint']
    first_bin = index['popped_at_depth_0']
    rows = []
    for line in lines[1:]:
        if ',footprint,' not in line:
            continue
        f = line.split(',')
        if f[route] != 'footprint' or f[staged] != '1' or f[search] != '1' or f[locked] == '1':
            continue
        rows.append([int(f[numbers[0]]), int(f[numbers[1]]), int(f[numbers[2]]), int(f[complete] == '1'),
                     int(f[popped]), int(f[queries]), int(f[serving] or -1), int(f[hint] or -1),
                     *map(int, f[first_bin:first_bin + BINS])])
    return rows


def sources(root):
    """(game id, labels, CSV text) for a tournament or a directory of runs."""
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
                yield record['job']['id'], labels, ''.join(lines)
    else:
        for path in sorted(root.glob('*/gradient-stats.csv')):
            result = path.parent / 'result.json'
            labels = {'run': path.parent.name}
            if result.exists():
                value = json.loads(result.read_text())
                labels.update(map=value.get('resolved', {}).get('map'), players=len(value.get('players', [])))
            yield f'{root.name}/{path.parent.name}', labels, path.read_text()


def _parse(item):
    game, labels, text = item
    return game, labels, parse_csv(text)


def build_dataset(roots, jobs=1):
    """{column: array} for every lifetime plus `bins` (n, BINS), `games` and `labels`."""
    numpy = np()
    games, labels, parts, owners = [], [], [], []
    items = (item for root in roots for item in sources(root))
    if jobs > 1:
        import multiprocessing
        pool = multiprocessing.get_context('fork').Pool(jobs)
        parsed = pool.imap(_parse, items, chunksize=4)
    else:
        pool, parsed = None, map(_parse, items)
    try:
        for game, game_labels, rows in parsed:
            index = len(games)
            games.append(game)
            labels.append(game_labels)
            if rows:
                parts.append(numpy.asarray(rows, dtype=numpy.int64))
                owners.append(numpy.full(len(rows), index, dtype=numpy.int32))
    finally:
        if pool:
            pool.close()
    table = numpy.concatenate(parts) if parts else numpy.zeros((0, 8 + BINS), dtype=numpy.int64)
    data = {'game': numpy.concatenate(owners) if owners else numpy.zeros(0, dtype=numpy.int32)}
    for i, name in enumerate(('w', 'h', 'actual', 'complete', 'popped', 'queries', 'serving', 'hint')):
        data[name] = table[:, i]
    data['complete'] = data['complete'].astype(bool)
    data['bins'] = table[:, 8:].astype(numpy.int64)
    data['games'] = numpy.array(games, dtype=str)
    data['labels'] = numpy.array([json.dumps(l, sort_keys=True) for l in labels], dtype=str)
    return data


def save_dataset(data, path):
    np().savez_compressed(path, version=4, **data)


def load_dataset(path):
    numpy = np()
    with numpy.load(path) as stored:
        if int(stored.get('version', 0)) != 4:
            raise SystemExit(f'{path}: not a version 4 dataset; rebuild it with `dataset`')
        data = {k: stored[k] for k in stored.files if k != 'version'}
    data['games'] = [str(g) for g in data['games']]
    data['labels'] = [json.loads(str(l)) for l in data['labels']]
    return prepare(data)


def from_lifetimes(lifetimes, games=None):
    """A prepared dataset from dicts with the COLUMNS (game as a name) and bins."""
    numpy = np()
    names = sorted({l['game'] for l in lifetimes}) if games is None else list(games)
    position = {g: i for i, g in enumerate(names)}
    data = {'game': numpy.array([position[l['game']] for l in lifetimes], dtype=numpy.int32)}
    for c in COLUMNS[1:]:
        data[c] = numpy.array([-1 if l.get(c) is None else l[c] for l in lifetimes], dtype=numpy.int64)
    data['complete'] = numpy.array([bool(l.get('complete')) for l in lifetimes], dtype=bool)
    data['bins'] = numpy.array([l['bins'] for l in lifetimes], dtype=numpy.int64).reshape(-1, BINS)
    data['games'] = names
    data['labels'] = [{} for _ in names]
    return prepare(data)


# Work curves -----------------------------------------------------------------
#
# Every metric is a sum over lifetimes of a function of D, so lifetimes are
# reduced to per-cell arrays over the depth grid (D_k = STEP * k):
#   hits(k)  = #{actual <= D_k}, and coverage the same weighted by queries;
#   saved(k) = popped work below D_k: each 80-cost band's mass spread evenly over
#              the grid steps it covers, then integrated;
#   cpu(k)   = work + the sum over partial lifetimes with actual < D_k of
#              profile(D_k) - profile(actual), per extrapolation profile.

def profile_at(cumulative, cost):
    """Interpolated cumulative mean popped: every profile row of `cumulative`
    (profiles, BINS + 1) at every cost, or, with `cost` one per row, each row
    at its own cost."""
    numpy = np()
    i = numpy.minimum(cost // BIN_COST, BINS - 1)
    width = numpy.where(i < BINS - 1, BIN_COST, LAST_BIN_WIDTH)
    fraction = numpy.minimum(1.0, (cost - i * BIN_COST) / width)
    if cumulative.ndim == 2 and numpy.ndim(cost) == 1 and len(cost) == len(cumulative):
        rows = numpy.arange(len(cost))
        low, high = cumulative[rows, i], cumulative[rows, i + 1]
    else:
        low, high = cumulative[..., i], cumulative[..., i + 1]
    return low + (high - low) * fraction


def prepare(data):
    """Adds the derived per-lifetime arrays the curves and the fit read."""
    numpy = np()
    a = data['actual']
    serving, hint = data['serving'], data['hint']
    data['m'] = numpy.maximum(serving, hint)  # -1 when neither is known
    data['group'] = numpy.where(data['m'] < 0, UNKNOWN, numpy.minimum(data['m'] // STEP, LAST_GROUP))
    data['hit'] = numpy.where(a > GRID_LIMIT, EAGER, numpy.maximum(MIN_INDEX, -(-a // STEP)))
    # Extrapolation profiles: the mean band profile of complete lifetimes per map
    # size, falling back to all complete lifetimes.
    size = data['w'] + data['h']
    complete = data['complete']
    sizes = sorted(set(size[complete].tolist()))
    cumulative, profile = [], numpy.full(len(a), -1, dtype=numpy.int64)
    for key in sizes + [None]:
        members = complete if key is None else complete & (size == key)
        if not members.any():
            continue
        mean = data['bins'][members].mean(axis=0)
        cumulative.append(numpy.concatenate([[0.0], numpy.cumsum(mean)]))
        target = (profile < 0) if key is None else (size == key)
        profile[target & ~complete] = len(cumulative) - 1
    data['profiles'] = numpy.array(cumulative).reshape(-1, BINS + 1)
    data['profile'] = numpy.where(complete, -1, profile)
    grid = numpy.array([grid_depth(k) for k in range(SIZE)])
    data['profile_grid'] = (numpy.stack([profile_at(row, grid) for row in data['profiles']])
                            if len(cumulative) else numpy.zeros((0, SIZE)))
    partial = data['profile'] >= 0
    data['extra_start'] = numpy.where(a >= GRID_LIMIT, EAGER, a // STEP + 1)
    at = numpy.zeros(len(a))
    if partial.any():
        at[partial] = profile_at(data['profiles'][data['profile'][partial]], a[partial])
    data['extra_at'] = at
    return data


def cell_curves(data, cell, cells, members=None):
    """Per-cell grid arrays for the lifetimes `members` (all by default), each
    assigned to cell[i] in range(cells): n, work, queries, hits, covered,
    saved and cpu, the last four shaped (cells, SIZE)."""
    numpy = np()
    if members is not None:
        cell = cell[members]
    pick = (lambda x: x) if members is None else (lambda x: x[members])
    a, bins = pick(data['actual']), pick(data['bins']).astype(float)
    popped, queries = pick(data['popped']).astype(float), pick(data['queries']).astype(float)
    n = numpy.bincount(cell, minlength=cells).astype(float)
    work = numpy.bincount(cell, popped, cells)
    total_queries = numpy.bincount(cell, queries, cells)
    hit = cell * SIZE + pick(data['hit'])
    hits = numpy.bincount(hit, minlength=cells * SIZE).reshape(cells, SIZE).cumsum(1).astype(float)
    covered = numpy.bincount(hit, queries, cells * SIZE).reshape(cells, SIZE).cumsum(1)
    # saved: band i spans [80 i, min(80 (i + 1), actual)), the last band up to
    # actual; its mass is spread evenly over the grid steps from its start to
    # its rounded end.
    i = numpy.arange(BINS)
    low = i * BIN_COST
    high = numpy.minimum(numpy.where(i < BINS - 1, (i + 1) * BIN_COST, a[:, None]), a[:, None])
    start = numpy.broadcast_to(low // STEP, bins.shape)
    end = numpy.minimum(SIZE - 1, numpy.maximum(start + 1, numpy.round(high / STEP).astype(numpy.int64)))
    nonzero = bins > 0
    per_step = (bins / (end - start))[nonzero]
    base = (cell * SIZE)[:, None]
    slope = (numpy.bincount((base + start)[nonzero], per_step, cells * SIZE)
             - numpy.bincount((base + end)[nonzero], per_step, cells * SIZE)).reshape(cells, SIZE).cumsum(1)
    saved = numpy.zeros((cells, SIZE))
    saved[:, 1:GRID + 1] = slope[:, :GRID].cumsum(1)
    saved[:, EAGER] = work
    saved = numpy.minimum(saved, work[:, None])
    # cpu: work, plus the extrapolated search past each partial lifetime's reach.
    cpu = numpy.repeat(work[:, None], SIZE, axis=1)
    profile = pick(data['profile'])
    partial = profile >= 0
    profiles = len(data['profile_grid'])
    if partial.any():
        index = (cell[partial] * profiles + profile[partial]) * SIZE + pick(data['extra_start'])[partial]
        count = numpy.bincount(index, minlength=cells * profiles * SIZE).reshape(cells, profiles, SIZE).cumsum(2)
        total = numpy.bincount(index, pick(data['extra_at'])[partial],
                               cells * profiles * SIZE).reshape(cells, profiles, SIZE).cumsum(2)
        cpu += (count * data['profile_grid'][None, :, :] - total).sum(axis=1)
    return {'n': n, 'work': work, 'queries': total_queries, 'hits': hits, 'covered': covered, 'saved': saved,
            'cpu': cpu}


def subtract(whole, part):
    return {k: whole[k] - part[k] for k in whole}


def read(curves, index):
    """Totals over cells of hits, covered, saved and cpu at each cell's grid index."""
    numpy = np()
    rows = numpy.arange(len(index))
    return {'n': curves['n'].sum(), 'work': curves['work'].sum(), 'queries': curves['queries'].sum(),
            **{k: curves[k][rows, index].sum() for k in ('hits', 'covered', 'saved', 'cpu')}}


def metric_values(t):
    return {'lifetimes': int(round(t['n'])), 'hit_rate': round(t['hits'] / t['n'], 4) if t['n'] else 0.0,
            'query_weighted_coverage': round(t['covered'] / t['queries'], 4) if t['queries'] else 0.0,
            'extra_cpu': round(t['cpu'] / t['work'], 4) if t['work'] else 1.0,
            'owner_saving': round(t['saved'] / t['work'], 4) if t['work'] else 0.0}


def add(a, b):
    return {k: a.get(k, 0) + b[k] for k in b}


# Fitting ----------------------------------------------------------------------

def centres():
    numpy = np()
    return numpy.arange(LAST_GROUP + 1) * STEP + STEP // 2


def group_indices(offsets, slopes):
    """Grid index per (parameter, group) for parameter arrays offsets, slopes;
    the unknown group settles MIN_DEPTH."""
    numpy = np()
    m = centres()
    d = offsets[:, None] + ((slopes[:, None] * m[None, :]) >> 8)
    d = numpy.clip(d, MIN_DEPTH, MAX_DEPTH)
    index = numpy.where(d > GRID_LIMIT, EAGER, numpy.maximum(MIN_INDEX, -(-d // STEP)))
    return numpy.concatenate([index, numpy.full((len(offsets), 1), MIN_INDEX)], axis=1)


def sums(curves, offsets, slopes, chunk=4096):
    """Total saved and cpu over groups for each (offset, slope) pair."""
    numpy = np()
    saved, cpu = numpy.empty(len(offsets)), numpy.empty(len(offsets))
    rows = numpy.arange(GROUPS)[None, :]
    for s in range(0, len(offsets), chunk):
        index = group_indices(offsets[s:s + chunk], slopes[s:s + chunk])
        saved[s:s + chunk] = curves['saved'][rows, index].sum(axis=1)
        cpu[s:s + chunk] = curves['cpu'][rows, index].sum(axis=1)
    return saved, cpu


def best(saved, cpu, offsets, slopes, lam):
    """The pair maximising saved - lambda * cpu; ties go to the larger offset,
    then the larger slope."""
    numpy = np()
    score = saved - lam * cpu
    order = numpy.lexsort((slopes, offsets, score))
    i = order[-1]
    return float(score[i]), int(offsets[i]), int(slopes[i])


def fit_points(curves, lambdas):
    """Integer offset and slope256 per lambda maximising the objective over
    {group: grid arrays}: one coarse grid shared by every lambda, then unit-step
    refinement around each lambda's best."""
    numpy = np()
    grid_o, grid_s = numpy.meshgrid(numpy.array(OFFSETS), numpy.array(SLOPES), indexing='ij')
    grid_o, grid_s = grid_o.ravel(), grid_s.ravel()
    coarse = sums(curves, grid_o, grid_s)
    points = []
    for lam in lambdas:
        score, offset, slope = best(*coarse, grid_o, grid_s, lam)
        for step_o, step_s in REFINE:
            o, s = numpy.meshgrid(numpy.arange(offset - 5 * step_o, offset + 5 * step_o + 1, step_o),
                                  numpy.arange(max(1, slope - 5 * step_s), slope + 5 * step_s + 1, step_s),
                                  indexing='ij')
            o, s = o.ravel(), s.ravel()
            score, offset, slope = best(*sums(curves, o, s), o, s, lam)
        points.append({'offset': offset, 'slope256': slope})
    return points


def point_indices(point):
    numpy = np()
    return group_indices(numpy.array([point['offset']]), numpy.array([point['slope256']]))[0]


def cross_validate(data, lambdas=LAMBDAS, folds=FOLDS, by=None):
    """Held-out metrics per lambda, with whole games held out together. `by`
    maps the dataset to an integer label array (and its names) for a
    per-label breakdown."""
    numpy = np()
    lambdas = list(lambdas)
    games = data['games']
    folds = max(2, min(folds, len(games)))
    fold = numpy.array([fold_of(g, folds) for g in games])[data['game']]
    labels, names = by(data) if by else (numpy.zeros(len(fold), dtype=numpy.int64), None)
    count = len(names) if names else 1
    cells = folds * count * GROUPS
    cell = (fold * count + labels) * GROUPS + data['group']
    curves = cell_curves(data, cell, cells)
    shaped = {k: v.reshape((folds, count, GROUPS) + v.shape[1:]) for k, v in curves.items()}
    whole = {k: v.sum(axis=(0, 1)) for k, v in shaped.items()}
    totals = [dict(n=0) for _ in lambdas]
    groups = [[dict(n=0) for _ in lambdas] for _ in range(count)]
    points = []
    for f in range(folds):
        held = {k: v[f].sum(axis=0) for k, v in shaped.items()}
        if held['n'].sum() == 0:
            continue
        training = subtract(whole, held)
        if training['n'].sum() == 0:
            continue  # every game is in this fold: nothing to train on
        fitted = fit_points(training, lambdas)
        points.append(fitted)
        for i, point in enumerate(fitted):
            index = point_indices(point)
            totals[i] = add(totals[i], read(held, index))
            for label in range(count):
                groups[label][i] = add(groups[label][i], read({k: v[f, label] for k, v in shaped.items()}, index))
    results = []
    for i, lam in enumerate(lambdas):
        result = metric_values(totals[i])
        result['lambda'] = lam
        result['fold_points'] = [p[i] for p in points]
        if names:
            result['by'] = {names[label]: metric_values(groups[label][i]) for label in range(count)}
        results.append(result)
    return results


def under_budget(curve, budget):
    """Owner saving at `budget` extra CPU, interpolated between the best point
    within the budget and the next cheapest point beyond it that saves more."""
    within = [p for p in curve if p['extra_cpu'] <= budget]
    if not within:
        return {'score': 0.0, 'lambda': None}
    best_point = max(within, key=lambda p: (p['owner_saving'], p['extra_cpu']))
    score = best_point['owner_saving']
    beyond = [p for p in curve if p['extra_cpu'] > budget and p['owner_saving'] > score]
    if beyond:
        nxt = min(beyond, key=lambda p: p['extra_cpu'])
        f = (budget - best_point['extra_cpu']) / (nxt['extra_cpu'] - best_point['extra_cpu'])
        score += f * (nxt['owner_saving'] - score)
    return {'score': round(score, 4), 'lambda': best_point['lambda']}


def metrics(data, depths, by=None, members=None):
    """Metrics of predicting depths[i] for lifetime i, optionally per label and
    over `members` only (depths then one per member)."""
    numpy = np()
    index = numpy.where(depths > GRID_LIMIT, EAGER, numpy.maximum(MIN_INDEX, -(-depths // STEP)))
    labels, names = by(data) if by else (numpy.zeros(len(data['actual']), dtype=numpy.int64), None)
    if members is not None:
        labels = labels[members]
    count = len(names) if names else 1
    cell = numpy.zeros(len(data['actual']), dtype=numpy.int64)
    cell[members if members is not None else slice(None)] = labels * SIZE + index
    curves = cell_curves(data, cell, count * SIZE, members)
    shaped = {k: v.reshape((count, SIZE) + v.shape[1:]) for k, v in curves.items()}
    columns = numpy.arange(SIZE)
    per = [read({k: v[label] for k, v in shaped.items()}, columns) for label in range(count)]
    total = dict(n=0)
    for part in per:
        total = add(total, part)
    overall = metric_values(total)
    if names:
        return overall, {names[label]: metric_values(per[label]) for label in range(count)}
    return overall


def predict(data, point):
    """The engine's depth for every lifetime."""
    numpy = np()
    m = data['m']
    d = numpy.clip(point['offset'] + ((point['slope256'] * numpy.maximum(m, 0)) >> 8), MIN_DEPTH, MAX_DEPTH)
    return numpy.where(m < 0, MIN_DEPTH, d)


# Summary and header ---------------------------------------------------------

def point_name(lam):
    return f'l{round(lam * 1000):04d}'


def summarise(data, points, default):
    """The fitted operating points: `points` are the lambdas the header
    carries, and `default` is one of them."""
    lambdas = sorted(set(points) | {default})
    curves = cell_curves(data, data['group'], GROUPS)
    fitted = fit_points(curves, lambdas)
    return {'version': 5, 'games': len(data['games']), 'lifetimes': int(len(data['actual'])),
            'default': point_name(default),
            'points': [{'name': point_name(lam), 'lambda': lam, **p} for lam, p in zip(lambdas, fitted)]}


def point_of(summary, name=None):
    name = name or summary['default']
    return next(p for p in summary['points'] if p['name'] == name)


def emit_header(summary, provenance=()):
    points = summary['points']
    default = next(i for i, p in enumerate(points) if p['name'] == summary['default'])
    lines = [
        '// SPDX-License-Identifier: GPL-3.0-or-later',
        '// Generated by tools/gradient_depth_fit.py -- do not edit by hand.',
        '//',
        '// Predicted settle depth for a scheduled building walking field (see',
        '// docs/ai/architecture/building-gradient-depth-model.md), from the field\'s own past depths.',
        '// The depth only moves worker CPU: readers still resolve unsettled cells',
        '// synchronously, so a different depth never changes a simulation result',
        '// and needs no SIM_REVISION bump.',
        '//',
        *[f'// {line}' for line in provenance],
        '#pragma once',
        '',
        '#include <string_view>',
        '',
        'namespace BuildingGradientDepth',
        '{',
        f'inline constexpr int MIN_DEPTH = {MIN_DEPTH};',
        f'inline constexpr int MAX_DEPTH = {MAX_DEPTH};',
        '',
        '/// One operating point: depth = offset + (slope256 * m) >> 8 for m, the',
        '/// deeper of the two past depths. A lower lambda moves more search off the',
        '/// owner for more worker CPU.',
        'struct Point',
        '{',
        '\tstd::string_view name;',
        '\tint offset, slope256;',
        '};',
        '',
        'inline constexpr Point POINTS[] = {',
        *[f'\t{{"{p["name"]}", {p["offset"]}, {p["slope256"]}}},' for p in points],
        '};',
        f'inline constexpr int POINT_COUNT = {len(points)};',
        f'inline constexpr int DEFAULT_POINT = {default}; // {points[default]["name"]}',
        '',
        '/// The cost to settle up front from reader demand on the serving field',
        '/// `serving` and its last replaced lifetime `previous` (each -1 if',
        '/// unknown). Without either, as after loading a save, minimum depth lets the',
        '/// readers decide. Never affects results, only who pays.',
        'constexpr int target(int serving, int previous, int point = DEFAULT_POINT)',
        '{',
        '\tconst Point &p = POINTS[point];',
        '\tconst int m = serving > previous ? serving : previous;',
        '\tif (m < 0)',
        '\t\treturn MIN_DEPTH;',
        '\tconst long long d = p.offset + ((long long)p.slope256 * m >> 8);',
        '\treturn int(d < MIN_DEPTH ? MIN_DEPTH : d > MAX_DEPTH ? MAX_DEPTH : d);',
        '}',
        '',
        '/// The index of the point named `name`, or -1.',
        'constexpr int pointIndex(std::string_view name)',
        '{',
        '\tfor (int i = 0; i < POINT_COUNT; ++i)',
        '\t\tif (POINTS[i].name == name)',
        '\t\t\treturn i;',
        '\treturn -1;',
        '}',
        '} // namespace BuildingGradientDepth',
    ]
    return '\n'.join(lines) + '\n'


def provenance(summary, report=None):
    default = point_of(summary)
    lines = [f'Fitted on {summary["games"]} games, {summary["lifetimes"]} scheduled field lifetimes.',
             f'Default {default["name"]} (lambda {default["lambda"]}).']
    if report:
        lines.append(f'Default, held out by game: hit rate {report["hit_rate"]}, coverage '
                     f'{report["query_weighted_coverage"]}, extra CPU {report["extra_cpu"]}, '
                     f'owner saving {report["owner_saving"]}.')
    return lines


def by_size(data):
    """Map-size labels: (integer label per lifetime, label names)."""
    numpy = np()
    keys = [f'{w}x{h}' for w, h in zip(data['w'].tolist(), data['h'].tolist())]
    names = sorted(set(keys))
    position = {k: i for i, k in enumerate(names)}
    return numpy.array([position[k] for k in keys], dtype=numpy.int64), names


def by_scenario(data):
    """busy_oazis for the Oazis runs, tournament for everything else."""
    numpy = np()
    names = ['busy_oazis', 'tournament']
    per_game = numpy.array([0 if labels.get('map') == 'Oazis' else 1 for labels in data['labels']], dtype=numpy.int64)
    return per_game[data['game']], names


def strip(m):
    return {k: v for k, v in m.items() if k not in ('by', 'lambda', 'fold_points')}


def curve(data, lambdas, shares=SEARCH_SHARE):
    """Held-out owner saving against field extra CPU per lambda, plus eager."""
    numpy = np()
    held = cross_validate(data, lambdas, by=by_scenario)
    rows = []

    def row(point, name, m):
        value = {'point': point, 'scenario': name, **strip(m)}
        if name in shares:
            value['process_cpu_ratio'] = round(1 + shares[name] * (m['extra_cpu'] - 1), 4)
        rows.append(value)
    for m in held:
        row(point_name(m['lambda']), 'all', m)
        for name, values in m['by'].items():
            if values['lifetimes']:
                row(point_name(m['lambda']), name, values)
    eager, scenarios = metrics(data, numpy.full(len(data['actual']), MAX_DEPTH), by=by_scenario)
    row('eager', 'all', eager)
    for name, values in scenarios.items():
        if values['lifetimes']:
            row('eager', name, values)
    return rows


def print_curve(rows):
    print(f'{"point":6s} {"scenario":11s} {"hit":>6s} {"cover":>6s} {"field_cpu":>9s} {"owner_save":>10s} {"process_cpu":>11s}')
    for r in rows:
        process = f'{r["process_cpu_ratio"]:.3f}' if 'process_cpu_ratio' in r else '-'
        print(f'{r["point"]:6s} {r["scenario"]:11s} {r["hit_rate"]:6.3f} {r["query_weighted_coverage"]:6.3f} '
              f'{r["extra_cpu"]:9.3f} {r["owner_saving"]:10.3f} {process:>11s}')


# Commands -------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('dataset', help='tournament results or run directories -> dataset')
    p.add_argument('roots', nargs='+', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--jobs', type=int, default=1, help='processes parsing statistics files')
    for name in ('fit', 'evaluate'):
        p = sub.add_parser(name)
        p.add_argument('dataset', type=Path)
        p.add_argument('--report', type=Path)
        p.add_argument('--summary', type=Path, default=SUMMARY)
        if name == 'fit':
            p.add_argument('--budget', type=float, default=DEFAULT_BUDGET,
                           help='extra-CPU ratio that picks the default point when --operating-point is absent')
            p.add_argument('--operating-point', dest='lam', type=float,
                           help='the default point\'s lambda (default: the best held-out point within --budget)')
            p.add_argument('--points', nargs='*', type=float, default=[],
                           help='further operating points (lambdas) the header carries')
            p.add_argument('--header', type=Path, default=HEADER)
        else:
            p.add_argument('--curve', action='store_true', help='owner saving vs CPU per lambda')
            p.add_argument('--share', action='append', default=[], metavar='SCENARIO=SHARE',
                           help='search share of process CPU (default: the measured SEARCH_SHARE)')
    args = parser.parse_args(argv)

    if args.command == 'dataset':
        data = build_dataset(args.roots, args.jobs)
        save_dataset(data, args.output)
        print(f'{len(data["games"])} games, {len(data["actual"])} lifetimes -> {args.output}')
        return 0

    data = load_dataset(args.dataset)
    if args.command == 'fit':
        lam = args.lam
        if lam is None:
            lam = under_budget(cross_validate(data), args.budget)['lambda']
        if lam is None:
            raise SystemExit('no operating point meets the CPU budget')
        held_out = strip(cross_validate(data, [lam])[0])
        summary = summarise(data, args.points, lam)
        summary['cross_validated'] = held_out
        args.summary.write_text(json.dumps(summary, indent=1) + '\n')
        args.header.write_text(emit_header(summary, provenance(summary, held_out)))
        report = {'points': summary['points'], 'default': summary['default'], 'cross_validated': held_out}
        print(json.dumps(report, indent=2))
    elif args.curve:
        summary = json.loads(args.summary.read_text())
        shares = dict(SEARCH_SHARE)
        for item in args.share:
            name, value = item.split('=', 1)
            shares[name] = float(value)
        lambdas = sorted(set(LAMBDAS) | {p['lambda'] for p in summary['points']})
        rows = curve(data, lambdas, shares)
        print_curve(rows)
        report = {'shares': shares, 'curve': rows}
    else:
        summary = json.loads(args.summary.read_text())
        point = point_of(summary)
        overall, sizes = metrics(data, predict(data, point), by=by_size)
        held = cross_validate(data, [point['lambda']], by=by_size)[0]
        report = {'point': point, 'in_sample': overall, 'in_sample_by_size': sizes,
                  'cross_validated': {k: v for k, v in held.items() if k not in ('lambda', 'fold_points')}}
        print(json.dumps(report, indent=2))
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
