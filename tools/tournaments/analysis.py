"""Offline policy, reproducible ratings, paired effects and readable exports."""
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import random
import statistics
from .common import atomic_json, digest
from .fairness_statistics import share_table, squared_bias, bias_interval, rms_points, seed_for, benjamini_hochberg, holm
from .results import Results

POLICY_VERSION = 1
POLICIES = ('prestige', 'survivor_draw', 'military')


def adjudicate(result, policy='prestige', alliances=False):
    if policy not in POLICIES:
        raise ValueError('unknown adjudication policy')
    teams = result['teams']
    groups = defaultdict(list)
    for team in teams:
        groups[team['alliance'] if alliances else team['team']].append(team)
    scores = {}
    for group, colonies in groups.items():
        won = any(t['outcome'] == 'won' or t['team'] in result.get('winning_teams', []) for t in colonies)
        alive = any(t['alive'] and t['outcome'] != 'lost' for t in colonies)
        # Preserve engine declarations, including simultaneous alliance wins.
        if won:
            score = (3,)
        elif alive:
            if result.get('termination') != 'tick_cap' or policy == 'survivor_draw':
                score = (2,)
            else:
                fields = ('warrior_attack', 'warrior_hp', 'warriors') if policy == 'military' else ('prestige', 'units', 'buildings')
                score = (2, *(sum(t[k] for t in colonies if t['alive']) for k in fields))
        else:
            score = (1, max(t.get('eliminated_tick', -1) for t in colonies))
        scores[group] = score
    placements = {}
    for group, score in scores.items():
        better = sum(other > score for other in scores.values())
        tied = sum(other == score for other in scores.values())
        placements[group] = 1 + better + (tied - 1) / 2
    best = max(scores.values())
    return {'policy': policy, 'version': POLICY_VERSION, 'alliances': alliances,
            'engine_outcome': bool(result.get('winning_teams')), 'placements': placements,
            'winners': [key for key, score in scores.items() if score == best], 'scores': scores}


def elo_update(ratings, competitors, placements, k=32):
    """Simultaneous pairwise Elo; each competitor's update is divided by n-1."""
    n = len(competitors)
    if n < 2:
        return
    old = [ratings.get(name, 1500.0) for name in competitors]
    delta = defaultdict(float)
    for i, name in enumerate(competitors):
        for j in range(n):
            if i == j:
                continue
            actual = 1.0 if placements[i] < placements[j] else 0.0 if placements[i] > placements[j] else 0.5
            expected = 1 / (1 + 10 ** ((old[j] - old[i]) / 400))
            delta[name] += k * (actual - expected) / (n - 1)
    for name in competitors:
        ratings[name] = ratings.get(name, 1500.0) + delta.pop(name, 0.0)


def observations(records, policy):
    rows = []
    for record in records:
        result = record.get('result') or {}
        if record['job']['type'] != 'game' or record['category'] != 'success' or not result.get('teams'):
            continue
        job = record['job']
        labels = job['labels']
        fmt = labels.get('format', 'ffa' if len(result['teams']) > 2 else '1v1')
        outcome = adjudicate(result, policy, alliances=fmt == '2v2')
        players = {p['team']: p['ai'] for p in result['players']}
        groups = defaultdict(list)
        for team in result['teams']:
            groups[team['alliance'] if fmt == '2v2' else team['team']].append(players.get(team['team'], 'unknown'))
        keys = sorted(groups)
        competitors = ['+'.join(sorted(groups[key])) for key in keys]
        rows.append({'job_id': job['id'], 'format': fmt, 'block': str(labels.get('block', digest(job['inputs']))),
                     'map': labels.get('map', labels.get('map_seed')), 'build': job['build'],
                     'variant': labels.get('variant', 'baseline'), 'pair': labels.get('pair'),
                     'held_out': labels.get('held_out', False), 'rotation': labels.get('rotation', 0), 'generator': labels.get('generator'),
                     'subject_group': next((t['alliance'] if fmt == '2v2' else t['team'] for t in result['teams'] if t['team'] == labels.get('subject_player',0)), keys[0]),
                     'symmetric_control': labels.get('symmetric_control', False),
                     'ticks': result['ticks'], 'cap': result['termination'] == 'tick_cap',
                     'engine_outcome': outcome['engine_outcome'], 'groups': keys,
                     'competitors': competitors, 'placements': [outcome['placements'][key] for key in keys],
                     'winners': outcome['winners'], 'policy': outcome['policy'], 'teams': result['teams']})
    return rows


