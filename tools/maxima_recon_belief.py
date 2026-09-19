#!/usr/bin/env python3
"""Fit fog-safe present and forward beliefs; evaluate frozen models on new games.

Training uses hidden state only as a label. Predictions use the information at
the cutoff, its causal observation history, and elapsed AI time. Exported trees
have integer thresholds and fixed-point leaves; inference needs no sklearn.
"""
import argparse
from collections import Counter, defaultdict
import gzip
import hashlib
import json
import math
from pathlib import Path

from tools.maxima_recon_calibration import FEATURES, sampled_warriors

TARGETS = ['truth_warriors', 'truth_power', 'truth_units']
QUANTILES = [.1, .5, .9, .95]
HISTORY_KEYS = ['visible_warriors', 'visible_workers', 'visible_power', 'known_buildings']
HISTORY_FEATURES = [k+s for k in HISTORY_KEYS for s in ['_previous', '_peak', '_rate']]
BELIEF_FEATURES = FEATURES + HISTORY_FEATURES + ['forecast_horizon']
SCALE = 1024


def read(path):
    with gzip.open(path, 'rt') as stream:
        return [json.loads(line) for line in stream]


def write_rows(path, rows):
    with gzip.GzipFile(filename=str(path), mode='wb', mtime=0) as stream:
        for row in rows:
            stream.write((json.dumps(row, sort_keys=True)+'\n').encode())


def training_rows(paths):
    """Load force JSONL exports without assuming a particular campaign layout."""
    rows = []
    for path in paths:
        for row in read(path):
            # Calibration exports already carry geography blocks and game ids.
            # Preserve them so shared maps remain together in held-out folds.
            if 'block' not in row:
                raise ValueError('Training observations require a geography block')
            row['truth_units'] = row['truth_workers'] + row['truth_warriors'] + row['truth_explorers']
            rows.append(row)
    return rows


def sequences(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row['job_id'], row['observer'], row['enemy']].append(row)
    for key in sorted(groups):
        seq = sorted(groups[key], key=lambda r: r['tick'])
        if len({r['tick'] for r in seq}) != len(seq):
            raise ValueError('Duplicate observation in enemy series')
        yield seq


class ObservationMemory:
    """One enemy's fixed-size, fog-only memory at the audit observation cadence.

    This is a prototype belief state, independent of the simulation. Use one
    instance per observer/enemy and feed observations in strictly increasing AI
    time. Hidden labels and identities are neither retained nor used.
    """
    def __init__(self):
        self.previous = None
        self.peak = {key: 0 for key in HISTORY_KEYS}
        self.cutoff = None

    def observe(self, observation):
        source = {key: observation[key] for key in FEATURES}
        if any(type(value) is not int for value in source.values()):
            raise ValueError('Belief observables must be integers')
        if self.previous is not None and source['tick'] <= self.previous['tick']:
            raise ValueError('Observation ticks must increase')
        before = source if self.previous is None else self.previous
        dt = max(1, source['tick']-before['tick'])
        row = dict(source)
        for key in HISTORY_KEYS:
            self.peak[key] = max(self.peak[key], source[key])
            row[key+'_previous'] = before[key]
            row[key+'_peak'] = self.peak[key]
            # Floor division is part of the feature schema, including declines.
            row[key+'_rate'] = (source[key]-before[key])*1000//dt
        self.previous = source
        self.cutoff = row
        return self.project(source['tick'])

    def project(self, tick):
        if self.cutoff is None or tick < self.cutoff['tick']:
            raise ValueError('A forecast needs an observation at or before its tick')
        return dict(self.cutoff, forecast_horizon=tick-self.cutoff['tick'])


def histories(rows):
    output = []
    for seq in sequences(rows):
        memory = ObservationMemory()
        for source in seq:
            output.append(dict(source, **memory.observe(source)))
    return output


