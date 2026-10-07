"""Fit offline cost-quantile policies to opt-in building-gradient-demand traces.

No model is applied to simulation. Whole-match holdouts prevent lookup leakage.
Raw costs are tenths of a cardinal land step, not geometric tile radii.
"""
import argparse
import collections
import csv
import gzip
import json
import math
import random
from pathlib import Path


def opened(path):
    return gzip.open(path, 'rt', newline='') if path.suffix == '.gz' else path.open(newline='')


def csv_path(directory, name):
    path = directory / name
    return path if path.exists() else Path(str(path) + '.gz')


def size_bucket(value):
    return max(0, int(value)).bit_length()


def feature_key(row, level=0):
    field = (row['route'], row['swim'], row['resource'])
    if level == 0:
        return (row['type'], size_bucket(row['colony_units']), size_bucket(row['colony_buildings']), *field)
    if level == 1:
        return (row['type'], *field)
    return field


def required_cost(row):
    if row['forbidden']:
        return 0  # Seeding already marks impassable cells.
    if not row['full'] and row['reachable']:
        return row['cost']
    if row['complete'] and row['after_cost'] > 0:
        return row['after_cost'] - 1  # Must prove exhaustion / finish the field.
    return None  # Do not silently remove unknown demand from coverage denominators.


def weighted_quantile(histogram, fraction):
    if not histogram:
        return None
    total = sum(histogram.values())
    partial = 0
    for value, weight in sorted(histogram.items()):
        partial += weight
        if partial + 1e-12 >= total * fraction:
            return value
    return max(histogram)


def export_aggregate(match, path):
    """Retain sufficient statistics for reproducible policy fitting without huge CSVs."""
    payload = {k: v for k, v in match.items() if k not in ('rows', 'extension_rows', '_fit_groups')}
    payload['schema'] = 1
    for name in ('rows', 'extension_rows'):
        payload[name] = [[list(features), cost, count] for (features, cost), count in match[name].items()]
    with gzip.open(path, 'wt') as stream:
        json.dump(payload, stream, separators=(',', ':'))


def read_aggregate(path):
    with gzip.open(path, 'rt') as stream:
        match = json.load(stream)
    if match.pop('schema') != 1:
        raise ValueError('unsupported demand aggregate schema')
    for name in ('rows', 'extension_rows'):
        match[name] = collections.Counter({(tuple(features), cost): count for features, cost, count in match[name]})
    return match


def read_match(directory):
    aggregate = directory / 'building-gradient-demand-aggregate.json.gz'
    if not csv_path(directory, 'building-gradient-demand-requests.csv').exists() and aggregate.exists():
        return read_aggregate(aggregate)
    with opened(csv_path(directory, 'building-gradient-demand-ticks.csv')) as stream:
        ticks = list(csv.DictReader(stream))
    dropped = sum(int(r['dropped_requests']) for r in ticks)
    if dropped:
        raise ValueError(f'{directory}: {dropped} dropped requests; incomplete traces cannot fit a policy')
    rows = collections.Counter()
    extension_rows = collections.Counter()
    epochs = {}
    callers = collections.Counter()
    caller_popped = collections.Counter()
    totals = collections.Counter()
    total_popped = 0
    with opened(csv_path(directory, 'building-gradient-demand-requests.csv')) as stream:
        for raw in csv.DictReader(stream):
            row = {k: int(v) if k not in ('type', 'caller') else v for k, v in raw.items()}
            cost = required_cost(row)
            features = (row['type'], row['colony_units'], row['colony_buildings'], row['route'], row['swim'], row['resource'])
            rows[(features, cost)] += row['requests']
            if row['extended']:
                extension_rows[(features, cost)] += row['requests']
            callers[(row['caller'], row['full'])] += row['requests']
            caller_popped[(row['caller'], row['full'])] += row['popped']
            totals['queries'] += row['requests']
            totals['extensions'] += row['requests'] if row['extended'] else 0
            totals['full_requests'] += row['requests'] if row['full'] else 0
            totals['unreachable'] += row['requests'] if not row['reachable'] and not row['forbidden'] and not row['full'] else 0
            totals['unknown'] += row['requests'] if cost is None else 0
            total_popped += row['popped']
            epoch = epochs.setdefault(row['epoch'], {'features': features, 'cost': 0, 'unknown': False, 'queries': 0,
                'full': False, 'first_tick': row['tick'], 'captured_tick': row['captured_tick']})
            epoch['cost'] = max(epoch['cost'], cost or 0)
            epoch['unknown'] |= cost is None
            epoch['queries'] += row['requests']
            epoch['full'] |= bool(row['full'])
    expected = sum(int(r['requests']) for r in ticks)
    if totals['queries'] != expected:
        raise ValueError(f'{directory}: request denominator mismatch {totals["queries"]} != {expected}')
    return {'directory': str(directory.resolve()), 'rows': rows, 'extension_rows': extension_rows, 'epochs': epochs, 'totals': dict(totals),
            'popped': total_popped, 'callers': [{'caller': k[0], 'full': bool(k[1]), 'requests': n, 'popped': caller_popped[k]} for k, n in callers.items()],
            'ticks': len(ticks), 'first_tick': min((int(r['tick']) for r in ticks), default=0)}


