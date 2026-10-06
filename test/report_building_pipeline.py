#!/usr/bin/env python3
"""Compare retained timing rounds and gameplay audits; never auto-promote a feature."""
import argparse
import collections
import csv
import json
import statistics
import math
from pathlib import Path
from analyze_building_gradient_impact import distribution, open_csv, market_changes


def mean_interval(values):
    """Exploratory two-sided t interval over independent seeds or paired rounds.

    Use conservative degrees-of-freedom breakpoints for larger samples. Events
    within a match never contribute independent samples to this interval.
    """
    values = list(values)
    if not values:
        return {'count': 0, 'mean': None, 'ci95': None}
    mean = statistics.mean(values)
    result = {'count': len(values), 'mean': mean, 'min': min(values), 'max': max(values),
              'ci95': None}
    if len(values) > 1:
        critical = {1: 12.706205, 2: 4.302653, 3: 3.182446, 4: 2.776445,
                    5: 2.570582, 6: 2.446912, 7: 2.364624, 8: 2.306004,
                    9: 2.262157, 10: 2.228139, 15: 2.131450, 20: 2.085963,
                    30: 2.042272, 40: 2.021075, 60: 2.000298, 120: 1.979930}
        df = len(values) - 1
        t = critical[max(k for k in critical if k <= df)]
        result['sd'] = statistics.stdev(values)
        half_width = t * result['sd'] / math.sqrt(len(values))
        result['ci95'] = [mean - half_width, mean + half_width]
    return result


def paired_statistics(pairs):
    pairs = list(pairs)
    baseline = [a for a, b in pairs]
    candidate = [b for a, b in pairs]
    denominator = sum(baseline)
    return {'baseline': mean_interval(baseline), 'candidate': mean_interval(candidate),
            'difference': mean_interval(b - a for a, b in pairs),
            'relative_change': mean_interval(b / a - 1 for a, b in pairs if a > 0),
            'pooled_relative_change': sum(candidate) / denominator - 1 if denominator else None}


def gameplay_statistics(volumes, audits, workloads):
    if not workloads:
        return {}
    if len(workloads) != len(set(workloads)):
        raise ValueError('statistical workloads must be unique')
    seeds = [volumes[name]['0']['game_seed'] for name in workloads]
    if None in seeds or len(set(seeds)) != len(seeds):
        raise ValueError('gameplay intervals require distinct known seeds')
    horizons = [volumes[name][delay].get('observed_ticks')
                for name in workloads for delay in ('0', '2', '4', '8')]
    if None in horizons or len(set(horizons)) != 1 or horizons[0] <= 0:
        raise ValueError('gameplay comparisons require identical known observation horizons')
    result = {'workloads': workloads, 'seeds': seeds, 'ticks_per_match': horizons[0],
              'independent_unit': 'seeded match',
              'limitations': 'Exploratory small-sample t intervals assume comparable independent seeds. '
              'Decision events and trips within a match are correlated. Whole-match differences do not '
              'establish the cause of a change. Completed-trip statistics exclude censored trips.',
              'delays': {}}
    for delay in ('2', '4', '8'):
        metrics = {}
        for metric in ('accepted_resource_units', 'starvation_deaths'):
            metrics[metric] = paired_statistics((volumes[name]['0'][metric], volumes[name][delay][metric])
                                                for name in workloads)
        available = [name for name in workloads
                     if name in audits and delay in audits[name]['delays']]
        if len(available) == len(workloads):
            for metric in ('delivery_events', 'construction_completions', 'unfilled_slot_ticks',
                           'hungry_unit_ticks', 'deaths'):
                metrics[metric] = paired_statistics((audits[name]['baseline']['economy'][metric],
                    audits[name]['delays'][delay]['economy'][metric]) for name in workloads)
            trip_pairs = [(audits[name]['baseline']['trip_measurements']['complete_duration_ticks'].get('p95'),
                           audits[name]['delays'][delay]['trip_measurements']['complete_duration_ticks'].get('p95'))
                          for name in workloads]
            metrics['completed_trip_p95_ticks'] = paired_statistics((a, b) for a, b in trip_pairs
                                                                    if a is not None and b is not None)
            for metric, column in (('completed_trip_mean_ticks', 'complete_duration_ticks'),
                                   ('completed_trip_mean_distance_tiles', 'complete_distance_tiles'),
                                   ('completed_trip_mean_reversals', 'complete_reversals')):
                pairs = []
                for name in workloads:
                    a = audits[name]['baseline']['trip_measurements'][column]
                    b = audits[name]['delays'][delay]['trip_measurements'][column]
                    if a['count'] and b['count']:
                        pairs.append((a['sum'] / a['count'], b['sum'] / b['count']))
                metrics[metric] = paired_statistics(pairs)
            for kind, numerator in (('movement', 'worse_step'), ('movement', 'changed'),
                                    ('hiring', 'live_failed_fresh_succeeded'),
                                    ('hiring_candidate', 'live_failed_fresh_succeeded'),
                                    ('resource', 'resource_type_changed'), ('resource_site', 'changed')):
                pairs = []
                for name in workloads:
                    a = audits[name]['baseline']['decisions'].get(kind, {})
                    b = audits[name]['delays'][delay]['decisions'].get(kind, {})
                    if a.get('decisions') and b.get('decisions'):
                        pairs.append((a.get(numerator, 0) / a['decisions'], b.get(numerator, 0) / b['decisions']))
                metrics[f'{kind}_{numerator}_fraction'] = paired_statistics(pairs)
        result['delays'][delay] = {'metrics': metrics, 'audited_seeds_complete': len(available),
                                  'audited_seeds_expected': len(workloads)}
    return result


