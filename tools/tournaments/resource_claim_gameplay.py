"""Temporary paired claim gameplay endpoints from existing exact team counters.

The explicit frozen design identifies selected teams, observation windows, policy,
initial state and independent pairing blocks. No winner selection is performed.
"""
import argparse
from collections import defaultdict
from contextlib import contextmanager
import json
from pathlib import Path
import re
import shutil
import statistics

from .claim_policy import attested_policy_digest, policy_digest
from .common import atomic_json, digest
from .fairness_statistics import bootstrap, MIN_MAPS_FOR_INTERVAL
from .results import Results

MAX_GAMES = 4096
MAX_LOG_BYTES = 128 * 1024 * 1024
MAX_LINE_BYTES = 1024 * 1024
FLOWS = ('harvested', 'delivered', 'withdrawn', 'transferredIn', 'transferredOut', 'repairDelivered')


class BoundedResults(Results):
    def __iter__(self):
        for job in self.manifest['jobs']:
            path = self.root / 'results' / (job['id'] + '.json')
            if path.exists() and path.stat().st_size > 32*1024*1024:
                raise ValueError('committed result exceeds analysis allowance')
        yield from super().__iter__()

    @contextmanager
    def open_artifact(self, record, name, mode='rt', verify=True):
        meta = next(a for a in record['artifacts'] if a['path'] == name)
        if meta['bytes'] > MAX_LOG_BYTES:
            raise ValueError('compressed log exceeds analysis allowance')
        with super().open_artifact(record, name, mode, verify) as stream:
            def lines():
                total = 0
                while True:
                    line = stream.readline(MAX_LINE_BYTES + 1)
                    if not line:
                        return
                    size = len(line.encode('utf-8'))
                    total += size
                    if size > MAX_LINE_BYTES or total > MAX_LOG_BYTES:
                        raise ValueError('decoded log exceeds analysis allowance')
                    yield line
            yield lines()


def resolved_policy_identity(resolved):
    if resolved.get('active_claim_policy') == 'experimental':
        return attested_policy_digest(resolved)
    mode = resolved.get('active_claim_policy')
    metadata = resolved.get('material_destinations_enabled')
    if mode not in ('disabled', 'prototype-stationary-v1') or type(metadata) is not bool:
        raise ValueError('engine did not attest claim mode and metadata setting')
    if mode != 'disabled' and not metadata:
        raise ValueError('active claims require destination metadata')
    return digest({'active_claim_policy':mode, 'material_destinations_enabled':metadata})


def initial_state_identity(job):
    """Frozen setup identity excludes only explicit claim policy/diagnostic switches."""
    config = {k:v for k,v in job['config'].items() if not k.startswith('resource_claims_')}
    return digest({'inputs':job.get('inputs', {}), 'seeds':job.get('seeds', {}), 'config':config})


def validate_design(design):
    if design.get('version') != 1 or not isinstance(design.get('games'), dict) or not 1 <= len(design['games']) <= MAX_GAMES:
        raise ValueError('invalid bounded gameplay design')
    if not isinstance(design.get('source_experiment'), str) or not design['source_experiment']:
        raise ValueError('source experiment identity required')
    seen = set()
    partitions = {}
    for job, row in design['games'].items():
        for key in ('cohort', 'initial_state_identity', 'pair_block', 'policy_digest'):
            if not isinstance(row.get(key), str) or not 1 <= len(row[key]) <= 256:
                raise ValueError('missing frozen identity: ' + key)
        if row.get('partition') not in ('discovery', 'holdout'):
            raise ValueError('explicit discovery/holdout partition required')
        if not isinstance(row.get('teams'), list) or not row['teams'] or len(set(row['teams'])) != len(row['teams']) or any(type(t) is not int or not 0 <= t < 32 for t in row['teams']):
            raise ValueError('explicit unique selected teams required')
        if any(type(row.get(k)) is not int for k in ('window_start', 'window_end')) or not 0 <= row['window_start'] < row['window_end'] <= 0xffffffff:
            raise ValueError('invalid explicit observation window')
        identity = row['cohort'], row['initial_state_identity'], row['policy_digest']
        if identity in seen:
            raise ValueError('duplicate initial state/policy observation')
        seen.add(identity)
        # Rotations or repeated starting states cannot leak across partitions.
        for key in (('block', row['cohort'], row['pair_block']), ('state', row['cohort'], row['initial_state_identity'])):
            if key in partitions and partitions[key] != row['partition']:
                raise ValueError('paired block leaks across discovery/holdout')
            partitions[key] = row['partition']


