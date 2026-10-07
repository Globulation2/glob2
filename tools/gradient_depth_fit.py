#!/usr/bin/env python3
"""Fit building-field depth predictions from --telemetry gradient-stats rows.

Each run's gradient-stats.csv holds one row per building walking-field lifetime.
A lifetime's *actual* depth is the cost its lazy search had settled when it
ended (`prev_settled_cost`). A scheduled build would settle a predicted depth D
up front; readers past D still resolve synchronously, so D moves CPU and never
results. Two model families are fitted per group (route, swim class, building
type):

* ``constant``: D is a quantile of the group's actual depths;
* ``linear``: D = clamp(alpha * previous actual + beta), where *previous* is the
  same field's preceding lifetime in the same run.

Every candidate is evaluated with leave-one-seed-out cross-validation (each CSV
is one seed): fit on the other seeds, score the held-out one. Metrics:

* hit rate: share of lifetimes with actual <= D;
* query-weighted coverage: the same, weighted by resolve calls;
* extra-CPU ratio: sum popped(max(D, actual)) / sum popped(actual);
* owner saving: sum popped(min(D, actual)) / sum popped(actual), the share of
  lazy search work a scheduled build would move off the owner.

popped(c) integrates the 80-cost depth histogram; beyond what a partial
lifetime reached, the group's mean profile over complete lifetimes estimates
the work. Linear fits target a 0.85 training hit rate; the fitted table is
adopted only at pooled held-out hit rate >= 0.8 with extra-CPU <= 1.25,
otherwise the constant-per-group rule is kept. The report also scores the
provisional D = max(32, previous * 5/4) rule as a reference.
The adopted table is emitted as integer Q8 constants for
src/map/gradient/BuildingGradientDepthPolicy.h.
"""
import argparse
import csv
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

BIN_COST = 80
BINS = 32
MIN_DEPTH = 32
FULL = 1 << 16
MIN_GROUP = 30
ADOPT_HIT_RATE = 0.8
ADOPT_EXTRA_CPU = 1.25
# Fit with a margin over the held-out adoption threshold.
FIT_HIT_RATE = 0.85
QUANTILES = (0.5, 0.6, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0)
ALPHAS = tuple(a / 8 for a in range(4, 33))  # 0.5 .. 4.0
ROUTES = ('footprint', 'clearing', 'combat')


class Lifetime:
    __slots__ = ('seed', 'group', 'actual', 'previous', 'complete', 'popped', 'queries', 'bins')

    def __init__(self, seed, group, actual, previous, complete, popped, queries, bins):
        self.seed, self.group, self.actual, self.previous = seed, group, actual, previous
        self.complete, self.popped, self.queries, self.bins = complete, popped, queries, bins


def load(path, seed):
    """Lifetimes with a search, in field order so each can see its predecessor."""
    lifetimes = []
    previous = {}
    with open(path, newline='') as source:
        rows = list(csv.DictReader(source))
    rows.sort(key=lambda r: int(r['tick']))
    for row in rows:
        key = (row['team'], row['gid'], row['route'], row['swim'])
        if row['event'] == 'rebuild' and row['reason'] == 'null':
            previous.pop(key, None)
        if row.get('prev_search') != '1' or row.get('prev_locked') == '1':
            if row['event'] != 'rebuild':
                previous.pop(key, None)
            continue
        actual = int(row['prev_settled_cost'])
        bins = [int(row[f'popped_at_depth_{i}']) for i in range(BINS)]
        group = (row['route'], int(row['swim']), row['type'])
        lifetimes.append(Lifetime(seed, group, actual, previous.get(key), row['prev_complete'] == '1',
                                  int(row['prev_popped']), int(row['prev_queries']), bins))
        if row['event'] == 'rebuild':
            previous[key] = actual
        else:
            previous.pop(key, None)
    return lifetimes


def profiles(lifetimes):
    """Mean popped per depth bin over complete lifetimes, by group and pooled."""
    sums = defaultdict(lambda: [0.0] * BINS)
    counts = defaultdict(int)
    for life in lifetimes:
        if not life.complete:
            continue
        for key in (life.group, ('*',)):
            counts[key] += 1
            for i, value in enumerate(life.bins):
                sums[key][i] += value
    return {k: [v / counts[k] for v in values] for k, values in sums.items()}


def popped_to(life, cost, profile):
    """Entries popped settling every cost below `cost`."""
    if cost >= FULL and life.complete:
        return life.popped
    total = 0.0
    reach = life.actual
    for i in range(BINS):
        low = i * BIN_COST
        high = (i + 1) * BIN_COST if i < BINS - 1 else FULL
        if cost <= low:
            break
        observed_high = min(high, reach)
        if low < observed_high:
            # Observed part of the bin: popped layers [low, observed_high).
            span = observed_high - low
            total += life.bins[i] * min(1.0, (min(cost, observed_high) - low) / span)
        if cost > observed_high and not life.complete and profile:
            # Unobserved part, estimated from complete lifetimes.
            start = max(low, observed_high)
            width = (high - low) if i < BINS - 1 else BIN_COST
            total += profile[i] * (min(cost, high) - start) / width
    return total