def examples(rows, horizons=(0, 2000, 5000)):
    """Artificial blackouts: cutoff inputs, future labels, no future sightings.

    A label is the first audit at or beyond the requested horizon, at most one
    audit interval late. Actual elapsed AI ticks are an explicit model input.
    No extrapolated labels beyond a game's last living-enemy observation.
    """
    output = []
    for seq in sequences(histories(rows)):
        for i, source in enumerate(seq):
            for horizon in horizons:
                future = next((r for r in seq[i:] if r['tick'] >= source['tick']+horizon), None)
                if future is None or future['tick']-source['tick']-horizon >= 1000:
                    continue
                row = dict(source)
                row.update(forecast_horizon=future['tick']-source['tick'],
                           requested_horizon=horizon, label_tick=future['tick'],
                           label_game_tick=future['game_tick'])
                for target in TARGETS:
                    row[target] = future[target]
                output.append(row)
    return output


def weights(rows):
    """Equal geography mass, then equal observer/enemy series within geography."""
    import numpy as np
    counts = Counter((r['block'], r['job_id'], r['observer'], r['enemy']) for r in rows)
    series = Counter(key[0] for key in counts)
    result = np.array([1/(series[r['block']]*counts[r['block'], r['job_id'], r['observer'], r['enemy']])
                       for r in rows])
    return result*len(result)/result.sum()


def features(row, names):
    return [min(row[k], 100000) if k.endswith('_age') else row[k] for k in names]


def lower_bound(row, target):
    # A current sighting is a lower bound now, never after a blackout: units die.
    if row['forecast_horizon']:
        return 0
    if target == 'truth_warriors':
        return row['visible_warriors']
    if target == 'truth_power':
        return row['visible_power']
    return row['visible_warriors']+row['visible_workers']+row['visible_explorers']


def export_tree(nodes):
    result = []
    for node in nodes:
        if node['is_leaf']:
            result.append([-1, int(round(float(node['value'])*SCALE)), 0, 0])
        else:
            # Features are integers: x <= float threshold iff x <= floor(threshold).
            result.append([int(node['feature_idx']), math.floor(float(node['num_threshold'])),
                           int(node['left']), int(node['right'])])
    return result


def fit(rows, names):
    import numpy as np
    from sklearn.ensemble import HistGradientBoostingRegressor
    from threadpoolctl import threadpool_limits
    X = np.array([features(r, names) for r in rows], dtype=float)
    w = weights(rows)
    exported = {'schema_version': 1, 'scale': SCALE, 'features': names,
                'quantiles': QUANTILES, 'models': {}, 'parameters': {
                    'iterations': 100, 'leaves': 7, 'min_samples_leaf': 30,
                    'l2_regularization': 2, 'random_state': 19}}
    with threadpool_limits(limits=1):
        for target in TARGETS:
            y = np.array([r[target] for r in rows], dtype=float)
            models = []
            for q in QUANTILES:
                model = HistGradientBoostingRegressor(loss='quantile', quantile=q,
                    max_iter=100, max_leaf_nodes=7, min_samples_leaf=30,
                    l2_regularization=2, early_stopping=False, random_state=19)
                model.fit(X, y, sample_weight=w)
                converted = {'base': int(round(float(model._baseline_prediction[0, 0])*SCALE)),
                    'trees': [export_tree(predictors[0].nodes) for predictors in model._predictors]}
                # Verify the export on every training row before retaining it.
                exact = model.predict(X)
                portable = np.array([predict_tree_model(converted, list(x), SCALE) for x in X])
                error = float(np.max(np.abs(exact-portable)))
                if error > (1+len(converted['trees']))/(2*SCALE)+1e-8:
                    raise ValueError('Tree export differs beyond fixed-point rounding error')
                converted['max_training_export_error'] = error
                models.append(converted)
            exported['models'][target] = models
    return exported


def predict_tree_model(model, x, scale):
    value = model['base']
    for tree in model['trees']:
        index = 0
        while tree[index][0] >= 0:
            feature, threshold, left, right = tree[index]
            index = left if x[feature] <= threshold else right
        value += tree[index][1]
    return value/scale