def observations(match, mode):
    if mode.endswith('queries') or mode.endswith('extensions'):
        source = match['extension_rows'] if mode.endswith('extensions') else match['rows']
        yield from ((features, cost, count) for (features, cost), count in source.items()
                    if not mode.startswith('walking') or features[-1] < 0)
    else:
        for epoch in match['epochs'].values():
            if mode.startswith('walking') and epoch['features'][-1] >= 0:
                continue
            # Features are frozen at the first observed query for this refresh.
            # A checkpoint can start inside a lifetime; report that limitation.
            yield epoch['features'], None if epoch['unknown'] else epoch['cost'], 1


def features_dict(features):
    return dict(zip(('type', 'colony_units', 'colony_buildings', 'route', 'swim', 'resource'), features))


def fit(matches, fraction, mode, minimum_matches=3):
    grouped = [collections.defaultdict(lambda: collections.defaultdict(collections.Counter)) for _ in range(3)]
    for index, match in enumerate(matches):
        cached = match.setdefault('_fit_groups', {}).get(mode)
        if cached is None:
            cached = [collections.defaultdict(collections.Counter) for _ in range(3)]
            for features, cost, count in observations(match, mode):
                if cost is None:
                    continue
                row = features_dict(features)
                for level in range(3):
                    cached[level][feature_key(row, level)][cost] += count
            match['_fit_groups'][mode] = cached
        for level, groups in enumerate(cached):
            for key, histogram in groups.items():
                grouped[level][key][index] = histogram
    model = []
    for groups in grouped:
        table = {}
        for key, by_match in groups.items():
            if len(by_match) < minimum_matches:
                continue
            # Equal weight per match within each stratum; one large colony cannot dominate.
            histogram = collections.Counter()
            for values in by_match.values():
                total = sum(values.values())
                for cost, count in values.items():
                    histogram[cost] += count / total
            value = weighted_quantile(histogram, fraction)
            table[key] = {'cost': int(math.ceil(value / 10) * 10), 'matches': len(by_match)}
        model.append(table)
    return model


def predict(model, features):
    row = features_dict(features)
    for level, table in enumerate(model):
        value = table.get(feature_key(row, level))
        if value is not None:
            return value['cost'], level
    return None, None


def mean_interval(values, seed=0):
    if not values:
        return {'mean': None, 'bootstrap95': None, 'matches': 0}
    average = sum(values) / len(values)
    if len(values) < 2:
        return {'mean': average, 'bootstrap95': None, 'matches': len(values)}
    rng = random.Random(seed)
    samples = sorted(sum(rng.choice(values) for _ in values) / len(values) for _ in range(2000))
    return {'mean': average, 'bootstrap95': [samples[49], samples[1949]], 'matches': len(values)}