def clamp(value):
    return max(MIN_DEPTH, min(FULL, int(round(value))))


def quantile(values, q):
    ordered = sorted(values)
    if not ordered:
        return FULL
    index = min(len(ordered) - 1, max(0, math.ceil(q * len(ordered)) - 1))
    return ordered[index]


def group_keys(group):
    route, swim, _ = group
    return (group, (route, swim, '*'), (route, '*', '*'), ('*', '*', '*'))


def by_group(lifetimes):
    groups = defaultdict(list)
    for life in lifetimes:
        for key in group_keys(life.group):
            groups[key].append(life)
    return groups


class Model:
    """A per-group table: (alpha, beta) with alpha == 0 for the constant family."""

    def __init__(self, family, table):
        self.family, self.table = family, table

    def rule(self, group):
        for key in group_keys(group):
            if key in self.table:
                return key, self.table[key]
        return None, (0.0, FULL)

    def predict(self, life):
        key, (alpha, beta) = self.rule(life.group)
        if alpha and life.previous is not None:
            return clamp(alpha * life.previous + beta)
        if alpha:
            # No predecessor: the group's constant quantile.
            return clamp(self.table.get(('fallback',) + key, (0.0, FULL))[1])
        return clamp(beta)


def evaluate(model, lifetimes, profile_table):
    hits = queries = covered = 0
    actual_work = cpu_work = saved_work = 0.0
    for life in lifetimes:
        profile = profile_table.get(life.group) or profile_table.get(('*',))
        depth = model.predict(life)
        hit = life.actual <= depth
        hits += hit
        queries += life.queries
        covered += life.queries if hit else 0
        actual = popped_to(life, life.actual, profile)
        actual_work += actual
        cpu_work += popped_to(life, max(depth, life.actual), profile)
        saved_work += popped_to(life, min(depth, life.actual), profile)
    count = len(lifetimes)
    return dict(lifetimes=count, hit_rate=hits / count if count else 0.0,
                query_weighted_coverage=covered / queries if queries else 0.0,
                extra_cpu=cpu_work / actual_work if actual_work else 1.0,
                owner_saving=saved_work / actual_work if actual_work else 0.0)


def fit_constant(lifetimes, q):
    table = {}
    for key, members in by_group(lifetimes).items():
        if len(members) >= MIN_GROUP or key == ('*', '*', '*'):
            table[key] = (0.0, quantile([m.actual for m in members], q))
    return Model('constant', table)


def fit_linear(lifetimes, profile_table, target_hit=FIT_HIT_RATE):
    """Per group: the cheapest (alpha, beta) whose training hit rate reaches the target."""
    table = {}
    for key, members in by_group(lifetimes).items():
        if not (len(members) >= MIN_GROUP or key == ('*', '*', '*')):
            continue
        paired = [m for m in members if m.previous is not None]
        fallback = quantile([m.actual for m in members], target_hit)
        table[('fallback',) + key] = (0.0, fallback)
        if len(paired) < MIN_GROUP:
            table[key] = (0.0, fallback)
            continue
        best = None
        for alpha in ALPHAS:
            # A lifetime hits when alpha * previous + beta >= actual (or actual is
            # under the clamp floor), so the cheapest beta reaching the target is
            # a quantile of the per-lifetime requirements.
            need = sorted(-math.inf if m.actual <= MIN_DEPTH else m.actual - alpha * m.previous
                          for m in paired)
            beta = math.ceil(quantile(need, target_hit))
            beta = 0 if beta == -math.inf else beta
            cost = sum(clamp(alpha * m.previous + beta) for m in paired)
            if best is None or cost < best[0]:
                best = (cost, alpha, beta)
        table[key] = (best[1], best[2]) if best else (0.0, fallback)
    return Model('linear', table)


def cross_validate(runs, family, q=ADOPT_HIT_RATE):
    folds = []
    pooled = []
    for held_out in runs:
        training = [life for seed, lives in runs.items() if seed != held_out for life in lives]
        if not training:
            training = runs[held_out]
        profile_table = profiles(training)
        model = fit_constant(training, q) if family == 'constant' else fit_linear(training, profile_table)
        score = evaluate(model, runs[held_out], profiles(runs[held_out]))
        folds.append(dict(seed=held_out, **score))
        pooled.extend((model, life) for life in runs[held_out])
    total = pooled_metrics(pooled, runs)
    return dict(folds=folds, pooled=total)