def coherent(prediction):
    # Warriors are a subset of units and every living warrior contributes at
    # least one power. Their marginal quantiles must obey the same dominance.
    warriors = prediction['truth_warriors']
    return {target: values[:] if target == 'truth_warriors' else
            [max(w, v) for w, v in zip(warriors, values)]
            for target, values in prediction.items()}


def predict(model, rows):
    result = []
    for row in rows:
        x = features(row, model['features'])
        # A present-only model is the persistence reference during a blackout.
        # Retain exactly its cutoff prediction, including the cutoff lower bound.
        bound_row = row if 'forecast_horizon' in model['features'] else dict(row, forecast_horizon=0)
        result.append(coherent({target: sorted(max(lower_bound(bound_row, target),
            predict_tree_model(tree_model, x, model['scale'])) for tree_model in models)
            for target, models in model['models'].items()}))
    return result


def metrics(rows, predictions):
    import numpy as np
    w = weights(rows)
    result = {}
    for target in TARGETS:
        y = np.array([r[target] for r in rows])
        p = np.array([r[target] for r in predictions])
        av = lambda v: float(np.average(v, weights=w))
        error = y[:, None]-p
        result[target] = {'mae': av(abs(error[:, 1])), 'bias': av(-error[:, 1]),
            'q90_miss': av(y > p[:, 2]), 'q95_miss': av(y > p[:, 3]),
            'coverage80': av((y >= p[:, 0]) & (y <= p[:, 2])),
            'width80': av(p[:, 2]-p[:, 0]),
            'pinball': [av(np.maximum(q*error[:, i], (q-1)*error[:, i]))
                        for i, q in enumerate(QUANTILES)]}
    result['samples'] = len(rows)
    result['blocks'] = len({r['block'] for r in rows})
    return result


def evaluate(model, rows):
    predictions = predict(model, rows)
    strata = {'all': list(range(len(rows)))}
    for key in ['requested_horizon', 'enemy_ai', 'format', 'map_width']:
        for value in sorted({r[key] for r in rows}):
            strata[f'{key}={value}'] = [i for i, r in enumerate(rows) if r[key] == value]
    for low, high in [(0, 1), (1, 2500), (2500, 10000), (10000, 2000000)]:
        strata[f'warrior_age={low}:{high}'] = [i for i, r in enumerate(rows)
            if low <= r['warrior_age']+r['forecast_horizon'] < high]
    return {key: metrics([rows[i] for i in indices], [predictions[i] for i in indices])
            for key, indices in strata.items() if indices}, predictions


def paired_gain(rows, baseline, candidate):
    """Bootstrap whole geographies; repeats/arm variants stay in their block."""
    import numpy as np
    w = weights(rows)
    groups = sorted({r['block'] for r in rows})
    gains = []
    for block in groups:
        mask = np.array([r['block'] == block for r in rows])
        gains.append(float(np.average((np.array(baseline)-np.array(candidate))[mask], weights=w[mask])))
    rng = np.random.default_rng(19)
    boot = rng.choice(gains, size=(10000, len(gains)), replace=True).mean(axis=1)
    return {'blocks': len(groups), 'mae_gain': float(np.mean(gains)),
            'bootstrap95': np.quantile(boot, [.025, .975]).tolist()}


def weighted_quantile(values, sample_weights, quantile):
    pairs = sorted(zip(values, sample_weights))
    if not pairs or not 0 <= quantile <= 1 or any(w <= 0 for _, w in pairs):
        raise ValueError('A weighted quantile requires positive weights and observations')
    threshold = quantile*sum(w for _, w in pairs)
    total = 0
    for value, weight in pairs:
        total += weight
        if total >= threshold:
            return value
    return pairs[-1][0]


def calibration_group(row):
    # No opponent identity. Distinguish an unseen military from observed military
    # activity, and a current estimate from a forecast during loss of contact.
    return f'{int(row["forecast_horizon"] > 0)}:{int(row["visible_warriors_peak"] > 0)}'