def rate(rows, k=32):
    ratings = defaultdict(dict)
    cohorts = {row['build'] for row in rows}
    for row in rows:
        label = row['format'] + (':' + row['build'][:12] if len(cohorts) > 1 else '')
        elo_update(ratings[label], row['competitors'], row['placements'], k)
    return dict(ratings)


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(values)
    position = p * (len(ordered) - 1)
    lo, hi = math.floor(position), math.ceil(position)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def bootstrap_ratings(rows, manifest, draws, seed, k):
    expected = Counter(str(j['labels'].get('block', digest(j['inputs']))) for j in manifest['jobs'] if j['type'] == 'game')
    groups = defaultdict(list)
    for row in rows:
        groups[row['block']].append(row)
    complete = {key: block for key, block in groups.items() if len(block) == expected[key]}
    if len(complete) < 2 or draws < 1:
        return {'complete_blocks': len(complete), 'intervals': {}, 'draws': 0, 'seed': seed}
    keys = sorted(complete)
    rng = random.Random(seed)
    samples = defaultdict(list)
    for _ in range(draws):
        sampled = [row for _ in keys for row in complete[rng.choice(keys)]]
        for fmt, ratings in rate(sampled, k).items():
            for competitor, value in ratings.items():
                samples[(fmt, competitor)].append(value)
    return {'complete_blocks': len(complete), 'draws': draws, 'seed': seed,
            'intervals': {fmt + ':' + name: [percentile(values, 0.025), percentile(values, 0.975)]
                          for (fmt, name), values in sorted(samples.items())}}


def matchup_tables(rows):
    matches = defaultdict(lambda: Counter(games=0, wins=0, draws=0, caps=0, ticks=0))
    breakdown = defaultdict(lambda: Counter(games=0, wins=0, caps=0))
    for row in rows:
        for i, competitor in enumerate(row['competitors']):
            breakdown[(row['format'], str(row['map']), row['rotation'], competitor)].update(
                games=1, wins=int(row['groups'][i] in row['winners'] and len(row['winners']) == 1), caps=int(row['cap']))
            for j, opponent in enumerate(row['competitors']):
                if i == j:
                    continue
                entry = matches[(row['format'], competitor, opponent)]
                entry.update(games=1, wins=int(row['placements'][i] < row['placements'][j]),
                             draws=int(row['placements'][i] == row['placements'][j]), caps=int(row['cap']), ticks=row['ticks'])
    return ([{'format': fmt, 'competitor': a, 'opponent': b, **value,
              'win_rate': value['wins']/value['games'], 'draw_rate': value['draws']/value['games'],
              'cap_rate': value['caps']/value['games'], 'mean_ticks': value['ticks']/value['games']}
             for (fmt, a, b), value in sorted(matches.items())],
            [{'format': fmt, 'map': map_id, 'rotation': rotation, 'competitor': name, **value}
             for (fmt, map_id, rotation, name), value in sorted(breakdown.items())])


