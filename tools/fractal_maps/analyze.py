#!/usr/bin/env python3
"""Per-map support and per-AI/per-seat outcomes from immutable tournament records.

Missing jobs and telemetry are explicit. Capped games remain capped: this report
never turns an adjudicated cap into an engine victory or treats retries as games.
"""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
from tools.tournaments.results import Results
from tools.tournaments.game_telemetry import parse


def write_csv(path, rows):
    keys = sorted({key for row in rows for key in row})
    with path.open('w', newline='') as f:
        writer = csv.DictWriter(f, keys)
        writer.writeheader()
        writer.writerows(rows)


def gameplay_rows(source, record):
    """Decode only gameplay counters used by this summary from the verified log.

    AI/performance records remain in the raw artifact for the general telemetry
    exporter. Parsing those large records just to discard them made repeated
    diagnostic summaries needlessly slow. Errors here refer to gameplay rows.
    """
    if not any(a['path'] == 'stdout.log' for a in record['artifacts']):
        yield {'error': 'missing stdout.log'}
        return
    with source.open_artifact(record, 'stdout.log') as stream:
        for line in stream:
            if not line.startswith('GLOB2_MEASURE '):
                continue
            try:
                prefix, values = parse(line)
                yield {'record': prefix, 'values': values}
            except ValueError as error:
                yield {'error': str(error)}