def corrected_market_counts(directory):
    # Old retained summaries counted an unavailable fetch as harvesting. Read
    # only resource rows rather than parsing millions of hiring candidates.
    counts = collections.Counter()
    with open_csv(directory / 'building-gradient-impact-decisions.csv') as stream:
        columns = next(csv.reader([next(stream)]))
        for line in stream:
            if ',resource,' not in line and not line.startswith('resource,'):
                continue
            row = dict(zip(columns, next(csv.reader([line]))))
            if row['kind'] != 'resource':
                continue
            market, identity = market_changes(row)
            counts['market_vs_harvesting_changed'] += market
            counts['market_identity_changed'] += identity
    return dict(counts)


def resource_volumes(path):
    if path is None:
        return {}
    retained = json.loads(Path(path).read_text())
    workloads = collections.defaultdict(dict)
    for row in retained['rows']:
        counters = row['counters']
        workloads[row['workload']][str(row['delay'])] = {
            'accepted_resource_units': sum(v for k, v in counters.items() if k.startswith('delivered_')),
            'gathered_resource_units_including_market': sum(v for k, v in counters.items() if k.startswith('harvested_')),
            'starvation_deaths': sum(v for k, v in counters.items() if k.startswith('deaths_') and k.endswith('_1')),
            'accepted_by_resource': {k.removeprefix('delivered_'): v for k, v in counters.items() if k.startswith('delivered_')},
            'final_checksum': row['final_checksum'],
            'game_seed': row.get('result', {}).get('game_seed'),
            'observed_ticks': (row['result']['ticks'] - row['initial_tick']
                               if 'initial_tick' in row and 'result' in row else None),
        }
    for variants in workloads.values():
        baseline = variants['0']['accepted_resource_units']
        for row in variants.values():
            row['accepted_throughput_ratio'] = row['accepted_resource_units'] / baseline if baseline else None
            row['screening_flag'] = baseline > 0 and row['accepted_throughput_ratio'] < .98
    return dict(workloads)


def censor_bounds(directory):
    """Elapsed observations are lower bounds when no completed endpoint was seen."""
    bounds = collections.defaultdict(list)
    with open_csv(directory / 'building-gradient-impact-outcomes.csv') as stream:
        for row in csv.DictReader(stream):
            if int(row['censored']):
                bounds[row['kind']].append(int(row['elapsed_ticks']))
    return {kind: distribution(values) for kind, values in bounds.items()}