def measure_frames(first, last, start, end):
    """Exact cumulative deltas; gauges remain explicitly endpoint observations."""
    for frame, tick in ((first, start), (last, end)):
        if frame.get('tick') != tick:
            raise ValueError('exact window boundary frame missing')
        for key in ('coverage_start', 'extended_coverage_start', 'labour_coverage_start'):
            if type(frame.get(key)) is not int or not 0 <= frame[key] <= start:
                raise ValueError('incomplete counter coverage: ' + key)
    def delta(key):
        a, b = first.get(key), last.get(key)
        if type(a) is not int or type(b) is not int or not 0 <= a <= b:
            raise ValueError('missing/nonmonotonic cumulative field: ' + key)
        return b-a
    labour = [delta('labour_' + str(i)) for i in range(11)]
    filling = [[delta(f'filling_{j}_{p}') for p in range(4)] for j in range(4)]
    result = {'transport_ticks': sum(labour)+sum(map(sum, filling)),
              'pursuit_ticks': sum(row[0] for row in filling),
              'harvest_ticks': sum(row[1] for row in filling),
              'return_ticks': sum(row[2] for row in filling), 'no_inn_ticks': labour[3]}
    for prefix in FLOWS:
        keys = sorted(k for k in first if re.fullmatch(prefix+r'_\d+', k))
        if not keys or set(keys) != {k for k in last if re.fullmatch(prefix+r'_\d+', k)}:
            raise ValueError('missing/changing material flow catalog: ' + prefix)
        for key in keys:
            result[key] = delta(key)
        result[prefix] = sum(result[k] for k in keys)
    deaths = sorted(k for k in first if re.fullmatch(r'deaths_\d+_1', k))
    if not deaths or set(deaths) != {k for k in last if re.fullmatch(r'deaths_\d+_1', k)}:
        raise ValueError('missing/changing starvation catalog')
    result['starvation_deaths'] = sum(delta(k) for k in deaths)
    return result


def endpoint_metrics(counters, duration):
    def ratio(a, b, factor=1):
        return factor*a/b if b else None
    ticks, delivered = counters['transport_ticks'], counters['delivered']
    values = dict(counters)
    values.update(delivered_per_1000_transport_ticks=ratio(delivered, ticks, 1000),
                  pursuit_ticks_per_delivery=ratio(counters['pursuit_ticks'], delivered),
                  harvested_per_1000_transport_ticks=ratio(counters['harvested'], ticks, 1000),
                  no_inn_transport_fraction=ratio(counters['no_inn_ticks'], ticks),
                  harvest_transport_fraction=ratio(counters['harvest_ticks'], ticks),
                  return_transport_fraction=ratio(counters['return_ticks'], ticks),
                  mean_transport_population=ticks/duration)
    return values