def fairness(rows):
    maps = defaultdict(list)
    for row in rows:
        maps[(row['build'], str(row['map']))].append(row)
    output = []
    for (build, map_id), played in sorted(maps.items()):
        n = len(played[0]['teams'])
        starts, decisive, teams = [0]*n, [0]*n, [0]*n
        for row in played:
            if len(row['winners']) != 1:
                continue
            winner = row['winners'][0]
            slot = (winner - row['rotation']) % n
            starts[slot] += 1; teams[winner] += 1
            if row['engine_outcome']: decisive[slot] += 1
        output.append({'build': build, 'generator': played[0].get('generator'), 'map': map_id, 'games': len(played),
                       'start': share_table(starts, seed_for(build,map_id,'start')),
                       'team': share_table(teams, seed_for(build,map_id,'team')),
                       'decisive_start': share_table(decisive, seed_for(build,map_id,'decisive')),
                       'symmetric_control': all(r.get('symmetric_control', False) for r in played)})
    tested = [m for m in output if m['start']['p'] is not None]
    q = benjamini_hochberg([m['start']['p'] for m in tested]) if tested else []
    corrected = holm([m['start']['p'] for m in tested]) if tested else []
    for item in output: item['start'].update(q_bh=None, p_holm=None)
    for item, bh, family in zip(tested, q, corrected):
        item['start'].update(q_bh=bh, p_holm=family)
    groups = defaultdict(list)
    for item in output: groups[(item['build'],item['generator'],len(item['start']['counts']))].append(item)
    aggregate = []
    for (build, generator, n), entries in groups.items():
        n = len(entries[0]['start']['counts'])
        usable = [m for m in entries if m['start']['n'] >= 2]
        biases = [m['start']['squared_bias'] for m in usable]
        aggregate.append({'build': build, 'generator': generator, 'maps': len(entries),
                          'rms_position_points': rms_points(statistics.mean(biases), n) if biases else None,
                          'interval': bias_interval(biases, n),
                          'pooled_start': share_table([sum(m['start']['counts'][i] for m in entries) for i in range(n)], seed_for(build,'start')),
                          'pooled_team': share_table([sum(m['team']['counts'][i] for m in entries) for i in range(n)], seed_for(build,'team'))})
    return {'maps': output, 'aggregates': aggregate}


def paired_effects(rows, draws=1000, seed=1):
    paired = defaultdict(dict)
    for row in rows:
        if row['pair'] is not None:
            # Keep complete matched configurations; subject may be any player.
            paired[(row['build'], str(row['pair']), row['held_out'])][row['variant']] = row
    differences = defaultdict(list)
    for (build, pair, held_out), variants in paired.items():
        baseline = variants.get('baseline')
        if not baseline: continue
        for name, row in variants.items():
            if name == 'baseline': continue
            for metric in ('ticks', 'placement'):
                effect = (row['ticks'] - baseline['ticks']) if metric == 'ticks' else (row['placements'][row['groups'].index(row.get('subject_group',row['groups'][0]))] - baseline['placements'][baseline['groups'].index(baseline.get('subject_group',baseline['groups'][0]))])
                differences[(build, held_out, name, metric)].append((pair, effect))
    rng = random.Random(seed)
    output = []
    for (build, held_out, variant, metric), pairs in sorted(differences.items()):
        values = [value for _, value in pairs]
        estimates = [statistics.mean(rng.choices(values, k=len(values))) for _ in range(draws)] if len(values) > 1 else []
        output.append({'build': build, 'held_out': held_out, 'variant': variant, 'metric': metric,
                       'pairs': len(values), 'mean_effect': statistics.mean(values),
                       'interval': [percentile(estimates, .025), percentile(estimates, .975)] if estimates else None,
                       'raw_pairs': pairs})
    return output


def generator_distributions(records):
    groups = defaultdict(list)
    for record in records:
        if record['job']['type'] == 'generate_map':
            groups[(record['job']['build'], record['job']['labels'].get('variant', 'baseline'), record['job']['config']['generator'])].append(record)
    output = []
    for (build, variant, generator), entries in sorted(groups.items()):
        metrics = defaultdict(list)
        for record in entries:
            if 'seconds' in record: metrics['seconds'].append(record['seconds'])
            if record['category'] == 'success':
                for key, value in (record.get('result') or {}).get('statistics', {}).items():
                    if isinstance(value, (int, float)): metrics[key].append(value)
        output.append({'build': build, 'variant': variant, 'generator': generator,
                       'outcomes': dict(Counter(r['category'] for r in entries)),
                       'distributions': {k: {'n': len(v), 'mean': statistics.mean(v), 'min': min(v), 'max': max(v),
                                             'p50': percentile(v,.5), 'p95': percentile(v,.95)} for k,v in metrics.items()}})
    return output