def fit_calibration(rows, predictions):
    result = {}
    groups = ['all']+sorted({calibration_group(r) for r in rows})
    for group in groups:
        indices = [i for i, r in enumerate(rows) if group == 'all' or calibration_group(r) == group]
        selected = [rows[i] for i in indices]
        if group != 'all' and len({r['block'] for r in selected}) < 4:
            continue
        w = weights(selected)
        result[group] = {}
        for target in TARGETS:
            factors = []
            for index, q in [(2, .9), (3, .95)]:
                scores = [(rows[i][target]-predictions[i][target][1]) /
                          max(1, predictions[i][target][index]-predictions[i][target][1]) for i in indices]
                factors.append(max(0, weighted_quantile(scores, w, q)))
            result[group][target] = factors
    return result


def apply_calibration(calibration, row, prediction):
    factors = calibration.get(calibration_group(row), calibration['all'])
    result = {}
    for target in TARGETS:
        low, mid, high, highest = prediction[target]
        high, highest = [mid+factor*max(1, raw-mid)
                         for factor, raw in zip(factors[target], [high, highest])]
        result[target] = [low, mid, high, max(high, highest)]
    return coherent(result)


def calibrate(rows, out):
    """Secondary, cross-validated recalibration; not a new prospective holdout."""
    predictions = [r['beliefs']['forward'] for r in rows]
    blocks = sorted({r['block'] for r in rows}, key=lambda b: hashlib.sha256(b.encode()).hexdigest())
    if len(blocks) < 8:
        raise ValueError('Recalibration needs at least eight held-out geographies')
    fold = {b: i % 4 for i, b in enumerate(blocks)}
    calibrated = [None]*len(rows)
    fits = []
    for f in range(4):
        train = [i for i, r in enumerate(rows) if fold[r['block']] != f]
        test = [i for i, r in enumerate(rows) if fold[r['block']] == f]
        fit = fit_calibration([rows[i] for i in train], [predictions[i] for i in train])
        fits.append({'test_blocks': [b for b in blocks if fold[b] == f], 'calibration': fit})
        for i in test:
            calibrated[i] = apply_calibration(fit, rows[i], predictions[i])
    summary = {}
    strata = {'all': list(range(len(rows)))}
    for key in ['requested_horizon', 'enemy_ai', 'map_width']:
        for value in sorted({r[key] for r in rows}):
            strata[f'{key}={value}'] = [i for i, r in enumerate(rows) if r[key] == value]
    for name, indices in strata.items():
        selected = [rows[i] for i in indices]
        summary[name] = {'raw': metrics(selected, [predictions[i] for i in indices]),
                         'calibrated': metrics(selected, [calibrated[i] for i in indices])}
    (out/'calibration.json').write_text(json.dumps({'validation': '4 folds by whole new map block; exploratory recalibration',
        'folds': fits, 'summary': summary,
        'fit_all': fit_calibration(rows, predictions)}, indent=2)+'\n')
    write_rows(out/'calibrated-predictions.jsonl.gz', [dict(row, calibrated=pred)
                                                   for row, pred in zip(rows, calibrated)])