def read_game(source, record, design):
    if record.get('category') != 'success':
        raise ValueError('unsuccessful committed game')
    if initial_state_identity(record['job']) != design.get('setup_digest', design['initial_state_identity']):
        raise ValueError('frozen initial setup differs from committed job')
    labeled = record['job'].get('labels', {}).get('initial_state_identity')
    if labeled is not None and labeled != design['initial_state_identity']:
        raise ValueError('frozen initial state label differs from committed job')
    result = record.get('result') or {}
    resolved = result.get('resolved', {})
    if resolved_policy_identity(resolved) != design['policy_digest']:
        raise ValueError('frozen policy digest differs from actual engine config')
    config = record['job']['config']
    if 'resource_claims_active' in config and resolved.get('active_claim_policy') != config['resource_claims_active']:
        raise ValueError('requested active mode differs from engine result')
    if 'material_destinations' in config and resolved.get('material_destinations_enabled') is not config['material_destinations']:
        raise ValueError('requested destination mode differs from engine result')
    requested = config.get('resource_claims_policy')
    if requested is not None and policy_digest(requested) != design['policy_digest']:
        raise ValueError('requested policy differs from frozen policy')
    if type(result.get('ticks')) is not int or result['ticks'] < design['window_end']:
        raise ValueError('game ended before frozen observation window')
    frames = {}
    for row in source.telemetry(record):
        if 'error' in row:
            raise ValueError('malformed/unavailable telemetry: ' + row['error'])
        if row['family'] != 'gameplay':
            continue
        value = row['values']
        key = value.get('team'), value.get('tick')
        if key[0] in design['teams'] and key[1] in (design['window_start'], design['window_end']):
            # Final/history duplication is allowed only when all measured fields agree.
            clean = {k:v for k,v in value.items() if k != 'final'}
            if key in frames and frames[key] != clean:
                raise ValueError('conflicting duplicate boundary frame')
            frames[key] = clean
    counters = defaultdict(int)
    gauges = {}
    for team in design['teams']:
        first = frames.get((team, design['window_start']), {})
        last = frames.get((team, design['window_end']), {})
        for key, value in measure_frames(first, last, design['window_start'], design['window_end']).items():
            counters[key] += value
        gauges[team] = {k:v for k,v in last.items() if k in ('hungry', 'critical', 'feeding') or re.fullmatch(r'(stock|carried|lowFood)_\d+(?:_\d+)?', k)}
    return {'metrics': endpoint_metrics(counters, design['window_end']-design['window_start']),
            'endpoint_gauges': gauges,
            'final_team_state': [{k:t.get(k) for k in ('team','outcome','alive','units','workers','buildings','sites','eliminated_tick')} for t in result.get('teams', []) if t.get('team') in design['teams']],
            'actual_outcome': {k:result.get(k) for k in ('winning_teams', 'winning_alliances', 'termination', 'ticks')},
            # Preserve exact area/coverage/model; ordinary win-probability analysis remains authoritative.
            'win_probability': [{'team':t.get('team', t.get('id')), 'value':t.get('win_probability')} for t in result.get('teams', [])]}


def paired_report(games, baseline, *, draws=1000, seed=1):
    if not 1 <= draws <= 10000:
        raise ValueError('bootstrap draws outside allowance')
    indexed = defaultdict(dict)
    for game in games:
        d = game['design']
        indexed[d['cohort'], d['partition'], d['initial_state_identity']][d['policy_digest']] = game
    grouped = defaultdict(list)
    missing = []
    for (cohort, partition, identity), variants in indexed.items():
        for policy, candidate in variants.items():
            if policy == baseline:
                continue
            control = variants.get(baseline)
            if control is None:
                missing.append({'game':candidate['game'], 'reason':'missing matched baseline'})
                continue
            a, b = control['design'], candidate['design']
            if any(a[k] != b[k] for k in ('pair_block', 'window_start', 'window_end', 'teams')):
                raise ValueError('paired window/team/block mismatch')
            for metric in sorted(set(control['metrics']) | set(candidate['metrics'])):
                x, y = control['metrics'].get(metric), candidate['metrics'].get(metric)
                grouped[cohort, partition, policy, metric].append((a['pair_block'], None if x is None or y is None else y-x))
    output = []
    for (cohort, partition, policy, metric), observations in sorted(grouped.items()):
        blocks = defaultdict(list)
        for block, value in observations:
            if value is not None:
                blocks[block].append(value)
        units = list(blocks.values())
        mean = lambda sample: statistics.fmean(v for block in sample for v in block)
        output.append({'cohort':cohort, 'partition':partition, 'policy_digest':policy, 'baseline_digest':baseline,
                       'metric':metric, 'paired_games':sum(map(len, units)), 'independent_blocks':len(units),
                       'missing_denominator_pairs':sum(v is None for _,v in observations),
                       'mean_delta':mean(units) if units else None,
                       'ci95':bootstrap(units, mean, draws, seed) if len(units)>=MIN_MAPS_FOR_INTERVAL else None})
    return output, missing


