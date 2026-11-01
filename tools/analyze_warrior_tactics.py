#!/usr/bin/env python3
"""Offline warrior duty and paired, terrain-clustered campaign comparisons.

Input is an experiment manifest with runs containing id, arm, cluster, team,
directory. A cluster is one exported terrain seed, keeping seat rotations and
simulation seeds together. Missing runs and truncated windows are errors, never
zero observations. Enemy strength is offline truth, not Maxima's estimate.
"""
import argparse
from collections import Counter, defaultdict
import csv
import gzip
import json
from pathlib import Path
import random
import statistics

STATES = ('fighting', 'to_flag', 'at_flag', 'to_food', 'to_heal', 'training',
          'inside_service', 'idle')


def open_text(path):
    path = Path(path)
    return path.open() if path.exists() else gzip.open(str(path)+'.gz', 'rt')


def read_csv(path):
    with open_text(path) as stream:
        yield from csv.DictReader(stream)


def maxima_events(path, team):
    with open_text(path) as stream:
        for line in stream:
            if not line.startswith('MAXIMA_TELEMETRY\t'):
                continue
            fields = line.rstrip().split('\t')
            if int(fields[2]) != team:
                continue
            values = dict(part.split('=', 1) for part in fields[4:] if '=' in part)
            yield int(values['game_tick']), fields[3], values


def medical_contact_summary(episodes, offense_flags):
    """Follow clinical departures until combat, another flag, loss or censoring.

    Input contains one team's episodes. Reassignment alone is not a return;
    repeated medical visits before contact retain the original departure time.
    """
    previous, active = {}, {}
    outcomes = Counter()
    durations = []
    departures = nonclinical = 0
    for row in episodes:
        identity = row['gid'], row['generation']
        state, purpose = row['state'], row['service_purpose']
        if state == 'training':
            state = {'17': 'to_heal', '18': 'to_food'}.get(purpose, state)
        flag = row['flag_gid'], row['flag_generation']
        attached = flag in offense_flags
        prior = previous.get(identity)
        leaving = (prior and (prior['flag_gid'], prior['flag_generation']) in offense_flags
                   and row['flag_gid'] == '65535')
        clinical = (state in ('to_food', 'to_heal')
                    or (state == 'inside_service' and purpose in ('17', '18')))
        if leaving and not clinical and state in ('training', 'inside_service'):
            nonclinical += 1
        if leaving and clinical and identity not in active:
            active[identity] = int(row['start'])
            departures += 1
        if identity in active:
            if attached and state == 'fighting':
                outcomes['returned_to_combat'] += 1
                durations.append(int(row['start']) - active.pop(identity))
            elif row['flag_gid'] != '65535' and not attached:
                outcomes['assigned_other_flag'] += 1
                active.pop(identity)
            elif row['end_reason'] in ('disappeared', 'converted', 'right_censored'):
                outcomes[row['end_reason']] += 1
                active.pop(identity)
        previous[identity] = row
    # A terminal zero-length state is omitted from episode files. It cannot
    # establish positive-time combat following the preceding interval.
    outcomes['right_censored'] += len(active)
    return dict(strict_medical_departures=departures,
                nonclinical_service_departures=nonclinical,
                strict_medical_contact_outcomes=dict(outcomes),
                strict_medical_contact_fraction=outcomes['returned_to_combat']/departures
                    if departures else None,
                strict_medical_contact_median=statistics.median(durations) if durations else None)