def analyze(directory, output, attempt_ids=None):
    source = Results(directory)
    rows, teams, errors = [], [], []
    observed = set()
    catalogs = {}
    for path in (source.root / 'builds').glob('*/bundle.json'):
        bundle = json.loads(path.read_text())
        catalogs[path.parent.name] = {g['method']: g for g in bundle['capabilities']['generators']}
    support = defaultdict(lambda: defaultdict(set))
    committed = {r['id'] for r in source}
    records = list(source)
    if attempt_ids:
        # Diagnostic controls may finish after a transport lease expires. Analyze
        # only explicitly named retained attempts, never silently prefer a result
        # or count late duplicates as extra independent games.
        wanted = set(attempt_ids)
        records = [r for r in source.attempts() if r['id'] in wanted]
        if {r['id'] for r in records} != wanted:
            raise ValueError('requested diagnostic attempt is missing')
    for record in records:
        job = record['job']; observed.add(job['id'])
        result = record.get('result') or {}
        row = dict(attempt=record['id'], committed=record['id'] in committed, job=job['id'], build=job['build'], kind=job['type'], category=record['category'],
                   seconds=record['seconds'], generator=job['labels'].get('generator'),
                   seed=job['labels'].get('map_seed'), rotation=job['labels'].get('rotation'),
                   diagnostic=result.get('diagnostic', ''), missing_artifacts=','.join(record['missing_artifacts']))
        definition = catalogs.get(job['build'], {}).get(row['generator'], {})
        row['generator_key'] = definition.get('id', 'unknown')
        if job['type'] == 'generate_map':
            # Normalize omitted defaults before grouping: baseline and grid jobs
            # may be duplicate requests, not independent seed evidence.
            params = {c['id']: c['default'] for c in definition.get('controls', [])}
            params.update(job['config']['params']); row.update(params)
            config_key = (job['build'], row['generator_key'], json.dumps(params, sort_keys=True))
            support[config_key][record['category']].add(row['seed'])
            report = result.get('map_report') or {}
            row['report_present'] = bool(report)
            generation = report.get('generation', {})
            row['generation_stage'] = generation.get('outcome', {}).get('stage')
            quality = report.get('canonical_quality', {})
            row['quality_worst'] = quality.get('worst')
            row['quality_fairness'] = quality.get('fairness')
            for metric in generation.get('telemetry', {}).get('records', []):
                key, value = metric['key'], metric['value']
                if any(part in key for part in ('.depth.', '.bank-farms.', '.lakes', '.home-stops', '.size-stops')):
                    row[key] = value
        else:
            row.update(ai=job['config'].get('players', ['saved'])[0], ticks=result.get('ticks'),
                       termination=result.get('termination'))
            # Keep final cumulative fields per team, and the first observed combat tick.
            final, combat, opening, peak = {}, {}, {}, defaultdict(int)
            telemetry_errors = 0
            for trace in gameplay_rows(source, record):
                if 'error' in trace:
                    telemetry_errors += 1; continue
                if trace['record'] != 'GLOB2_MEASURE': continue
                value = trace['values']; team = value['team']; tick = value['tick']
                final[team] = value
                population = sum(value.get(f'births_{u}', 0) + value.get(f'conversionsIn_{u}', 0)
                                 - value.get(f'conversionsOut_{u}', 0)
                                 - sum(value.get(f'deaths_{u}_{cause}', 0) for cause in range(5)) for u in range(3))
                # Four starting workers is the pilot's explicit catalog default.
                peak[team] = max(peak[team], population + 4)
                if tick <= 10000: opening[team] = value
                if team not in combat and sum(v for k, v in value.items() if k.startswith('damageReceived_') and isinstance(v, int)):
                    combat[team] = tick
            row['telemetry_teams'] = len(final); row['gameplay_telemetry_errors'] = telemetry_errors
            for team in result.get('teams', []):
                k = team['team']; measures = final.get(k, {})
                values = dict(row, team=k, alive=team['alive'], units_final=team.get('units'),
                              buildings_final=team.get('buildings'), peak_population=peak.get(k),
                              eliminated_tick=team.get('eliminated_tick'), first_observed_combat=combat.get(k),
                              timeline_final_tick=measures.get('tick'))
                for label, data in [('final', measures), ('opening', opening.get(k, {}))]:
                    values[f'{label}_starvation'] = sum(data.get(f'deaths_{u}_1', 0) for u in range(3)) if data else None
                    values[f'{label}_combat_deaths'] = sum(data.get(f'deaths_{u}_0', 0) for u in range(3)) if data else None
                    values[f'{label}_conversions_out'] = sum(data.get(f'conversionsOut_{u}', 0) for u in range(3)) if data else None
                    values[f'{label}_buildings_completed'] = sum(v for key,v in data.items() if key.startswith('completed_0_')) if data else None
                    values[f'{label}_upgrades_completed'] = sum(v for key,v in data.items() if key.startswith('completed_1_')) if data else None
                    values[f'{label}_meals'] = data.get('meals')
                teams.append(values)
        rows.append(row)
        if record['category'] != 'success': errors.append(row)
    missing = [dict(job=j['id'], kind=j['type'], labels=j['labels']) for j in source.manifest['jobs'] if j['id'] not in observed]
    support_rows = []
    for (build, generator, parameters), categories in support.items():
        success = categories.get('success', set())
        rejected = set().union(*(v for k, v in categories.items() if k != 'success'))
        support_rows.append(dict(build=build, generator=generator, **json.loads(parameters),
                                 successful_seeds=','.join(map(str, sorted(success))),
                                 rejected_seeds=','.join(map(str, sorted(rejected))),
                                 mixed_seed_outcomes=bool(success and rejected),
                                 categories=json.dumps({k: sorted(v) for k, v in categories.items()})))
    output.mkdir(parents=True, exist_ok=True)
    write_csv(output/'support.csv', support_rows)
    write_csv(output/'jobs.csv', rows); write_csv(output/'teams.csv', teams); write_csv(output/'failures.csv', errors)
    summary = dict(experiment=source.manifest['id'], planned=len(source.manifest['jobs']), observed=len(rows), accepted=sum(r['committed'] for r in rows),
                   missing=missing, categories=dict(Counter(r['category'] for r in rows)),
                   games=dict(Counter((r.get('termination') or 'missing') for r in rows if r['kind']=='game')))
    (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps({key:value for key,value in summary.items() if key!='missing'},indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory');parser.add_argument('--output', required=True)
    parser.add_argument('--attempt', action='append', help='explicit retained diagnostic attempt; never pooled with committed results')
    args=parser.parse_args();analyze(args.directory,Path(args.output),args.attempt)