def analyze(directory, design, baseline, *, draws=1000, seed=1):
    validate_design(design)
    if baseline not in {r['policy_digest'] for r in design['games'].values()}:
        raise ValueError('baseline policy absent from frozen design')
    if not 1 <= draws <= 10000:
        raise ValueError('bootstrap draws outside allowance')
    if (Path(directory)/'experiment.json').stat().st_size > 64*1024*1024:
        raise ValueError('source manifest exceeds analysis allowance')
    source = BoundedResults(directory)
    if source.manifest.get('id') != design['source_experiment']:
        raise ValueError('source experiment identity mismatch')
    if len(source.manifest['jobs']) > MAX_GAMES:
        raise ValueError('source cohort exceeds analysis allowance')
    planned = {j['id'] for j in source.manifest['jobs']}
    if set(design['games'])-planned:
        raise ValueError('design references games outside source cohort')
    games, missing, seen = [], [], set()
    for record in source:
        job = record['job']['id']
        if job not in design['games']:
            continue
        seen.add(job)
        try:
            games.append({'game':job, 'design':design['games'][job], **read_game(source, record, design['games'][job])})
        except (ValueError, KeyError, OSError, StopIteration) as error:
            missing.append({'game':job, 'reason':str(error)})
    missing.extend({'game':job, 'reason':'missing committed result'} for job in sorted(set(design['games'])-seen))
    comparisons, unmatched = paired_report(games, baseline, draws=draws, seed=seed)
    return {'version':1, 'source_experiment':source.manifest.get('id'), 'baseline_digest':baseline,
            'games':games, 'comparisons':comparisons, 'missing':missing+unmatched,
            'limits':['Pursuit time includes stationary waits; no actual path length or wasted-movement inference.',
                      'Harvest distance and endpoint gauges are not integrated route or shortage measurements.',
                      'Delivery includes supplier transfers; retain separate harvested/withdrawn/transferred counters.',
                      'Delivery counts accepted capacity-clamped units; rejected attempts and spillage are unavailable in this printed schema.',
                      'Intervals resample independent pairing blocks; seat rotations remain clustered. No interval below five blocks.',
                      'Exact frozen windows require both boundary frames; early endings are missing, never zero.',
                      'Integrated win probability remains raw per-team area/coverage/model and actual outcomes: balance diagnostics, not all-team claim effectiveness (all-team mean is constant).']}