def trip_observations(directory):
    # Trips already underway in the checkpoint have an unknown start. Their
    # delivery counts remain valid, but observed elapsed time is a lower bound.
    with open_csv(directory / 'building-gradient-impact-ticks.csv') as stream:
        first_tick = int(next(csv.DictReader(stream))['tick'])
    hired = {}
    completed, partial, distances, reversals = [], [], [], []
    with open_csv(directory / 'building-gradient-impact-outcomes.csv') as stream:
        for row in csv.DictReader(stream):
            key = (row['unit'], row['unit_identity'], row['building'], row['building_identity'])
            tick, elapsed = int(row['tick']), int(row['elapsed_ticks'])
            if row['kind'] == 'hired':
                hired[key] = tick
            elif row['kind'] == 'delivered' and not int(row['censored']):
                start = tick - elapsed
                known_start = ((elapsed > 0 and start > first_tick)
                               or first_tick - 1 <= hired.get(key, -1) <= start)
                (completed if known_start else partial).append(elapsed)
                if known_start:
                    distances.append(int(row['distance']))
                    reversals.append(int(row['reversals']))
    return {'complete_duration_ticks': distribution(completed),
            'left_censored_duration_lower_bounds': distribution(partial),
            'complete_distance_tiles': distribution(distances),
            'complete_reversals': distribution(reversals)}


def timing_report(root):
    workloads = {}
    for path in Path(root).glob('*/timings.json'):
        rows = json.loads(path.read_text())
        for row in rows:
            samples = path.parent / Path(row['command'][-1]).name / 'building-gradient-timing.csv'
            if samples.exists():
                with samples.open() as stream:
                    census = list(csv.DictReader(stream))
                row['queue_depth'] = distribution(int(r['queue_depth']) for r in census)
                row['queue_bytes'] = distribution(int(r['queue_bytes']) for r in census)
        index = {(r['repeat'], r['delay'], r['workers']): r for r in rows}
        variants = {}
        for delay in (2, 4, 8):
            for workers in (1, 2, 4):
                samples = [r for r in rows if r['delay'] == delay and r['workers'] == workers]
                pairs = [(r, index[(r['repeat'], 0, workers)]) for r in samples]
                if not pairs:
                    continue
                reductions = [1 - r['result']['run_ns'] / b['result']['run_ns'] for r, b in pairs]
                variants[f'd{delay}-w{workers}'] = {
                    'engine_reduction': mean_interval(reductions),
                    'cpu_change': mean_interval(r['cpu_s'] / b['cpu_s'] - 1 for r, b in pairs),
                    'engine_cpu_change': mean_interval(
                        r['result']['benchmark_run_cpu_ns'] / b['result']['benchmark_run_cpu_ns'] - 1
                        for r, b in pairs if b['result'].get('benchmark_run_cpu_ns', 0) > 0
                        and r['result'].get('benchmark_run_cpu_ns', 0) > 0),
                    'repetitions': len(pairs), 'engine_reduction_median': statistics.median(reductions),
                    'engine_reduction_min': min(reductions), 'engine_reduction_max': max(reductions),
                    'rounds_faster': sum(x > 0 for x in reductions),
                    'engine_ns_median': statistics.median(r['result']['run_ns'] for r, _ in pairs),
                    'baseline_engine_ns_median': statistics.median(b['result']['run_ns'] for _, b in pairs),
                    'cpu_s_median': statistics.median(r['cpu_s'] for r, _ in pairs),
                    'baseline_cpu_s_median': statistics.median(b['cpu_s'] for _, b in pairs),
                    'cpu_ratio_median': statistics.median(r['cpu_s'] / b['cpu_s'] for r, b in pairs),
                    'peak_rss_bytes_median': statistics.median(r['peak_rss_bytes'] for r, _ in pairs),
                    'baseline_peak_rss_bytes_median': statistics.median(b['peak_rss_bytes'] for _, b in pairs),
                    'tick_p95_ns_median': statistics.median(r['tick_ns']['p95'] for r, _ in pairs),
                    'baseline_tick_p95_ns_median': statistics.median(b['tick_ns']['p95'] for _, b in pairs),
                    'tick_max_ns': max(r['tick_ns']['max'] for r, _ in pairs),
                    'baseline_tick_max_ns': max(b['tick_ns']['max'] for _, b in pairs),
                    'deadline_wait_p95_ns_median': statistics.median(r['deadline_wait_ns']['p95'] for r, _ in pairs),
                    'deadline_wait_max_ns': max(r['deadline_wait_ns']['max'] for r, _ in pairs),
                    'deadline_wait_total_ns_median': statistics.median(r['result']['building_gradient_wait_ns'] for r, _ in pairs),
                    'snapshot_cpu_ns_median': statistics.median(r['result']['building_gradient_snapshot_cpu_ns'] for r, _ in pairs),
                    'build_cpu_ns_median': statistics.median(r['result']['building_gradient_build_cpu_ns'] for r, _ in pairs),
                    'walking_fields_median': statistics.median(r['result']['building_gradient_walking_fields'] for r, _ in pairs),
                    'trip_fields_median': statistics.median(r['result']['building_gradient_trip_fields'] for r, _ in pairs),
                    'queue_depth_p95_median': statistics.median(r['queue_depth']['p95'] for r, _ in pairs)
                        if all('queue_depth' in r for r, _ in pairs) else None,
                    'queue_bytes_p95_median': statistics.median(r['queue_bytes']['p95'] for r, _ in pairs)
                        if all('queue_bytes' in r for r, _ in pairs) else None,
                    'submitted_jobs_median': statistics.median(r['result']['building_gradient_jobs'] for r, _ in pairs),
                    'coalesced_requests_median': statistics.median(r['result']['building_gradient_coalesced'] for r, _ in pairs),
                    'discarded_results_median': statistics.median(r['result']['building_gradient_discarded'] for r, _ in pairs),
                    'synchronous_fallback_cpu_ns_median': statistics.median(r['result']['building_gradient_fallback_cpu_ns'] for r, _ in pairs),
                    'max_pending': max(r['result']['building_gradient_max_pending'] for r, _ in pairs),
                    'max_reserved_bytes': max(r['result']['building_gradient_max_bytes'] for r, _ in pairs),
                    'synchronous_fallbacks': sum(r['result']['building_gradient_synchronous_fallback'] for r, _ in pairs),
                }
        workloads[path.parent.name] = variants
    return workloads