def evaluate(matches, fraction, mode, minimum_matches=3):
    results = []
    for held, match in enumerate(matches):
        model = fit([m for i, m in enumerate(matches) if i != held], fraction, mode, minimum_matches)
        total = covered = known = predicted = 0
        budgets = collections.Counter()
        fallbacks = collections.Counter()
        for features, cost, count in observations(match, mode):
            budget, level = predict(model, features)
            total += count
            known += count if cost is not None else 0
            predicted += count if budget is not None else 0
            covered += count if cost is not None and budget is not None and cost <= budget else 0
            if budget is not None:
                budgets[budget] += count
                fallbacks[level] += count
        results.append({'directory': match['directory'], 'observations': total, 'known': known, 'predicted': predicted,
                        'covered': covered, 'coverage': covered / total if total else None,
                        'budget_land_steps_p50': (weighted_quantile(budgets, .50) or 0) / 10,
                        'budget_land_steps_p95': (weighted_quantile(budgets, .95) or 0) / 10,
                        'fallback_levels': dict(fallbacks)})
    usable = [r['coverage'] for r in results if r['coverage'] is not None]
    return {'training_quantile': fraction, 'holdout': 'leave one entire match out', 'mode': mode,
            'macro_match_coverage': mean_interval(usable), 'matches': results}


def summarize(matches):
    output = []
    for match in matches:
        values = collections.Counter()
        epoch_values = collections.Counter()
        for _, cost, count in observations(match, 'queries'):
            if cost is not None:
                values[cost] += count
        for _, cost, count in observations(match, 'epochs'):
            if cost is not None:
                epoch_values[cost] += count
        output.append({k: match[k] for k in ('directory', 'totals', 'popped', 'callers', 'ticks')} |
                      {'epochs': len(match['epochs']), 'epochs_with_full_requests': sum(e['full'] for e in match['epochs'].values()),
                       'epochs_started_before_observation': sum(e['captured_tick'] < match['first_tick'] for e in match['epochs'].values()),
                       'required_land_steps': {str(p): (weighted_quantile(values, p) or 0) / 10 for p in (.50, .95, .99)},
                       'epoch_maximum_land_steps': {str(p): (weighted_quantile(epoch_values, p) or 0) / 10 for p in (.50, .95, .99)}})
    return output


def fixed_budgets(matches, mode):
    results = []
    for steps in (16, 30, 32, 48, 64, 96, 128):
        coverages = []
        for match in matches:
            total = covered = 0
            for _, cost, count in observations(match, mode):
                total += count
                covered += count if cost is not None and cost <= steps * 10 else 0
            if total:
                coverages.append(covered / total)
        results.append({'land_steps': steps, 'coverage': mean_interval(coverages)})
    return {'mode': mode, 'policies': results}


def model_tables(matches, fraction, mode):
    return {'mode': mode, 'training_quantile': fraction,
            'levels': [[{'features': list(key), **value} for key, value in sorted(table.items())]
                       for table in fit(matches, fraction, mode)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--export-aggregates', type=Path, help='Export sufficient statistics for sharing and reproducing the analysis')
    args = parser.parse_args()
    paths = [p.resolve() for p in args.directories]
    if len(paths) != len(set(paths)):
        parser.error('each match directory must be unique')
    matches = [read_match(p) for p in paths]
    if args.export_aggregates:
        for path, match in zip(paths, matches):
            target = args.export_aggregates / path.name
            target.mkdir(parents=True, exist_ok=True)
            export_aggregate(match, target / 'building-gradient-demand-aggregate.json.gz')
    result = {'schema': 1, 'note': 'Offline coverage prediction, not measured performance. Independent-match bootstrap; no within-match lookup independence claim. Partial observation lifetimes and future topology changes limit prediction. Raw cost/10 is land-equivalent movement cost, not a geometric radius. Full and unreachable requests require exhaustion, not merely a nearby tile. Unknown or unseen strata count as misses.',
              'runs': summarize(matches),
              'fixed_budgets': [fixed_budgets(matches, mode) for mode in ('queries', 'epochs', 'walking_queries', 'walking_epochs', 'walking_extensions')],
              'fitted_models': [model_tables(matches, .95, mode) for mode in ('queries', 'epochs', 'walking_queries', 'walking_epochs', 'walking_extensions')],
              'policies': [evaluate(matches, q, mode) for mode in ('queries', 'epochs', 'walking_queries', 'walking_epochs', 'walking_extensions') for q in (.95, .975, .99)]}
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