def summarize(run, root):
    directory = root / run['directory']
    result = json.loads((directory / 'result.json').read_text())
    team = int(run['team'])
    events = list(maxima_events(directory / 'stdout.log', team))
    offense_flags = {(v['flag_gid'], v['flag_generation']) for _, e, v in events if e == 'offense_flag'}
    forward_providers = {(v['building_gid'], v['building_generation'])
                         for _, e, v in events if e == 'forward_support_provider'}
    first_providers = {}
    for _, event, values in events:
        if event == 'forward_support_provider':
            first_providers.setdefault((values['building_gid'],values['building_generation']),values)
    own_result = next(t for t in result['teams'] if t['team'] == team)
    allies = own_result.get('allies_mask',1 << team)
    enemy_teams = {t['team'] for t in result['teams'] if not (allies & (1 << t['team']))}
    window_totals = Counter()
    snapshots = defaultdict(dict)
    first, last = None, None
    for row in read_csv(directory / 'warrior-windows.csv'):
        n = int(row['team']); end = int(row['end']); start = int(row['start'])
        snapshots[end][n] = row
        if n != team:
            continue
        if first is None:
            first = start
        if last is not None and start != last:
            raise ValueError(f"Noncontiguous windows in {directory}")
        last = end
        for name in (*STATES, 'moved_ticks', 'experienced_ticks'):
            window_totals[name] += int(row[name])
    if first is None or last is None or last <= first:
        raise ValueError(f"No measured warrior timeline in {directory}")
    if last != result['ticks']:
        raise ValueError(f"Warrior timeline does not reach the terminal tick in {directory}")
    # This is an annotation, not a causal subgroup: a treatment may itself
    # change whether advantage is reached. Report it beside unconditional duty.
    dominance_start = None
    dominance_since = None
    dominant_ticks = 0
    combined_since = None
    combined_advantage_start = None
    for tick, teams in sorted(snapshots.items()):
        own = teams[team]
        rivals = [float(t['combat_power']) for n, t in teams.items()
                  if n in enemy_teams and int(t['alive'])]
        superior = (int(own['alive']) and int(own['warriors']) >= 12
                    and rivals and float(own['combat_power']) >= 1.5 * max(rivals))
        if superior:
            if dominance_since is None:
                dominance_since = tick
            if tick - dominance_since >= 4096:
                if dominance_start is None:
                    dominance_start = dominance_since
                dominant_ticks += tick - int(own['start'])
        else:
            dominance_since = None
        # Stronger FFA sensitivity check: a lead over the strongest rival is
        # different from a lead over the combined remaining enemy melee power.
        combined = (int(own['alive']) and int(own['warriors']) >= 12 and rivals
                    and float(own['combat_power']) >= 1.25 * sum(rivals))
        if combined:
            if combined_since is None:
                combined_since = tick
            if tick-combined_since >= 4096 and combined_advantage_start is None:
                combined_advantage_start = combined_since
        else:
            combined_since = None
    # Death and cancellation remain censored journeys, not fast arrivals.
    previous = {}
    travel_ticks = []
    travel_outcomes = Counter()
    offense_journeys = Counter()
    offense_arrivals = []
    redirected_before_arrival_ticks = 0
    medical_after_flag = Counter()
    observed = Counter()
    normalized = Counter()
    offense_duty = Counter()
    forward_service = Counter()
    forward_recipients = set()
    used_forward_providers = set()
    forward_assignees = set()
    assigned_forward_providers = set()
    recovery_sessions = {}
    recovery_outcomes = Counter()
    recovery_duty = Counter()
    completed_recovery_times = []
    for row in read_csv(directory / 'warrior-episodes.csv'):
        if int(row['team']) != team:
            continue
        identity = int(row['gid']), int(row['generation'])
        start, end = int(row['start']), int(row['end'])
        ticks = int(row['ticks'])
        if ticks != end - start or ticks <= 0:
            raise ValueError(f"Invalid warrior interval in {directory}")
        observed[row['state']] += ticks
        # The initial collector called a service-bound, medically recovered unit
        # "training": ACT_UPGRADING is also used for food and healing visits.
        # Its recorded destination disambiguates old sidecars without reruns.
        state = row['state']
        if state == 'training':
            state = {'17': 'to_heal', '18': 'to_food'}.get(row['service_purpose'], state)
        normalized[state] += ticks
        attached_offense = (row['flag_gid'], row['flag_generation']) in offense_flags
        previous_offense = (row['previous_flag_gid'], row['previous_flag_generation']) in offense_flags
        provider = row['service_gid'], row['service_generation']
        if provider in forward_providers:
            purpose = {'17': 'healing', '18': 'feeding'}.get(row['service_purpose'], 'other')
            forward_service[purpose+'_'+state] += ticks
            forward_assignees.add(identity)
            assigned_forward_providers.add(provider)
            # Selecting a provider does not establish that the unit reached it.
            # InsideService includes entering, occupancy and exiting; it still
            # does not prove a particular meal or healing amount was delivered.
            if state == 'inside_service':
                forward_recipients.add(identity)
                used_forward_providers.add(provider)
        if attached_offense:
            offense_duty[state] += ticks
        if previous_offense and state in ('to_food', 'to_heal', 'training', 'inside_service'):
            offense_duty['recovery_'+state] += ticks
        prior = previous.get(identity)
        if prior and int(prior['end']) != start:
            raise ValueError(f"Noncontiguous warrior episodes in {directory}")
        # Follow an actual medical departure through service and reassignment,
        # rather than calling every later visit with a remembered flag a sortie.
        if (prior and (prior['flag_gid'], prior['flag_generation']) in offense_flags
                and row['flag_gid'] == '65535'
                and state in ('to_food', 'to_heal', 'inside_service', 'training')):
            recovery_sessions[identity] = start
        if identity in recovery_sessions:
            if row['flag_gid'] != '65535':
                outcome = 'returned_to_offense' if attached_offense else 'assigned_other_flag'
                recovery_outcomes[outcome] += 1
                if attached_offense:
                    completed_recovery_times.append(start-recovery_sessions[identity])
                del recovery_sessions[identity]
            else:
                recovery_duty[state] += ticks
                if row['end_reason'] in ('converted', 'disappeared', 'right_censored'):
                    recovery_outcomes[row['end_reason']] += 1
                    del recovery_sessions[identity]
        if row['previous_flag_gid'] != '65535' and state in ('to_food', 'to_heal', 'training', 'inside_service'):
            medical_after_flag[state] += ticks
        previous[identity] = row
    for row in read_csv(directory / 'warrior-journeys.csv'):
        if int(row['team']) != team:
            continue
        arrival, start = int(row['arrival']), int(row['start'])
        if (row['flag_gid'], row['flag_generation']) in offense_flags:
            if arrival >= 0:
                offense_journeys['arrived'] += 1
                if row['left_censored'] != '1':
                    offense_arrivals.append(arrival-start)
            else:
                offense_journeys[row['end_reason']] += 1
                if row['end_reason'] == 'redirected':
                    redirected_before_arrival_ticks += int(row['end'])-start
        if arrival >= 0:
            travel_outcomes['arrived'] += 1
            if row['left_censored'] != '1':
                travel_ticks.append(arrival-start)
        else:
            travel_outcomes[row['end_reason']] += 1
    if any(observed[s] != window_totals[s] for s in STATES):
        raise ValueError(f"Episode/window warrior accounting disagrees in {directory}")
    if recovery_sessions:
        # A state change on the terminal tick closes the preceding positive
        # interval as "changed"; the new zero-length episode is not emitted.
        # There is no positive-time return observation before this boundary.
        if any(int(previous[identity]['end']) != last for identity in recovery_sessions):
            raise ValueError(f"Unterminated medical departure in {directory}")
        recovery_outcomes['right_censored'] += len(recovery_sessions)
    for state in STATES:
        window_totals[state] = normalized[state]
    finishes = Counter()
    target_switches = 0
    previous_team = None
    live_enemy_switches = 0
    eliminated_at = {t['team']: t['eliminated_tick'] for t in result['teams']}
    for tick, event, fields in events:
        if event == 'mission_finished':
            finishes[fields['reason']] += 1
        if event == 'offense_state' and fields['kind'] != 'none' and int(fields['flags']) > 0:
            target = int(fields['target_team'])
            if previous_team is not None and target != previous_team:
                target_switches += 1
                death = eliminated_at.get(previous_team, -1)
                if death < 0 or tick < death:
                    live_enemy_switches += 1
            previous_team = target
    warrior_ticks = sum(window_totals[s] for s in STATES)
    denominator = warrior_ticks or None
    offense_ticks = sum(offense_duty[s] for s in STATES)
    last_own = snapshots[last][team]
    def counter_delta(column):
        initial = snapshots.get(first, {}).get(team)
        if (not initial or int(initial['start']) != int(initial['end'])
                or initial.get(column) in (None, '') or last_own.get(column) in (None, '')):
            return None
        delta = int(last_own[column]) - int(initial[column])
        if delta < 0:
            raise ValueError(f"Decreasing cumulative attrition counter in {directory}")
        return delta
    attrition = {name: counter_delta(name) for name in (
        'melee_class_combat_deaths', 'melee_class_starvation_deaths',
        'melee_class_conversions_in', 'melee_class_conversions_out')}
    attrition_rates = {name+'_per_warrior_tick': value/denominator
                      if value is not None and denominator else None
                      for name,value in attrition.items() if name != 'melee_class_conversions_in'}
    # Keep continuation damage unavailable here: a full-game endpoint count and
    # a continuation delta are different estimands. Fresh games start at zero.
    damage = int(last_own['melee_building_damage']) if first == 0 else None
    eliminated = [t['eliminated_tick'] for t in result['teams']
                  if t['team'] in enemy_teams and t['eliminated_tick'] >= 0]
    after = [tick for tick in eliminated if dominance_start is not None and tick >= dominance_start]
    clinical = medical_contact_summary(
        (row for row in read_csv(directory / 'warrior-episodes.csv') if int(row['team']) == team),
        offense_flags)
    return dict(run, **attrition, **attrition_rates, **clinical,
                outcome=own_result['outcome'], start=first, end=last,
                warrior_ticks=warrior_ticks, duty=dict(window_totals), offense_duty=dict(offense_duty),
                offense_flag_identities=len(offense_flags),
                offense_attack_fraction=offense_duty['fighting']/offense_ticks if offense_ticks else None,
                offense_flag_travel_fraction=offense_duty['to_flag']/offense_ticks if offense_ticks else None,
                attack_fraction=window_totals['fighting']/denominator if denominator else None,
                flag_travel_fraction=window_totals['to_flag']/denominator if denominator else None,
                medical_travel_fraction=(window_totals['to_food']+window_totals['to_heal'])/denominator if denominator else None,
                win=int(own_result['outcome']=='won'),loss=int(own_result['outcome']=='lost'),
                unresolved=int(own_result['outcome']=='unresolved'),
                match_unresolved=int(result['unresolved']),
                enemies_remaining=sum(t.get('alive',False) for t in result['teams'] if t['team'] in enemy_teams),
                victory_capped_tick=last if own_result['outcome']=='won' else result['resolved']['tick_limit'],
                offense_journey_outcomes=dict(offense_journeys),
                offense_arrival_fraction=offense_journeys['arrived']/sum(offense_journeys.values()) if offense_journeys else None,
                offense_arrival_median=statistics.median(offense_arrivals) if offense_arrivals else None,
                redirected_before_arrival_ticks=redirected_before_arrival_ticks,
                melee_building_damage=damage,
                damage_per_warrior_tick=damage/denominator if denominator and damage is not None else None,
                forward_providers=len(forward_providers), used_forward_providers=len(used_forward_providers),
                forward_recipients=len(forward_recipients), forward_service_ticks=dict(forward_service),
                assigned_forward_providers=len(assigned_forward_providers),
                forward_assignees=len(forward_assignees),
                forward_healing_initial_feeding_distances=[int(v['nearest_feeding_distance'])
                    for v in first_providers.values() if v.get('healing')=='1' and 'nearest_feeding_distance' in v],
                forward_builds_issued=sum(e=='forward_support_issued' for _, e, _ in events),
                medical_sortie_outcomes=dict(recovery_outcomes), medical_sortie_duty=dict(recovery_duty),
                medical_return_fraction=recovery_outcomes['returned_to_offense']/sum(recovery_outcomes.values())
                    if recovery_outcomes else None,
                completed_medical_sortie_median=statistics.median(completed_recovery_times) if completed_recovery_times else None,
                arrived_journey_median=statistics.median(travel_ticks) if travel_ticks else None,
                arrived_journeys=len(travel_ticks), journey_outcomes=dict(travel_outcomes),
                medical_after_flag=dict(medical_after_flag), target_switches=target_switches, live_enemy_switches=live_enemy_switches,
                mission_finishes=dict(finishes), dominance_start=dominance_start,
                combined_advantage_start=combined_advantage_start,
                dominant_ticks=dominant_ticks,
                dominance_to_elimination=min(after)-dominance_start if after else None,
                dominance_to_victory=last-dominance_start if dominance_start is not None and own_result['outcome']=='won' else None)