def counterfactual_report(cases):
    """Convenience diagnostic cases; event rows never become independent seeds."""
    paired = {}
    for delay in (2, 4, 8):
        groups = {}
        for kind in sorted({c['kind'] for c in cases if c['delay'] == delay}):
            selected = [c for c in cases if c['delay'] == delay and c['kind'] == kind]
            outcomes = {'case_rows': len(selected),
                        'distinct_interventions': len({(c['case']['tick'], c['case']['event']) for c in selected}),
                        'checkpoint_replays': sum(bool(c.get('checkpoint_proof')) for c in selected),
                        'rejected_late_checkpoints': sum(bool(c.get('checkpoint_rejection')) for c in selected)}
            for name in ('hired', 'harvested', 'market_acquired', 'acquired', 'delivered'):
                applicable = [c for c in selected if c['outcomes'][name].get('applicable', True)]
                observed = [c['outcomes'][name]['additional_ticks'] for c in applicable
                            if not c['outcomes'][name]['censored']]
                outcomes[name] = {'additional_ticks': distribution(observed),
                                  'cases': len(applicable),
                                  'censored': len(applicable) - len(observed),
                                  'completion_pairs': dict(collections.Counter(
                                      ('normal' if c['outcomes'][name]['normal'] else 'missing_normal') + '/' +
                                      ('fresh' if c['outcomes'][name]['fresh'] else 'missing_fresh')
                                      for c in applicable))}
            trips = [c['trip_endpoints'] for c in selected
                     if all((c.get('trip_endpoints', {}).get(side) or {}).get('kind') == 'delivered'
                            for side in ('normal', 'fresh'))]
            outcomes['observed_trip_differences'] = {
                'both_delivered': len(trips),
                'scope': 'Normal minus fresh; observations begin at the shared replay checkpoint. Earlier trip duration and initial heading can be left censored.',
                **{metric: distribution(int(t['normal'][metric]) - int(t['fresh'][metric])
                                        for t in trips)
                   for metric in ('elapsed_ticks', 'distance', 'reversals')}}
            outcomes['trip_endpoints'] = {
                side: dict(collections.Counter(
                    (c.get('trip_endpoints', {}).get(side) or {}).get('kind', 'no_matching_trip')
                    for c in selected)) for side in ('normal', 'fresh')}
            outcomes['hiring_censor_reasons'] = {
                side: dict(collections.Counter(
                    c['hiring_episode_endpoints'][side].get('censor_reason', 'unspecified')
                    for c in selected if c.get('hiring_episode_endpoints', {}).get(side)))
                for side in ('normal', 'fresh')}
            groups[kind] = outcomes
        hiring = [c['outcomes']['hired']['additional_ticks'] for c in cases
                  if c['delay'] == delay and c['kind'] in ('hiring', 'hiring_candidate')
                  and not c['outcomes']['hired']['censored']]
        hiring_distribution = distribution(hiring)
        paired[str(delay)] = {'categories': groups, 'observed_hiring_delay_ticks': hiring_distribution,
                              'hiring_screen': hiring_distribution['p95'] >= 8 if hiring else None}
    return paired


