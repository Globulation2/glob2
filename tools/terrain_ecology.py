#!/usr/bin/env python3
"""Report paired ecology assays without hiding sparse or incomplete evidence.

Thresholds are fixed at 5% corpus error and 10% error on at least 90% of maps.
Precision uses independent seeded runs, not a Poisson assumption about correlated
spread events. Its conservative t multiplier is 2.2 for >=16 seeds; a baseline
95% half-width above half the map tolerance requires additional sampling. This
approximate confidence diagnostic assumes seed-level means are approximately
normal. It is not a distribution-free proof or a replacement for full AI games.
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics

RESOURCES = ('wood', 'wheat', 'papyrus', 'stone', 'algae', 'cherry', 'orange', 'prune')
METRICS = ('net_growth', 'added', 'occupied', 'stock', 'harvested')
ARRAYS = ('stock', 'occupied', 'added', 'removed', 'new_tiles', 'harvested')
MIN_SEEDS = 16
PRECISION_FRACTION = .05
T_MULTIPLIER = 2.2


def relative_error(baseline, candidate):
    return abs(candidate-baseline)/abs(baseline) if baseline else (math.inf if candidate else 0)


def finite(value):
    return value if math.isfinite(value) else None


def precision(baseline, candidate):
    n = len(baseline)
    mean = statistics.mean(baseline)
    if n < MIN_SEEDS:
        return dict(seeds=n, baseline_adequate=False, difference_precise=False,
                    reason='fewer than 16 independent seeds', baseline_half_width=None,
                    difference_half_width=None)
    base_width = T_MULTIPLIER*statistics.stdev(baseline)/math.sqrt(n)
    differences = [c-b for b, c in zip(baseline, candidate)]
    difference_width = T_MULTIPLIER*statistics.stdev(differences)/math.sqrt(n)
    adequate = mean != 0 and base_width <= PRECISION_FRACTION*abs(mean)
    return dict(seeds=n, baseline_adequate=adequate,
                difference_precise=mean != 0 and difference_width <= PRECISION_FRACTION*abs(mean),
                reason=None if adequate else 'zero baseline or baseline precision exceeds 5%',
                baseline_half_width=base_width, difference_half_width=difference_width)


def role(scenario, metric):
    if metric in ('stock', 'occupied'):
        return 'storage'
    if scenario == 'replenishment' and metric in ('net_growth', 'harvested'):
        return 'production'
    return 'turnover_or_unattended_net_change_diagnostic'


def compare(rows, expected_maps=None, expected_seeds=None, expected_ticks=None):
    pairs, initial, initial_tiles = defaultdict(dict), {}, {}
    runs = defaultdict(list)
    contexts = defaultdict(lambda: defaultdict(set))
    for row in rows:
        if (not isinstance(row['map'],str) or not row['map'] or
                type(row['seed']) is not int or row['seed']<1 or
                type(row['tick']) is not int or row['tick']<1 or
                row['scenario'] not in ('unattended','replenishment')):
            raise ValueError('invalid observation identity')
        for field in ARRAYS:
            values = row[field]
            if len(values) != len(RESOURCES) or any(type(v) is not int or v < 0 for v in values):
                raise ValueError(f'invalid resource counter array: {field}')
        if row['variant'] not in ('baseline', 'candidate'):
            raise ValueError('unknown variant')
        inferred = tuple(s+r+h-a for s,r,h,a in zip(row['stock'],row['removed'],row['harvested'],row['added']))
        if min(inferred) < 0 or row['map'] in initial and initial[row['map']] != inferred:
            raise ValueError(f'conservation or initial-state mismatch: {row["map"]}')
        initial[row['map']] = inferred
        inferred_tiles=tuple(o-n for o,n in zip(row['occupied'],row['new_tiles']))
        if min(inferred_tiles)<0 or row['map'] in initial_tiles and initial_tiles[row['map']]!=inferred_tiles:
            raise ValueError(f'tile conservation mismatch: {row["map"]}')
        initial_tiles[row['map']]=inferred_tiles
        runs[(row['map'],row['seed'],row['scenario'],row['variant'])].append(row)
        key = (row['map'], row['seed'], row['scenario'], row['tick'])
        if row['variant'] in pairs[key]:
            raise ValueError(f'duplicate observation: {key} {row["variant"]}')
        pairs[key][row['variant']] = row
        contexts[row['map']][(row['scenario'], row['tick'])].add(row['seed'])
    if not pairs:
        raise ValueError('empty assay')
    for run in runs.values():
        run.sort(key=lambda row:row['tick'])
        for before,after in zip(run,run[1:]):
            if any(a<b for field in ('added','removed','new_tiles','harvested')
                   for b,a in zip(before[field],after[field])):
                raise ValueError('cumulative growth counter decreased')
    for key, pair in pairs.items():
        if set(pair) != {'baseline', 'candidate'}:
            raise ValueError(f'incomplete paired observation: {key}')
    if expected_maps is not None and set(contexts) != set(expected_maps):
        raise ValueError('map inventory does not match manifest')
    for map_name, samples in contexts.items():
        if expected_ticks is not None and set(samples) != {(s,t) for s in ('unattended','replenishment') for t in expected_ticks}:
            raise ValueError(f'incomplete scenario/snapshot inventory: {map_name}')
        seed_sets = list(samples.values())
        if any(seeds != seed_sets[0] for seeds in seed_sets):
            raise ValueError(f'unbalanced seed inventory within map: {map_name}')
        if expected_seeds is not None and seed_sets[0] != set(expected_seeds):
            raise ValueError(f'seed inventory does not match requested run: {map_name}')
    groups = defaultdict(lambda: defaultdict(lambda: [[], []]))
    for (map_name, seed, scenario, tick), pair in sorted(pairs.items()):
        for resource, name in enumerate(RESOURCES):
            for metric in METRICS:
                group = groups[(scenario, tick, name, metric)][map_name]
                for i, variant in enumerate(('baseline', 'candidate')):
                    row = pair[variant]
                    group[i].append(row['added'][resource]-row['removed'][resource]
                                    if metric == 'net_growth' else row[metric][resource])
    reports = []
    for (scenario, tick, resource, metric), maps in sorted(groups.items()):
        cases = []
        resource_id = RESOURCES.index(resource)
        for name, (b, c) in sorted(maps.items()):
            baseline, candidate = statistics.mean(b), statistics.mean(c)
            error = relative_error(baseline,candidate)
            structural_zero = baseline == candidate == 0 and (
                initial[name][resource_id] == 0 or
                resource == 'stone' and metric in ('added','net_growth','harvested') or
                scenario == 'unattended' and metric == 'harvested')
            sampling = precision(b,c)
            cases.append(dict(map=name, baseline=baseline,candidate=candidate,
                              relative_error=finite(error), within_10_percent=error<=.10,
                              structurally_neutral=structural_zero, sampling=sampling))
        # Means keep each map's corpus weight unchanged if sparse maps need
        # additional seeds; summing raw observations would overweight them.
        baseline = sum(case['baseline'] for case in cases)
        candidate = sum(case['candidate'] for case in cases)
        active = [case for case in cases if not case['structurally_neutral']]
        adequate = [case for case in active if case['sampling']['baseline_adequate']]
        pending = [case for case in active if not case['sampling']['baseline_adequate'] or not case['sampling']['difference_precise']]
        error = relative_error(baseline,candidate)
        passing = sum(case['within_10_percent'] for case in active)
        coverage = passing/len(active) if active else 1
        adequate_coverage = sum(case['within_10_percent'] for case in adequate)/len(adequate) if adequate else (None if active else 1)
        raw_passes = error<=.05 and coverage>=.90
        status = 'inconclusive' if pending else ('pass' if raw_passes else 'fail')
        reports.append(dict(scenario=scenario,tick=tick,resource=resource,metric=metric,
                            role=role(scenario,metric),baseline=baseline,candidate=candidate,
                            corpus_relative_error=finite(error),active_maps=len(active),
                            neutral_maps=len(cases)-len(active),adequately_sampled_maps=len(adequate),
                            inadequate_or_imprecise_maps=len(pending),maps_within_10_percent=passing,
                            fraction_within_10_percent=coverage,
                            adequately_sampled_fraction_within_10_percent=adequate_coverage,
                            raw_passes=raw_passes,passes=status=='pass',status=status,
                            worst_maps=sorted(active,key=lambda case:relative_error(case['baseline'],case['candidate']),reverse=True)[:5],
                            cases=cases))
    primary = [row for row in reports if row['role'] in ('production','storage')]
    def status(items):
        if any(item['status']=='fail' for item in items): return 'fail'
        return 'inconclusive' if any(item['status']=='inconclusive' for item in items) else 'pass'
    return dict(schema_version=2,corpus_limit=.05,map_limit=.10,required_map_fraction=.90,
                passes=status(reports)=='pass',status=status(reports),primary_status=status(primary),
                raw_passes=all(row['raw_passes'] for row in reports),
                precision_policy=dict(minimum_seeds=MIN_SEEDS,baseline_relative_half_width=PRECISION_FRACTION,
                                      difference_relative_half_width=PRECISION_FRACTION,t_multiplier=T_MULTIPLIER),
                corpus_aggregation='sum of per-map seed means; extra seeds do not alter map weights',
                inventory_verified=expected_maps is not None and expected_seeds is not None and expected_ticks is not None,
                conservation_verified=True,
                scope='isolated growth/replenishment; full-game supply/starvation checks required separately',comparisons=reports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs',nargs='+',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--manifest',type=Path)
    parser.add_argument('--first-seed',type=int,default=1)
    parser.add_argument('--seeds',type=int)
    parser.add_argument('--ticks',type=int)
    args = parser.parse_args()
    rows = [json.loads(line) for path in args.inputs for line in path.read_text().splitlines() if line]
    maps = [line for line in args.manifest.read_text().splitlines() if line and not line.startswith('#')] if args.manifest else None
    seeds = range(args.first_seed,args.first_seed+args.seeds) if args.seeds else None
    ticks = sorted({t for t in (1024,4096,16384,65536,args.ticks) if t and t<=args.ticks}) if args.ticks else None
    report = compare(rows,maps,seeds,ticks)
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    failed = [r for r in report['comparisons'] if not r['raw_passes']]
    print(f'{len(report["comparisons"])-len(failed)}/{len(report["comparisons"])} raw comparisons pass; '
          f'{len(failed)} raw failures; primary status: {report["primary_status"]}')
    for row in failed[:20]:
        print(row['scenario'],row['tick'],row['resource'],row['metric'],'corpus error',row['corpus_relative_error'],
              'map coverage',row['fraction_within_10_percent'],'status',row['status'])
    return 0 if report['passes'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
