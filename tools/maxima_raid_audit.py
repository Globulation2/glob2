"""Measure offensive flag occupancy and fixed-window, team-wide outcomes.

Run with --log arm=stdout.log or --results arm=tournament-directory. All truth
fields are offline labels. Sampled occupancy is an estimate, not an event trace;
combat and harvest deltas are correlations, not kills attributable to a flag.
"""
import argparse
from bisect import bisect_left
from collections import defaultdict
import gzip
import json
from pathlib import Path
import statistics


def parse(lines):
    for line in lines:
        cells = line.rstrip('\n').split('\t')
        if len(cells) < 4 or cells[0] != 'MAXIMA_TELEMETRY':
            continue
        if cells[3] not in ('offense_sample', 'offense_outcome'):
            continue
        row = dict(tick=int(cells[1]), observer=int(cells[2]), event=cells[3])
        for cell in cells[4:]:
            key, value = cell.split('=', 1)
            try:
                row[key] = int(value)
            except ValueError:
                row[key] = value
        yield row


def analyze(rows, horizons=(2048, 4096)):
    samples = defaultdict(list)
    outcomes = defaultdict(list)
    episodes = {}
    totals = defaultdict(lambda: defaultdict(int))
    for row in rows:
        key = row['run'], row['observer']
        if row['event'] == 'offense_sample':
            samples[key].append(row)
        else:
            outcomes[(*key, row['team'])].append(row)
    for series in outcomes.values():
        series.sort(key=lambda r: r['game_tick'])
    for key, series in samples.items():
        series.sort(key=lambda r: r['tick'])
        for i, row in enumerate(series):
            if not row['flag_present'] or row['kind'] not in ('raid', 'siege'):
                continue
            identity = (*key, row['flag'], row['started_tick'],
                        row['target_since_tick'], row['kind'], row['enemy'], row['gid'])
            if identity not in episodes:
                episodes[identity] = {
                    'run': row['run'], 'arm': row['arm'], 'observer': row['observer'],
                    'flag': row['flag'], 'kind': row['kind'], 'enemy': row['enemy'],
                    'target_since_tick': row['target_since_tick'],
                    'first_sample_game_tick': row['game_tick'], 'arrival': None,
                }
            episode = episodes[identity]
            episode['last_sample_game_tick'] = row['game_tick']
            if row['on_site'] > 0 and episode['arrival'] is None:
                episode['arrival'] = row['game_tick']
                episode['arrival_delay_ai_ticks_upper'] = row['tick'] - row['target_since_tick']
                episode['targets_at_arrival'] = row['truth_ground_units'] + row['truth_buildings']
                episode['workers_at_arrival'] = row['truth_workers']
            aggregate = totals[row['arm'], row['kind']]
            aggregate['samples'] += 1
            # Last samples and discontinuous logs are censored. Otherwise carry
            # the sampled state to the next sample, including an idle sample.
            if i + 1 == len(series) or series[i+1]['tick'] - row['tick'] != 128:
                aggregate['unweighted_samples'] += 1
                continue
            duration = series[i+1]['game_tick'] - row['game_tick']
            if duration <= 0:
                raise ValueError('non-increasing game clock')
            empty = row['truth_ground_units'] + row['truth_buildings'] == 0
            arrived = row['on_site'] > 0
            aggregate['active_game_ticks_estimate'] += duration
            aggregate['empty_game_ticks_estimate'] += duration * empty
            aggregate['arrived_game_ticks_estimate'] += duration * arrived
            aggregate['arrived_empty_game_ticks_estimate'] += duration * arrived * empty
            aggregate['on_site_warrior_ticks_estimate'] += duration * row['on_site']
            aggregate['empty_on_site_warrior_ticks_estimate'] += duration * row['on_site'] * empty
            aggregate['worker_empty_game_ticks_estimate'] += duration * (row['truth_workers'] == 0)

    def delta(run, observer, team, start, horizon):
        series = outcomes.get((run, observer, team), [])
        ticks = [row['game_tick'] for row in series]
        first = bisect_left(ticks, start)
        last = bisect_left(ticks, start + horizon)
        if first == len(series) or ticks[first] != start or last == len(series):
            return None
        # A coarse sample endpoint is recorded honestly, not interpolated into
        # fake exact combat events. Reject gaps longer than two sample periods.
        if ticks[last] - start - horizon > 256:
            return None
        fields = ('worker_combat_deaths', 'warrior_combat_deaths',
                  'melee_unit_damage', 'melee_building_damage', 'wheat_harvested')
        result = {field: series[last][field] - series[first][field] for field in fields}
        if any(value < 0 for value in result.values()):
            raise ValueError('cumulative counters reset inside an outcome window')
        result['actual_game_ticks'] = ticks[last] - start
        return result

    for episode in episodes.values():
        aggregate = totals[episode['arm'], episode['kind']]
        aggregate['episodes'] += 1
        aggregate['episodes_with_observed_arrival'] += episode['arrival'] is not None
        episode['outcomes'] = {}
        if episode['arrival'] is None:
            continue
        aggregate['empty_observed_arrivals'] += episode['targets_at_arrival'] == 0
        for horizon in horizons:
            own = delta(episode['run'], episode['observer'], episode['observer'], episode['arrival'], horizon)
            enemy = delta(episode['run'], episode['observer'], episode['enemy'], episode['arrival'], horizon)
            episode['outcomes'][str(horizon)] = (
                {'own': own, 'enemy': enemy} if own is not None and enemy is not None else None)
    summary = []
    for (arm, kind), values in sorted(totals.items()):
        row = dict(arm=arm, kind=kind, **values)
        for name, numerator, denominator in (
            ('empty_time_fraction', 'empty_game_ticks_estimate', 'active_game_ticks_estimate'),
            ('arrived_empty_time_fraction', 'arrived_empty_game_ticks_estimate', 'arrived_game_ticks_estimate'),
        ):
            row[name] = values[numerator] / values[denominator] if values[denominator] else None
        row['arrival_windows'] = {}
        group = [episode for episode in episodes.values()
                 if episode['arm'] == arm and episode['kind'] == kind and episode['arrival'] is not None]
        for horizon in horizons:
            complete = [episode['outcomes'][str(horizon)] for episode in group
                        if episode['outcomes'][str(horizon)] is not None]
            window = {'complete': len(complete), 'censored': len(group) - len(complete)}
            for label, side, field in (
                ('enemy_worker_combat_deaths', 'enemy', 'worker_combat_deaths'),
                ('own_warrior_combat_deaths', 'own', 'warrior_combat_deaths'),
                ('own_melee_building_damage', 'own', 'melee_building_damage'),
                ('own_melee_unit_damage', 'own', 'melee_unit_damage'),
                ('enemy_wheat_harvested', 'enemy', 'wheat_harvested'),
            ):
                window[label + '_mean'] = statistics.mean(item[side][field] for item in complete) if complete else None
            row['arrival_windows'][str(horizon)] = window
        summary.append(row)
    return summary, list(episodes.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', action='append', default=[], metavar='ARM=PATH')
    parser.add_argument('--results', action='append', default=[], metavar='ARM=PATH')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for specification in args.log:
        arm, path = specification.split('=', 1)
        opener = gzip.open if path.endswith('.gz') else open
        with opener(path, 'rt') as stream:
            rows.extend(dict(row, arm=arm, run=arm+':'+str(Path(path).resolve())) for row in parse(stream))
    for specification in args.results:
        from tools.tournaments.results import Results
        arm, path = specification.split('=', 1)
        source = Results(path)
        for record in source:
            if record['category'] != 'success' or record['job']['type'] != 'game':
                continue
            with source.open_artifact(record, 'stdout.log') as stream:
                rows.extend(dict(row, arm=arm, run=arm+':'+record['job']['id']) for row in parse(stream))
    if not rows:
        parser.error('no offense audit records; run the instrumented build with --telemetry maxima-recon')
    summary, episodes = analyze(rows)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (args.output / 'episodes.json').write_text(json.dumps(episodes, indent=2) + '\n')
    with gzip.open(args.output / 'samples.jsonl.gz', 'wt') as stream:
        for row in rows:
            stream.write(json.dumps(row, sort_keys=True) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