def write_csv(path, rows):
    keys = list(dict.fromkeys(key for row in rows for key in row))
    with Path(path).open('w', newline='') as stream:
        writer = csv.DictWriter(stream, keys)
        writer.writeheader()
        writer.writerows({key: json.dumps(value, sort_keys=True) if isinstance(value, (dict,list,tuple)) else value for key,value in row.items()} for row in rows)


def reanalyze(directory, policy='prestige', draws=1000, seed=1, k=32, output=None):
    source = Results(directory)
    records = list(source)
    attempts = list(source.attempts())
    failure_groups = Counter((r['job']['type'],r['job']['build'],r['host'],r['category'],
                              (r.get('diagnostics') or {}).get('stack_signature')) for r in attempts if r['category']!='success')
    rows = observations(records, policy)
    matchup, breakdown = matchup_tables(rows)
    report = {'schema_version': 1, 'experiment': source.manifest['id'], 'policy': {'name': policy, 'version': POLICY_VERSION, 'options': {}},
              'elo': {'initial': 1500, 'k': k, 'order': 'manifest', 'ffa': 'simultaneous pairwise / opponent count'},
              'ratings': rate(rows,k), 'uncertainty': bootstrap_ratings(rows,source.manifest,draws,seed,k),
              'committed_results': len(records), 'planned_jobs': len(source.manifest['jobs']),
              'matchups': matchup, 'map_start_breakdown': breakdown, 'observations': rows,
              'fairness': fairness(rows) if source.manifest.get('kind') == 'fairness' else None,
              'paired_effects': paired_effects(rows,draws,seed), 'generators': generator_distributions(records),
              'generator_attempts': generator_distributions(attempts),
              'failure_groups': [{'type':t,'build':b,'host':h,'category':c,'stack_signature':s,'count':n} for (t,b,h,c,s),n in failure_groups.items()],
              'attempt_counts': dict(Counter(record['category'] for record in attempts)),
              'failure_rates': {category: count/len(attempts) for category,count in Counter(r['category'] for r in attempts).items()} if attempts else {},
              'configurations': [{'id':j['id'],'build':j['build'],'config':j['config'],'seeds':j['seeds'],'labels':j['labels']} for j in source.manifest['jobs']]}
    out = Path(output) if output else Path(directory) / 'reports' / policy
    out.mkdir(parents=True,exist_ok=True)
    atomic_json(out/'report.json',report)
    for name in ('observations','matchups','map_start_breakdown','paired_effects','generators','configurations'):
        write_csv(out/(name+'.csv'),report[name])
    lines = [f'# {source.manifest["id"]}', '', f'Policy: {policy}, version {POLICY_VERSION}. Results: {len(records)}/{len(source.manifest["jobs"])}.',
             '', 'Elo starts at 1500, K='+str(k)+', in manifest order. FFA updates are simultaneous and normalized by opponent count.',
             '', '| Format | Competitor | Elo |', '| --- | --- | ---: |']
    for fmt, ratings in sorted(report['ratings'].items()):
        for competitor, rating in sorted(ratings.items(),key=lambda item:(-item[1],item[0])):
            lines.append(f'| {fmt} | {competitor} | {rating:.1f} |')
    lines += ['', f'Uncertainty uses {report["uncertainty"]["complete_blocks"]} complete map/seed blocks, seed {seed}.',
              'Raw observations, failures, configurations, pairing and distribution summaries are in the adjacent JSON and CSV files.']
    (out/'report.md').write_text('\n'.join(lines)+'\n')
    return report