def build_design(directory, *, teams=None):
    """Retrospective availability-only common prefix, never selected by yield/outcome.

    Every planned policy must be present. Full observed windows are descriptive,
    separate from paired comparisons; early victories can truncate common exposure.
    """
    if (Path(directory)/'experiment.json').stat().st_size > 64*1024*1024:
        raise ValueError('source manifest exceeds analysis allowance')
    source = BoundedResults(directory)
    if not 1 <= len(source.manifest['jobs']) <= MAX_GAMES:
        raise ValueError('source cohort exceeds analysis allowance')
    groups = defaultdict(list)
    available, excluded, whole, planned = {}, [], [], {}
    for job in source.manifest['jobs']:
        if job['type'] != 'game':
            continue
        label = job.get('labels', {})
        try:
            identity = label['initial_state_identity']
            partition = label['discovery_split']
            cohort = source.manifest['labels']['source_cohort']
            policy = label['policy_digest']
            selected = list(teams) if teams is not None else list(range(len(job['config']['players'])))
            row = dict(cohort=cohort, partition=partition, initial_state_identity=identity,
                       setup_digest=initial_state_identity(job), pair_block=label.get('block',identity),
                       policy_digest=policy, teams=selected, window_start=0, window_end=1)
            validate_design({'version':1,'source_experiment':source.manifest['id'],'games':{job['id']:row}})
        except (KeyError, ValueError, TypeError) as error:
            raise ValueError('live plan lacks frozen gameplay identities: '+str(error)) from error
        groups[cohort, partition, identity].append((job['id'], row))
        planned[job['id']]=(job,row)
    seen=set()
    for record in source:
        identifier=record['job']['id']
        if identifier not in planned:
            continue
        job,row=planned[identifier];selected=row['teams'];policy=row['policy_digest'];seen.add(identifier)
        try:
            if record['category'] != 'success':
                raise ValueError('unsuccessful committed game')
            if resolved_policy_identity(record['result']['resolved']) != policy:
                raise ValueError('frozen policy differs from engine result')
            ticks = {team:{} for team in selected}
            for event in source.telemetry(record):
                if 'error' in event:
                    raise ValueError('malformed/unavailable telemetry: '+event['error'])
                if event['family'] != 'gameplay':
                    continue
                value = event['values']; team=value.get('team');tick=value.get('tick')
                if team not in ticks:
                    continue
                if type(tick) is not int or not 0 <= tick <= record['result']['ticks']:
                    raise ValueError('invalid gameplay frame tick')
                coverage=[value.get(k) for k in ('coverage_start','extended_coverage_start','labour_coverage_start')]
                if any(type(v) is not int or not 0 <= v <= tick for v in coverage):
                    raise ValueError('unavailable gameplay coverage')
                if len(ticks[team]) >= 8192 and tick not in ticks[team]:
                    raise ValueError('frame inventory exceeds allowance')
                floor=max(coverage)
                if tick in ticks[team] and ticks[team][tick]!=floor:
                    raise ValueError('conflicting duplicate coverage')
                ticks[team][tick]=floor
            common=set.intersection(*(set(ticks[t]) for t in selected))
            if not common:
                raise ValueError('no complete selected-team boundary frames')
            coverage=max(ticks[t][tick] for t in selected for tick in common)
            common=sorted(tick for tick in common if tick>=coverage)
            if len(common)<2:
                raise ValueError('fewer than two completely covered boundary frames')
            available[job['id']]=common
            full=row|{'window_start':common[0],'window_end':common[-1]}
            whole.append({'game':job['id'],'design':full,**read_game(source,record,full),
                          'terminal_frame_observed':common[-1]==record['result']['ticks']})
        except (KeyError, ValueError, OSError, StopIteration) as error:
            available.pop(job['id'],None)
            excluded.append({'game':job['id'],'reason':str(error)})
    excluded.extend({'game':job,'reason':'missing committed result'} for job in sorted(set(planned)-seen))
    design={'version':1,'source_experiment':source.manifest['id'],'games':{},
            'window_rule':'retrospective earliest/latest shared observed covered boundaries across ALL planned policies and selected teams; uses availability only',
            'window_limitation':'Early termination shortens matched exposure; full observed games are separate descriptive summaries, not equal-duration paired effects.'}
    for key, variants in groups.items():
        if any(job not in available for job,_ in variants):
            for job,_ in variants:
                excluded.append({'game':job,'reason':'pair incomplete: at least one planned policy lacks covered observations'})
            continue
        if len({row['policy_digest'] for _,row in variants}) != len(variants):
            raise ValueError('duplicate frozen state/policy in live plan')
        if len({row['setup_digest'] for _,row in variants}) != 1:
            raise ValueError('paired frozen setup differs beyond policy switches')
        common=set.intersection(*(set(available[job]) for job,_ in variants))
        if len(common)<2:
            excluded.extend({'game':job,'reason':'no positive shared observed prefix'} for job,_ in variants)
            continue
        start,end=min(common),max(common)
        for job,row in variants:
            design['games'][job]=row|{'window_start':start,'window_end':end}
    if design['games']:
        validate_design(design)
    return {'design':design,'whole_observed_games':whole,'excluded':excluded,
            'whole_game_interpretation':'Different-duration descriptive totals and normalized transport-time rates; retain per-team win-probability area and actual outcomes separately.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--design', type=Path);mode.add_argument('--build-design', type=Path)
    parser.add_argument('--teams', help='comma-separated selected team IDs; builder default all planned players')
    parser.add_argument('--baseline');parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--draws', type=int, default=1000);parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()
    if args.design and args.design.stat().st_size > 4*1024*1024:
        raise ValueError('design exceeds allowance')
    if not args.output.parent.exists() or shutil.disk_usage(args.output.parent).free < 64*1024*1024:
        raise ValueError('output parent missing or free-space reserve unavailable')
    if args.build_design:
        result = build_design(args.results, teams=[int(v) for v in args.teams.split(',')] if args.teams else None)
    else:
        if not args.baseline:
            parser.error('--baseline is required with --design')
        result = analyze(args.results, json.loads(args.design.read_text()), args.baseline, draws=args.draws, seed=args.seed)
    if len(json.dumps(result, separators=(',', ':')).encode()) > 32*1024*1024:
        raise ValueError('report exceeds32MiB allowance; split frozen cohorts')
    if args.build_design:
        if not args.build_design.parent.exists() or shutil.disk_usage(args.build_design.parent).free < 64*1024*1024:
            raise ValueError('design output reserve unavailable')
        atomic_json(args.build_design, result['design'])
    atomic_json(args.output, result)


if __name__ == '__main__':
    main()