def audit_report(roots):
    workloads = {}
    for root in roots:
        for path in Path(root).glob('*/audit-d0/measurement.json'):
            base = json.loads(path.read_text())['impact']
            base['decisions'].get('resource', {}).update(corrected_market_counts(path.parent))
            base['censored_elapsed_lower_bounds'] = censor_bounds(path.parent)
            base['trip_measurements'] = trip_observations(path.parent)
            results = {}
            for delay in (2, 4, 8):
                candidate = path.parent.parent / f'audit-d{delay}' / 'measurement.json'
                if not candidate.exists():
                    continue
                audit = json.loads(candidate.read_text())['impact']
                audit['decisions'].get('resource', {}).update(corrected_market_counts(candidate.parent))
                ratios = {}
                for key in ('delivery_events', 'construction_completions'):
                    baseline_rate = base['economy'][key] / base['ticks']
                    candidate_rate = audit['economy'][key] / audit['ticks']
                    ratios[key] = candidate_rate / baseline_rate if baseline_rate else None
                trip_measurements = trip_observations(candidate.parent)
                trip_base = base['trip_measurements']['complete_duration_ticks'].get('p95')
                trip = trip_measurements['complete_duration_ticks'].get('p95')
                trip_ratio = trip / trip_base if trip_base else None
                flags = [key + ' more than 2% lower' for key, ratio in ratios.items() if ratio is not None and ratio < .98]
                if trip_ratio is not None and trip_ratio > 1.05:
                    flags.append('completed-trip p95 more than 5% longer')
                results[str(delay)] = {'throughput_ratios': ratios, 'completed_trip_p95_ratio': trip_ratio,
                    'screening_flags': flags, 'decisions': audit['decisions'], 'outcomes': audit['outcomes'],
                    'censored': audit['censored'], 'censor_reasons': audit['censor_reasons'],
                    'censored_elapsed_lower_bounds': censor_bounds(candidate.parent),
                    'trip_measurements': trip_measurements,
                    'actual_harvested_resources': audit.get('actual_harvested_resources', {}),
                    'actual_market_resources': audit.get('actual_market_resources', {}),
                    'economy': audit['economy']}
            paired_path = path.parent.parent / 'counterfactuals-corrected.json'
            if not paired_path.exists():
                paired_path = path.parent.parent / 'counterfactuals.json'
            paired = counterfactual_report(json.loads(paired_path.read_text())) if paired_path.exists() else {}
            workloads[path.parent.parent.name] = {'baseline': base, 'delays': results, 'counterfactuals': paired}
    return workloads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timing', required=True, type=Path)
    parser.add_argument('--audit', action='append', default=[], type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--resource-volumes', type=Path,
                        help='retained team-timeline counter deltas from identical configurations')
    parser.add_argument('--heavy-workload', action='append', default=[],
                        help='comparable independent seeded workload name; repeat for the statistical cohort')
    args = parser.parse_args()
    result = {'timing': timing_report(args.timing), 'audits': audit_report(args.audit),
              'resource_volumes': resource_volumes(args.resource_volumes),
              'interpretation': 'Whole-match outcomes are screening evidence after divergence, not causal attribution. Completed-only trip distributions must be read alongside censored outcomes. Promotion requires review of counterfactuals and actual play.'}
    result['gameplay_statistics'] = gameplay_statistics(result['resource_volumes'], result['audits'],
                                                       args.heavy_workload)
    args.output.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