def pooled_metrics(pairs, runs):
    profile_by_seed = {seed: profiles(lives) for seed, lives in runs.items()}
    hits = queries = covered = 0
    actual_work = cpu_work = saved_work = 0.0
    for model, life in pairs:
        table = profile_by_seed[life.seed]
        profile = table.get(life.group) or table.get(('*',))
        depth = model.predict(life)
        hit = life.actual <= depth
        hits += hit
        queries += life.queries
        covered += life.queries if hit else 0
        actual_work += popped_to(life, life.actual, profile)
        cpu_work += popped_to(life, max(depth, life.actual), profile)
        saved_work += popped_to(life, min(depth, life.actual), profile)
    count = len(pairs)
    return dict(lifetimes=count, hit_rate=hits / count if count else 0.0,
                query_weighted_coverage=covered / queries if queries else 0.0,
                extra_cpu=cpu_work / actual_work if actual_work else 1.0,
                owner_saving=saved_work / actual_work if actual_work else 0.0)


def adopt(linear_pooled):
    return linear_pooled['hit_rate'] >= ADOPT_HIT_RATE and linear_pooled['extra_cpu'] <= ADOPT_EXTRA_CPU


def header(model, sources):
    """C++ constants for BuildingGradientDepthPolicy.h: D = clamp(alphaQ8 * prev / 256 + beta)."""
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later',
             '// Generated by tools/gradient_depth_fit.py; do not edit by hand.',
             f'// Family: {model.family}; inputs: {", ".join(sources)}',
             '#pragma once',
             '',
             'struct BuildingGradientDepthRule',
             '{',
             '\tint route, swim;',
             '\tconst char *type;',
             '\tint alphaQ8, beta;',
             '};',
             '',
             '// A rule matches route (0 footprint, 1 clearing, 2 combat; -1 any), swim class (-1 any)',
             '// and building type ("" any). First match wins; alphaQ8 == 0 means a constant depth.',
             f'inline constexpr int BUILDING_GRADIENT_DEPTH_MIN = {MIN_DEPTH};',
             'inline constexpr BuildingGradientDepthRule BUILDING_GRADIENT_DEPTH_RULES[] = {']
    keys = [k for k in model.table if k[0] != 'fallback']
    keys.sort(key=lambda k: (k[0] == '*', k[1] == '*', k[2] == '*', str(k)))
    for route, swim, kind in keys:
        alpha, beta = model.table[(route, swim, kind)]
        r = -1 if route == '*' else ROUTES.index(route)
        s = -1 if swim == '*' else int(swim)
        t = '' if kind == '*' else kind
        lines.append(f'\t{{{r}, {s}, "{t}", {int(round(alpha * 256))}, {int(beta)}}},')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('csv', nargs='+', type=Path, help='gradient-stats.csv per seed (SEED=PATH to name it)')
    parser.add_argument('--quantile', type=float, default=ADOPT_HIT_RATE, help='constant-family quantile')
    parser.add_argument('--report', type=Path, help='JSON report path')
    parser.add_argument('--header', type=Path, help='write the adopted table as C++ constants')
    args = parser.parse_args(argv)
    runs = {}
    sources = []
    for spec in args.csv:
        text = str(spec)
        seed, path = text.split('=', 1) if '=' in text and not Path(text).exists() else (spec.parent.name, text)
        runs[seed] = load(path, seed)
        sources.append(seed)
    constant = cross_validate(runs, 'constant', args.quantile)
    linear = cross_validate(runs, 'linear')
    # Reference: the scheduled pipeline's provisional rule, D = max(32, previous * 5 / 4).
    hint_model = Model('hint', {('*', '*', '*'): (1.25, 0), ('fallback', '*', '*', '*'): (0.0, MIN_DEPTH)})
    hint = dict(pooled=pooled_metrics([(hint_model, life) for lives in runs.values() for life in lives], runs))
    adopted = 'linear' if adopt(linear['pooled']) else 'constant'
    everything = [life for lives in runs.values() for life in lives]
    final = fit_linear(everything, profiles(everything)) if adopted == 'linear' \
        else fit_constant(everything, args.quantile)
    report = dict(seeds=sources, quantile=args.quantile, constant=constant, linear=linear, hint=hint,
                  adopted=adopted,
                  rule=dict(hit_rate=ADOPT_HIT_RATE, extra_cpu=ADOPT_EXTRA_CPU),
                  table={'/'.join(map(str, k)): dict(alpha=v[0], beta=v[1]) for k, v in final.table.items()
                         if k[0] != 'fallback'})
    text = json.dumps(report, indent=2)
    if args.report:
        args.report.write_text(text + '\n')
    if args.header:
        args.header.write_text(header(final, sources))
    for family, result in (('constant', constant), ('linear', linear), ('hint', hint)):
        p = result['pooled']
        print(f"{family:8s} hit={p['hit_rate']:.3f} coverage={p['query_weighted_coverage']:.3f} "
              f"extra_cpu={p['extra_cpu']:.3f} owner_saving={p['owner_saving']:.3f} n={p['lifetimes']}")
    print('adopted', adopted)
    return 0


if __name__ == '__main__':
    sys.exit(main())