def compare(models, observations, out):
    rows = examples(observations)
    predictions = {name: predict(model, rows) for name, model in models.items()}
    strata = {}
    for h in [0, 2000, 5000]:
        strata[f'horizon={h}'] = [i for i, r in enumerate(rows) if r['requested_horizon'] == h]
    for key in ['enemy_ai', 'format', 'map_width']:
        for value in sorted({r[key] for r in rows}):
            strata[f'present/{key}={value}'] = [i for i, r in enumerate(rows)
                if r[key] == value and not r['forecast_horizon']]
    for name, predicate in [
        ('fresh', lambda r: r['warrior_age'] <= 2500),
        ('aged', lambda r: 2500 < r['warrior_age'] < 1000000),
        ('unseen', lambda r: r['warrior_age'] >= 1000000),
        ('late', lambda r: r['game_tick'] >= 32768),
        ('positive_army', lambda r: r['truth_warriors'] > 0),
    ]:
        strata['present/'+name] = [i for i, r in enumerate(rows) if not r['forecast_horizon'] and predicate(r)]
    report = {}
    for name, indices in strata.items():
        if not indices:
            continue
        selected = [rows[i] for i in indices]
        result = {n: metrics(selected, [p[i] for i in indices]) for n, p in predictions.items()}
        legacy = []
        for row in selected:
            if not row['forecast_horizon']:
                value = row['estimated_warriors']
            else:
                sampled = sampled_warriors(dict(row, visible_warriors=0,
                    warrior_age=row['warrior_age']+row['forecast_horizon']))
                value = sampled*250//100 if sampled else row['known_buildings']*150//100
            legacy.append(abs(value-row['truth_warriors']))
        import numpy as np
        result['legacy_count_mae'] = float(np.average(legacy, weights=weights(selected)))
        result['gains'] = {}
        for target in TARGETS:
            losses = {n: [abs(p[i][target][1]-rows[i][target]) for i in indices]
                      for n, p in predictions.items()}
            result['gains'][target] = {
                'history_over_snapshot': paired_gain(selected, losses['snapshot'], losses['present']),
                'forward_over_persistence': paired_gain(selected, losses['present'], losses['forward'])}
            if target == 'truth_warriors':
                result['gains'][target]['history_over_legacy'] = paired_gain(selected, legacy, losses['present'])
                result['gains'][target]['forward_over_legacy'] = paired_gain(selected, legacy, losses['forward'])
        report[name] = result
    (out/'comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    write_rows(out/'predictions.jsonl.gz', [dict(row, beliefs={n: p[i] for n, p in predictions.items()})
                                         for i, row in enumerate(rows)])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('fit')
    p.add_argument('--training', action='append', required=True,
                   help='Force JSONL.gz export; repeat to combine campaigns')
    p.add_argument('--output', required=True)
    p = sub.add_parser('evaluate')
    p.add_argument('--model', required=True)
    p.add_argument('--forces', required=True)
    p.add_argument('--output', required=True)
    p = sub.add_parser('compare')
    p.add_argument('--models', required=True)
    p.add_argument('--forces', required=True)
    p.add_argument('--output', required=True)
    p = sub.add_parser('calibrate')
    p.add_argument('--predictions', required=True)
    p.add_argument('--output', required=True)
    args = parser.parse_args()
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    if args.command == 'fit':
        rows = training_rows(args.training)
        ex = examples(rows)
        for name, selected, names in [('snapshot', [r for r in ex if not r['forecast_horizon']], FEATURES),
                                       ('present', [r for r in ex if not r['forecast_horizon']], FEATURES+HISTORY_FEATURES),
                                       ('forward', ex, BELIEF_FEATURES)]:
            model = fit(selected, names)
            model['training'] = {'games': len({r['job_id'] for r in rows}),
                'blocks': len({r['block'] for r in rows}), 'observations': len(rows),
                'examples': len(selected), 'horizons': [0, 2000, 5000],
                'inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in map(Path, args.training)}}
            write_rows(out/(name+'-model.json.gz'), [model])
            print(name, 'frozen', flush=True)
    elif args.command == 'evaluate':
        model = read(args.model)[0]
        rows = examples(read(args.forces))
        summary, predictions = evaluate(model, rows)
        (out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
        write_rows(out/'predictions.jsonl.gz', [dict(row, belief=pred) for row, pred in zip(rows, predictions)])
        print(json.dumps(summary['all'], indent=2))
    elif args.command == 'compare':
        models = {name: read(Path(args.models)/(name+'-model.json.gz'))[0]
                  for name in ['snapshot', 'present', 'forward']}
        compare(models, read(args.forces), out)
    else:
        calibrate(read(args.predictions), out)


if __name__ == '__main__':
    main()