def paired_interval(rows, baseline, candidate, metric, samples=10000):
    """Equal weight to matched games; resample complete terrain clusters."""
    arms = defaultdict(dict)
    for row in rows:
        key = row.get('pair', row['id'])
        if row['arm'] in (baseline, candidate):
            if row['arm'] in arms[key]:
                raise ValueError(f"Duplicate arm for pair {key}")
            arms[key][row['arm']] = row
    clusters = defaultdict(list)
    unavailable = 0
    for pair in arms.values():
        if baseline not in pair or candidate not in pair:
            raise ValueError('Incomplete matched comparison')
        a, b = pair[baseline], pair[candidate]
        if a['cluster'] != b['cluster']:
            raise ValueError('Pair crosses terrain clusters')
        if a[metric] is None or b[metric] is None:
            unavailable += 1
            continue
        clusters[a['cluster']].append(b[metric]-a[metric])
    if not clusters:
        return {'pairs': 0, 'unavailable_pairs': unavailable, 'estimate': None, 'ci95': None}
    values = list(clusters.values())
    estimate = statistics.mean(x for c in values for x in c)
    signs = Counter((x > 0)-(x < 0) for c in values for x in c)
    paired_signs = dict(positive_pairs=signs[1],negative_pairs=signs[-1],tied_pairs=signs[0])
    if len(values) < 2:
        return {'pairs': sum(map(len, values)), 'clusters': len(values),
                'unavailable_pairs': unavailable, 'estimate': estimate, 'ci95': None, **paired_signs}
    rng = random.Random(20261024)
    bootstrap = []
    for _ in range(samples):
        chosen = [values[rng.randrange(len(values))] for _ in values]
        bootstrap.append(statistics.mean(x for c in chosen for x in c))
    bootstrap.sort()
    return {'pairs': sum(map(len, values)), 'clusters': len(values),
            'unavailable_pairs': unavailable, 'estimate': estimate,
            **paired_signs,
            # A zero-width empirical interval (especially all ties in a small
            # win sample) is not evidence of equivalence or absence of risk.
            'bootstrap_degenerate': bootstrap[0] == bootstrap[-1],
            'ci95': [bootstrap[int(samples*.025)], bootstrap[min(samples-1, int(samples*.975))]]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', default='baseline')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    root = args.manifest.parent
    rows = [summarize(run, root) for run in manifest['runs']]
    comparisons = {}
    for candidate in sorted({r['arm'] for r in rows} - {args.baseline}):
        comparisons[candidate] = {metric: paired_interval(rows, args.baseline, candidate, metric)
            for metric in ('win', 'loss', 'unresolved', 'match_unresolved', 'enemies_remaining', 'victory_capped_tick', 'redirected_before_arrival_ticks', 'attack_fraction', 'flag_travel_fraction', 'offense_attack_fraction', 'offense_flag_travel_fraction', 'offense_arrival_fraction', 'medical_travel_fraction', 'live_enemy_switches',
                           'damage_per_warrior_tick', 'target_switches', 'completed_medical_sortie_median',
                           'medical_return_fraction',
                           'strict_medical_contact_fraction', 'strict_medical_contact_median',
                           'forward_builds_issued', 'used_forward_providers', 'forward_recipients',
                           'melee_class_combat_deaths', 'melee_class_starvation_deaths',
                           'melee_class_conversions_in', 'melee_class_conversions_out',
                           'melee_class_combat_deaths_per_warrior_tick',
                           'melee_class_starvation_deaths_per_warrior_tick',
                           'melee_class_conversions_out_per_warrior_tick')}
    args.output.write_text(json.dumps({'games': rows, 'comparisons': comparisons}, indent=2)+'\n')


if __name__ == '__main__':
    main()
