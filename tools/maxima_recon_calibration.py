#!/usr/bin/env python3
"""Plan reproducible reconnaissance games and analyse opt-in shadow diagnostics.

Execution and artifact verification use tools.tournaments. Model fitting is offline;
only explicitly allowlisted fog-safe observations are predictor inputs.
"""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import statistics

from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.model import job, validate_experiment
from tools.tournaments.results import Results

FEATURES = ['visible_warriors', 'remembered_warriors', 'warrior_age', 'force_age',
            'building_age', 'visible_explorers', 'visible_workers', 'visible_power',
            'known_buildings', 'visible_buildings', 'confidence', 'explored_percent', 'tick']
OPPONENTS = ['maxima', 'nicowar', 'cabino', 'cortex', 'warrush', 'castor', 'numbi', 'econo']


def parse(line):
    cells = line.rstrip('\n').split('\t')
    if len(cells) < 4 or cells[0] != 'MAXIMA_TELEMETRY':
        return None
    if cells[3] not in ('recon_audit', 'target_candidate', 'target_choice'):
        return None
    row = {'tick': int(cells[1]), 'observer': int(cells[2]), 'event': cells[3]}
    for cell in cells[4:]:
        key, value = cell.split('=', 1)
        try:
            row[key] = int(value)
        except ValueError:
            row[key] = value
    return row


def plan(args):
    bundles = [inspect_bundle(path) for path in args.bundle]
    jobs = []
    for g, generator in enumerate(args.generators):
        for f, fmt in enumerate(['1v1', '2v2', 'ffa']):
            for o, opponent in enumerate(OPPONENTS):
                # Same geography within each block; holdout splits keep the entire
                # block together, including mirrors and different opponents.
                seed = args.seed + 100 * g + f
                players = ['maxima', opponent]
                if fmt == '2v2':
                    players = ['maxima', 'maxima', opponent, opponent]
                elif fmt == 'ffa':
                    players = ['maxima', opponent, OPPONENTS[(o + 2) % 8], OPPONENTS[(o + 5) % 8]]
                config = {'generator': generator, 'params': {'width': 7, 'height': 7, 'teams': len(players)},
                          'candidates': 0, 'players': players, 'ticks': args.ticks}
                if fmt == '2v2':
                    config['alliances'] = [1, 1, 2, 2]
                jobs.append(job('game', bundles[len(jobs) % len(bundles)]['id'],
                                seeds={'map': seed, 'game': args.seed + 1000 + o}, config=config,
                                outputs={'telemetry': ['team-timeline', 'maxima-recon'],
                                         'saves': ['initial', 'final', 'every:8192']},
                                limits={'timeout_seconds': 3600},
                                labels={'format': fmt, 'opponent': opponent, 'generator': generator,
                                        'block': f'{generator}:{seed}:{fmt}', 'map_seed': seed}))
    manifest = validate_experiment({'schema_version': 1, 'id': args.id, 'kind': 'ai_comparison',
                                    'jobs': jobs, 'settings': {}, 'labels': {'purpose': 'recon-calibration'}})
    Path(args.output).write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{len(jobs)} jobs -> {args.output}')


def collect(directory):
    source = Results(directory)
    forces, candidates, choices = [], [], []
    completed = Counter()
    for record in source:
        if record['job']['type'] != 'game':
            continue
        labels = record['job']['labels']
        completed[record['category']] += 1
        with source.open_artifact(record, 'stdout.log') as stream:
            for line in stream:
                row = parse(line)
                if row is None:
                    continue
                row.update(job_id=record['job']['id'], host=record['host'], **labels)
                if row['event'] == 'recon_audit':
                    row['truth_units'] = row['truth_workers'] + row['truth_warriors'] + row['truth_explorers']
                players = record['job']['config']['players']
                params = record['job']['config'].get('params', {})
                if 'width' in params and 'height' in params:
                    row['map_width'], row['map_height'] = 1 << params['width'], 1 << params['height']
                if row.get('enemy', -1) >= 0:
                    row['enemy_ai'] = players[row['enemy']]
                {'recon_audit': forces, 'target_candidate': candidates, 'target_choice': choices}[row['event']].append(row)
    return forces, candidates, choices, completed


def metric(rows, predicted='estimated_warriors', truth='truth_warriors'):
    errors = [r[predicted] - r[truth] for r in rows]
    return {'samples': len(rows), 'games': len({r['job_id'] for r in rows}),
            'mae': statistics.mean(map(abs, errors)), 'bias': statistics.mean(errors),
            'under_by_50pct': statistics.mean(r[predicted] < r[truth] / 2 for r in rows),
            'over_by_2x': statistics.mean(r[predicted] > 2 * max(1, r[truth]) for r in rows)}


