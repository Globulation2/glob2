"""Offline map telemetry. One accepted logical sample is the statistical unit.

Raw records retain sequence, subject and typed values. Per-map means prevent maps
with repeated operations from receiving extra weight in numeric summaries.
"""
import json
import math
import random
import statistics
from collections import Counter, defaultdict


def numeric_leaves(value, path=''):
    """JSON pointer paths retain array positions; null and bool are not numbers."""
    if isinstance(value, dict):
        for key, child in value.items():
            yield from numeric_leaves(child, path + '/' + key.replace('~', '~0').replace('/', '~1'))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from numeric_leaves(child, path + '/' + str(index))
    elif type(value) in (int, float) and math.isfinite(value):
        yield path, value


def summarize(records, draws=1000, seed=1):
    from .analysis import percentile
    groups, rows, maps = {}, [], []
    rng = random.Random(seed)
    for record in records:
        job = record['job']
        if job['type'] != 'generate_map':
            continue
        report = (record.get('result') or {}).get('map_report') or {}
        generation = report.get('generation') or {}
        identity = dict(build=job['build'], generator=job['config']['generator'],
                        revision=generation.get('revision'), report_version=report.get('schema_version'),
                        config=job['config'], labels=job.get('labels', {}).get('variant'))
        key = json.dumps(identity, sort_keys=True)
        group = groups.setdefault(key, dict(identity, maps=0, outcomes=Counter(), missing=0,
            incomplete=0, metrics=defaultdict(list), fallbacks=Counter(), choices=Counter()))
        group['maps'] += 1
        group['outcomes'][record['category']] += 1
        trace = generation.get('telemetry') or {}
        available = trace.get('schema_version') == 1 and trace.get('enabled') is True
        incomplete = bool(trace.get('dropped_records', 0) or trace.get('invalid_values', 0))
        group['missing'] += not available
        group['incomplete'] += bool(available and incomplete)
        base = dict(job=job['id'], build=job['build'], generator=job['config']['generator'],
                    seed=job['seeds']['map'], chosen_seed=generation.get('seed'),
                    category=record['category'], revision=generation.get('revision'))
        # Preserve every final-map metric, including per-colony and pairwise measurements.
        final = dict(numeric_leaves({k:v for k,v in report.items() if k not in ('generation','engine')}))
        maps.append(dict(base, config=job['config'], telemetry_available=available,
                         telemetry_incomplete=incomplete, final_metrics=final))
        values, fallbacks, choices = defaultdict(list), set(), set()
        for sequence, item in enumerate(trace.get('records', []) if available else []):
            rows.append(dict(base, sequence=sequence, **item))
            value = item['value']
            if item['kind'] == 'measurement' and type(value) in (int, float) and math.isfinite(value):
                values[item['key']].append(value)
            if item['kind'] == 'fallback':
                fallbacks.add(item['key'])
            if item['kind'] == 'choice' or type(value) is bool:
                choices.add(json.dumps([item['key'], value], separators=(',', ':')))
        group['fallbacks'].update(fallbacks)
        group['choices'].update(choices)
        for metric, numbers in values.items():
            group['metrics']['telemetry/' + metric].append(statistics.mean(numbers))
        if record['category'] == 'success':
            for metric, value in final.items():
                group['metrics']['map' + metric].append(value)
    output = []
    for key in sorted(groups):
        group = groups[key]
        metrics = {}
        for name, values in sorted(group['metrics'].items()):
            estimates = ([values[0]] if draws and min(values) == max(values) else
                         [statistics.mean(rng.choices(values, k=len(values))) for _ in range(draws)])
            metrics[name] = dict(maps=len(values), mean=statistics.mean(values), minimum=min(values),
                maximum=max(values), p50=percentile(values, .5), p95=percentile(values, .95),
                mean_interval=[percentile(estimates,.025),percentile(estimates,.975)] if estimates else None)
        group['metrics'] = metrics
        for field in ('fallbacks', 'choices'):
            group[field] = {name: dict(maps=count, denominator_maps=group['maps'],
                observed_rate=count/group['maps']) for name,count in sorted(group[field].items())}
        output.append(group)
    return dict(schema_version=1, unit='accepted logical map sample', bootstrap_seed=seed,
        bootstrap_draws=draws, groups=output, records=rows, maps=maps, notes=[
            'Numeric telemetry first averages repeated records within each map; maps then have equal weight.',
            'Intervals resample maps within complete configuration/build/revision groups; use paired seed analysis for contrasts.',
            'Fallback/choice rates count maps with an observation, not records. Missing/truncated traces can undercount.',
            'Missing values are not zero. Raw sequence, subject, booleans, choices and sentinel numbers are retained.',
            'Retries and late duplicate attempts are excluded; raw attempt diagnostics remain in attempts/.'])