def target_metrics(candidates, choices):
    paired = defaultdict(dict)
    for row in choices:
        paired[(row['job_id'], row['observer'], row['tick'])][row['view']] = row
    comparisons = []
    for key, pair in paired.items():
        if set(pair) != {'fog', 'oracle'}:
            continue
        fog, oracle = pair['fog'], pair['oracle']
        match = all(fog[k] == oracle[k] for k in ['kind', 'enemy', 'gid', 'x', 'y'])
        same_region = match
        if fog['kind'] == oracle['kind'] == 'raid' and fog['enemy'] == oracle['enemy']:
            dx, dy = abs(fog['x']-oracle['x']), abs(fog['y']-oracle['y'])
            if 'map_width' in fog:
                dx, dy = min(dx, fog['map_width']-dx), min(dy, fog['map_height']-dy)
            same_region = dx*dx+dy*dy <= 49
        comparisons.append({'same_region': same_region, 'job_id': key[0], 'observer': key[1], 'tick': key[2], 'match': match,
                            'fog': fog, 'oracle': oracle})
    raids = [r for r in candidates if r['kind'] == 'raid' and r['workers']]
    return {'paired_choices': len(comparisons),
            'exact_agreement': statistics.mean(r['match'] for r in comparisons) if comparisons else None,
            'same_region_agreement': statistics.mean(r['same_region'] for r in comparisons) if comparisons else None,
            'choice_kinds': dict(Counter(r['fog']['kind']+'->'+r['oracle']['kind'] for r in comparisons)),
            'raid_same_region_radius': 7, 'raid_candidates': len(raids),
            'raid_fraction_inside_flag': statistics.mean(r['workers_in_radius']/r['workers'] for r in raids) if raids else None}, comparisons


def sampled_warriors(row, hold=2500, horizon=10000):
    age = row['warrior_age']
    confidence = 0 if age >= horizon else (100 if age <= hold else 100 - (age-hold)*100//(horizon-hold))
    return max(row['visible_warriors'], row['remembered_warriors']*confidence//100)


def fit(rows, out):
    # Dependencies belong to the analyst environment, never the worker bundle.
    import numpy as np
    from sklearn.ensemble import HistGradientBoostingRegressor
    from sklearn.model_selection import GroupKFold
    from sklearn.metrics import mean_absolute_error, mean_pinball_loss
    from threadpoolctl import threadpool_limits
    X = np.array([[min(r[k], 100000) if k.endswith('_age') else r[k] for k in FEATURES] for r in rows], dtype=float)
    groups = np.array([r['block'] for r in rows])
    if len(set(groups)) < 3:
        return {'status': 'insufficient independent map blocks'}
    # Equal game-observer weight, so long games/mirrors do not dominate training.
    counts = Counter((r['job_id'], r['observer']) for r in rows)
    weights = np.array([1/counts[(r['job_id'], r['observer'])] for r in rows])
    weights *= len(rows)/weights.sum()
    results = {}
    predictions = {}
    samples = np.array([sampled_warriors(r) for r in rows])
    known = np.array([r['known_buildings'] for r in rows])
    reconstructed = np.where(samples > 0, samples*250//100, known*150//100)
    baseline = np.array([r['estimated_warriors'] for r in rows])
    # Do not silently calibrate a different policy if a bundle overrides memory rules.
    if np.array_equal(reconstructed, baseline):
        y_count = np.array([r['truth_warriors'] for r in rows])
        simple = np.empty(len(rows))
        chosen = []
        for train, test in GroupKFold(n_splits=min(4, len(set(groups)))).split(X, y_count, groups):
            candidates = []
            for scale in range(100, 501, 25):
                for prior in range(0, 301, 25):
                    values = np.where(samples > 0, samples*scale//100, known*prior//100)
                    loss = np.average(np.abs(values[train]-y_count[train]), weights=weights[train])
                    candidates.append((loss, scale, prior))
            _, scale, prior = min(candidates)
            simple[test] = np.where(samples[test] > 0, samples[test]*scale//100, known[test]*prior//100)
            chosen.append({'recall_percent': scale, 'building_prior_percent': prior})
        results['two_parameter_count'] = {'mae': float(mean_absolute_error(y_count, simple)),
                                         'fold_parameters': chosen}
    else:
        results['two_parameter_count'] = {'status': 'memory settings differ from reference; skipped'}
    for target in ['truth_warriors', 'truth_power', 'truth_units']:
        y = np.array([r[target] for r in rows], dtype=float)
        pred = np.empty((len(rows), 3))
        for train, test in GroupKFold(n_splits=min(4, len(set(groups)))).split(X, y, groups):
            for column, quantile in enumerate([0.1, 0.5, 0.9]):
                model = HistGradientBoostingRegressor(loss='quantile', quantile=quantile,
                    max_iter=100, max_leaf_nodes=7, min_samples_leaf=30, l2_regularization=2,
                    early_stopping=False, random_state=19)
                with threadpool_limits(limits=1):
                    model.fit(X[train], y[train], sample_weight=weights[train])
                    pred[test, column] = np.maximum(0, model.predict(X[test]))
        pred.sort(axis=1)  # Rearrangement prevents crossed independently fitted quantiles.
        results[target] = {'mae': float(mean_absolute_error(y, pred[:,1])),
            'bias': float(np.mean(pred[:,1]-y)),
            'coverage_80': float(np.mean((pred[:,0]<=y)&(y<=pred[:,2]))),
            'mean_interval_width': float(np.mean(pred[:,2]-pred[:,0])),
            'pinball': [float(mean_pinball_loss(y,pred[:,i],alpha=q)) for i,q in enumerate([.1,.5,.9])]}
        results[target]['positive_truth'] = {
            'samples': int((y > 0).sum()),
            'mae': float(mean_absolute_error(y[y > 0], pred[y > 0, 1])) if np.any(y > 0) else None,
            'coverage_80': float(np.mean((pred[y > 0, 0] <= y[y > 0]) & (y[y > 0] <= pred[y > 0, 2]))) if np.any(y > 0) else None}
        results[target]['coverage_by_opponent'] = {}
        for ai in sorted({r['enemy_ai'] for r in rows}):
            mask = np.array([r['enemy_ai'] == ai for r in rows])
            results[target]['coverage_by_opponent'][ai] = float(np.mean((pred[mask,0]<=y[mask])&(y[mask]<=pred[mask,2])))
        predictions[target] = pred.tolist()
    for i, row in enumerate(rows):
        for target in predictions:
            row[target+'_q10'], row[target+'_q50'], row[target+'_q90'] = predictions[target][i]
    block_gains = []
    for group in sorted(set(groups)):
        selected = [r for r in rows if r['block'] == group]
        block_gains.append(statistics.mean(abs(r['estimated_warriors']-r['truth_warriors'])
                           - abs(r['truth_warriors_q50']-r['truth_warriors']) for r in selected))
    rng = np.random.default_rng(19)
    boot = rng.choice(block_gains, size=(2000, len(block_gains)), replace=True).mean(axis=1)
    results['warrior_mae_gain_by_map_block'] = {
        'blocks': len(block_gains), 'mean': statistics.mean(block_gains),
        'bootstrap_95': np.quantile(boot, [.025, .975]).tolist()}
    (out/'fit.json').write_text(json.dumps({'features': FEATURES, 'validation': 'GroupKFold by map/seed/format block',
                                          'targets': results}, indent=2)+'\n')
    return results


def analyse(args):
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    forces, candidates, choices, completed = collect(args.directory)
    if not forces:
        raise SystemExit('No recon_audit samples; verify maxima-recon was enabled and games completed.')
    summary = {'completed': dict(completed), 'force': metric(forces), 'by_opponent': {}, 'by_format': {}, 'by_phase': {}}
    for key, destination in [('enemy_ai','by_opponent'), ('format','by_format')]:
        for value in sorted({r[key] for r in forces}):
            summary[destination][value] = metric([r for r in forces if r[key] == value])
    for phase, low, high in [('early',0,8192),('middle',8192,24576),('late',24576,1000000)]:
        subset = [r for r in forces if low <= r['game_tick'] < high]
        if subset:
            summary['by_phase'][phase] = metric(subset)
    summary['by_information'] = {}
    for name, predicate in [('visible_warriors', lambda r: r['visible_warriors'] > 0),
                            ('memory_only', lambda r: r['visible_warriors'] == 0 and r['remembered_warriors'] > 0),
                            ('no_warrior_history', lambda r: r['remembered_warriors'] == 0),
                            ('confident_without_warriors', lambda r: r['confidence'] == 100 and r['visible_warriors'] == 0)]:
        subset = [r for r in forces if predicate(r)]
        if subset:
            summary['by_information'][name] = metric(subset)
    summary['combat_samples'] = metric([r for r in forces if r['truth_warriors'] > 0]) if any(r['truth_warriors'] > 0 for r in forces) else None
    summary['targets'], pairs = target_metrics(candidates, choices)
    if args.fit:
        summary['fit'] = fit(forces, out)
        if 'truth_warriors' in summary['fit']:
            for ai in summary['by_opponent']:
                subset = [r for r in forces if r['enemy_ai'] == ai]
                summary['by_opponent'][ai]['fitted_mae'] = metric(subset, 'truth_warriors_q50')['mae']
    for name, rows in [('forces',forces), ('candidates',candidates), ('choices',choices), ('pairs',pairs)]:
        (out/(name+'.jsonl')).write_text(''.join(json.dumps(row)+'\n' for row in rows))
    with (out/'forces.csv').open('w') as stream:
        writer = csv.DictWriter(stream, sorted(set().union(*(r.keys() for r in forces))))
        writer.writeheader(); writer.writerows(forces)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))


def end_metrics(source, record):
    from tools.tournaments.game_telemetry import parse as parse_measurement
    measured = {}
    labour = {}
    with source.open_artifact(record, 'stdout.log') as stream:
        for line in stream:
            if line.startswith('GLOB2_MEASURE '):
                _, values = parse_measurement(line)
                if values.get('final') == 1:
                    measured[values['team']] = values
            elif line.startswith('GLOB2_LABOUR '):
                values = {k: int(v) for k, v in (cell.split('=', 1) for cell in line.split()[1:])}
                labour[values['team']] = values
    own, enemy = measured[0], measured[1]
    result = record['result']
    metrics = {'ticks': result['ticks'], 'win': int(0 in result['winning_teams']),
               'loss': int(result['teams'][0]['outcome'] == 'lost'),
               'unresolved': int(result['termination'] == 'tick_cap'),
               'enemy_worker_combat_deaths': enemy['deaths_0_0'],
               'own_warrior_combat_deaths': own['deaths_2_0'],
               'melee_building_damage': own['damageDealt_0_1'],
               'melee_unit_damage': own['damageDealt_0_0'],
               'enemy_wheat_harvested': enemy['harvested_1'],
               'own_units': result['teams'][0]['units'], 'enemy_units': result['teams'][1]['units']}
    if 1 in labour:
        metrics['enemy_worker_ticks'] = labour[1]['b27']
        metrics['enemy_idle_worker_ticks'] = labour[1]['b0']
        metrics['labour_tick'] = labour[1]['tick']
    return metrics


def compare(args):
    baseline, candidate = Results(args.baseline), Results(args.candidate)
    base_records = {r['job']['id']: r for r in baseline if r['category'] == 'success'}
    pairs = []
    for r in candidate:
        base_id = r['job']['labels']['baseline_job']
        if r['category'] != 'success' or base_id not in base_records:
            continue
        original = base_records[base_id]
        a, b = end_metrics(baseline, original), end_metrics(candidate, r)
        pairs.append({'baseline_job': base_id, 'candidate_job': r['job']['id'],
                      'opponent': r['job']['labels']['opponent'], 'generator': r['job']['labels']['generator'],
                      'baseline': a, 'candidate': b,
                      'delta': {key: b[key]-a[key] for key in a if key in b}})
    summary = {'pairs': len(pairs)}
    if pairs:
        for variant in ['baseline', 'candidate']:
            summary[variant] = {key: sum(p[variant][key] for p in pairs) for key in ['win','loss','unresolved']}
        keys = sorted(set.intersection(*(set(p['delta']) for p in pairs)))
        summary['mean_delta'] = {key: statistics.mean(p['delta'][key] for p in pairs) for key in keys}
        summary['median_delta'] = {key: statistics.median(p['delta'][key] for p in pairs) for key in keys}
    out = Path(args.output); out.mkdir(parents=True, exist_ok=True)
    (out/'pairs.json').write_text(json.dumps(pairs, indent=2)+'\n')
    (out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


def refit(args):
    import gzip
    opener = gzip.open if str(args.rows).endswith('.gz') else open
    with opener(args.rows, 'rt') as stream:
        rows = [json.loads(line) for line in stream]
    out = Path(args.output); out.mkdir(parents=True, exist_ok=True)
    print(json.dumps(fit(rows, out), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan')
    p.add_argument('--bundle',action='append',required=True)
    p.add_argument('--generators',nargs='+',type=int,default=[15,17,11,44])
    p.add_argument('--seed',type=int,default=1)
    p.add_argument('--ticks',type=int,default=32768)
    p.add_argument('--id',default='maxima-recon')
    p.add_argument('--output',required=True)
    p.set_defaults(fn=plan)
    p = sub.add_parser('analyse')
    p.add_argument('directory'); p.add_argument('--output',required=True)
    p.add_argument('--fit',action='store_true'); p.set_defaults(fn=analyse)
    p = sub.add_parser('compare')
    p.add_argument('baseline'); p.add_argument('candidate'); p.add_argument('--output', required=True)
    p.set_defaults(fn=compare)
    p = sub.add_parser('fit')
    p.add_argument('rows'); p.add_argument('--output', required=True); p.set_defaults(fn=refit)
    args = parser.parse_args(); args.fn(args)


if __name__ == '__main__':
    main()
