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
from .probability import group_summary, stopping_threshold

POLICY_VERSION = 2
POLICIES = ('win_probability', 'prestige', 'survivor_draw', 'military')


def adjudicate(result, policy='win_probability', alliances=False):
    if policy not in POLICIES:
        raise ValueError('unknown adjudication policy')
    teams = result['teams']
    engine_outcome = any(t['outcome'] == 'won' or t['team'] in result.get('winning_teams', []) for t in teams)
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
            if (result.get('termination') != 'tick_cap' or policy == 'survivor_draw'
                    or (policy == 'win_probability' and engine_outcome)):
                score = (2,)
            elif policy == 'win_probability':
                terminal_tick = result.get('ticks')
                probability = group_summary(colonies, terminal_tick)
                if probability is None:
                    raise ValueError('win_probability adjudication requires automatic probability statistics; use an explicit legacy policy for older results')
                if terminal_tick is None or probability['end_tick'] != terminal_tick:
                    raise ValueError('win_probability adjudication requires a prediction at the terminal tick')
                score = (2, probability['final'])
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
            'engine_outcome': engine_outcome, 'placements': placements,
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
        probabilities = [group_summary([t for t in result['teams']
                         if (t['alliance'] if fmt == '2v2' else t['team']) == key], result['ticks']) for key in keys]
        rows.append({'job_id': job['id'], 'format': fmt, 'block': str(labels.get('block', digest(job['inputs']))),
                     'map': labels.get('map', labels.get('map_seed')), 'build': job['build'],
                     'seeds': job['seeds'],
                     'variant': labels.get('variant', 'baseline'), 'pair': labels.get('pair'),
                     'held_out': labels.get('held_out', False), 'rotation': labels.get('rotation', 0), 'generator': labels.get('generator'),
                     'subject_group': next((t['alliance'] if fmt == '2v2' else t['team'] for t in result['teams'] if t['team'] == labels.get('subject_player',0)), keys[0]),
                     'symmetric_control': labels.get('symmetric_control', False),
                     'ticks': result['ticks'], 'cap': result['termination'] == 'tick_cap',
                     'termination': result['termination'],
                     'win_probability_permille': stopping_threshold(result, job['config']),
                     'engine_outcome': outcome['engine_outcome'], 'groups': keys,
                     'competitors': competitors, 'probabilities': probabilities, 'placements': [outcome['placements'][key] for key in keys],
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


def _block_mean_interval(blocks, draws, rng):
    """Bootstrap an equal-game mean, keeping each seed's rotations together."""
    keys = sorted(blocks)
    if len(keys) < 2 or draws < 1:
        return None
    estimates = []
    for _ in range(draws):
        values = []
        for _ in keys:
            values.extend(blocks[rng.choice(keys)])
        estimates.append(statistics.mean(values))
    return [percentile(estimates, .025), percentile(estimates, .975)]


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
            for metric in ('ticks', 'placement', 'win_probability_mean'):
                if metric == 'win_probability_mean':
                    a = row.get('probabilities', [None] * len(row['groups']))[row['groups'].index(row['subject_group'])]
                    b = baseline.get('probabilities', [None] * len(baseline['groups']))[baseline['groups'].index(baseline['subject_group'])]
                    if (not a or not b or not a['whole_game'] or not b['whole_game'] or a['model'] != b['model']
                            or row.get('win_probability_permille', 0) != baseline.get('win_probability_permille', 0)):
                        continue
                    differences[(build, held_out, name, metric, a['model'], row.get('win_probability_permille', 0))].append((pair, a['mean'] - b['mean'], row['block']))
                    continue
                effect = (row['ticks'] - baseline['ticks']) if metric == 'ticks' else (row['placements'][row['groups'].index(row.get('subject_group',row['groups'][0]))] - baseline['placements'][baseline['groups'].index(baseline.get('subject_group',baseline['groups'][0]))])
                differences[(build, held_out, name, metric, None, None)].append((pair, effect, row['block']))
    rng = random.Random(seed)
    output = []
    for (build, held_out, variant, metric, model, threshold), pairs in sorted(differences.items(), key=lambda item: str(item[0])):
        values = [value for _, value, _ in pairs]
        blocks = defaultdict(list)
        for _, value, block in pairs:
            blocks[block].append(value)
        if metric == 'win_probability_mean':
            interval = _block_mean_interval(blocks, draws, rng)
        else:
            estimates = [statistics.mean(rng.choices(values, k=len(values))) for _ in range(draws)] if len(values) > 1 else []
            interval = [percentile(estimates, .025), percentile(estimates, .975)] if estimates else None
        output.append({'build': build, 'held_out': held_out, 'variant': variant, 'metric': metric,
                       **({'model': model, 'win_probability_permille': threshold, 'blocks': len(blocks),
                           'resampling': 'seed_block'} if metric == 'win_probability_mean' else {}),
                       'pairs': len(values), 'mean_effect': statistics.mean(values),
                       'interval': interval,
                       'raw_pairs': [(pair, value) for pair, value, _ in pairs]})
    return output


def probability_scores(rows, draws=1000, seed=1):
    """Standard continuous AI score beside W/L/D, with seed-block uncertainty.

    Average each game's normalized integral, rather than pooling raw area (which
    would give longer games more weight). Keep model/threshold cohorts separate.
    """
    groups = defaultdict(list)
    for row in rows:
        for i, competitor in enumerate(row['competitors']):
            value = row.get('probabilities', [None] * len(row['competitors']))[i]
            # In an ablation both sides may run the same AI implementation.
            # Pooling subject and opponent would cancel every improvement.
            role = ('subject' if row['groups'][i] == row['subject_group'] else 'opponent') if row.get('pair') is not None else 'competitor'
            key = (row['format'], row['build'], row['variant'], competitor, role,
                   value['model'] if value else None, row.get('win_probability_permille', 0))
            best = min(row['placements'])
            leading = row['placements'][i] == best
            outcome = 'draws' if leading and row['placements'].count(best) > 1 else 'wins' if leading else 'losses'
            groups[key].append((row['block'], value, outcome, row.get('termination')))
    output = []
    for (fmt, build, variant, competitor, role, model, threshold), entries in sorted(groups.items(), key=lambda item: str(item[0])):
        usable = [(block, value) for block, value, _, _ in entries if value and value['whole_game']]
        outcomes = Counter(outcome for _, _, outcome, _ in entries)
        blocks = defaultdict(list)
        for block, value in usable:
            blocks[block].append(value['mean'])
        rng = random.Random(seed_for(seed, fmt, build, variant, competitor, role, model, threshold))
        output.append({'format': fmt, 'build': build, 'variant': variant, 'competitor': competitor,
                       'role': role,
                       'model': model, 'win_probability_permille': threshold, 'games': len(entries),
                       **{name: outcomes[name] for name in ('wins', 'losses', 'draws')},
                       'probability_ended_games': sum(end == 'win_probability' for _, _, _, end in entries),
                       'capped_games': sum(end == 'tick_cap' for _, _, _, end in entries),
                       'scored_games': len(usable), 'unavailable': len(entries) - len(usable),
                       'blocks': len(blocks),
                       'mean': statistics.mean(v['mean'] for _, v in usable) if usable else None,
                       'mean_area_probability_ticks': statistics.mean(v['area'] for _, v in usable) if usable else None,
                       'interval': _block_mean_interval(blocks, draws, rng)})
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


def reanalyze(directory, policy='win_probability', draws=1000, seed=1, k=32, output=None):
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
              'probability_scores': probability_scores(rows,draws,seed),
              'paired_effects': paired_effects(rows,draws,seed), 'generators': generator_distributions(records),
              'generator_attempts': generator_distributions(attempts),
              'failure_groups': [{'type':t,'build':b,'host':h,'category':c,'stack_signature':s,'count':n} for (t,b,h,c,s),n in failure_groups.items()],
              'attempt_counts': dict(Counter(record['category'] for record in attempts)),
              'failure_rates': {category: count/len(attempts) for category,count in Counter(r['category'] for r in attempts).items()} if attempts else {},
              'configurations': [{'id':j['id'],'build':j['build'],'config':j['config'],'seeds':j['seeds'],'labels':j['labels']} for j in source.manifest['jobs']]}
    # Standard duels use all outcomes equally; keep sequential values only as
    # historical diagnostics. Other formats retain their established estimator.
    from .duel_ratings import report as duel_report
    batch = duel_report(rows, draws, seed)
    report['legacy_sequential_elo'] = report.pop('elo')
    report['rating_methods'] = {'1v1':'bradley_terry_batch', 'other_formats':'sequential_elo'}
    report['legacy_sequential_ratings'] = report['ratings']
    report['legacy_sequential_uncertainty'] = report['uncertainty']
    report['ratings'] = {key:value for key,value in report['ratings'].items() if key.split(':')[0] != '1v1'} | batch['ratings']
    report['duel_rating_fit'] = batch
    report['uncertainty'] = {key:value for key,value in report['uncertainty'].items() if key != 'intervals'} | {
        'intervals': {key:value for key,value in report['uncertainty']['intervals'].items() if not key.startswith('1v1:')},
        'duels': batch['cohorts']}
    from .map_telemetry import summarize
    report['map_telemetry'] = summarize(records, draws, seed)
    out = Path(output) if output else Path(directory) / 'reports' / policy
    out.mkdir(parents=True,exist_ok=True)
    from .game_telemetry import export
    report['game_telemetry'] = export(source, records, out)
    atomic_json(out/'report.json',report)
    atomic_json(out/'map-telemetry.json', report['map_telemetry'])
    write_csv(out/'map-telemetry-records.csv', report['map_telemetry']['records'])
    write_csv(out/'map-metrics.csv', report['map_telemetry']['maps'])
    write_csv(out/'map-telemetry-groups.csv', report['map_telemetry']['groups'])
    for name in ('observations','matchups','map_start_breakdown','paired_effects','probability_scores','generators','configurations'):
        write_csv(out/(name+'.csv'),report[name])
    lines = [f'# {source.manifest["id"]}', '', f'Policy: {policy}, version {POLICY_VERSION}. Results: {len(records)}/{len(source.manifest["jobs"])}.',
             '', 'Duels: order-independent Bradley–Terry fit, mean 1500, 400 points per tenfold odds, draws scored 0.5. No prior or K factor. Other formats retain sequential Elo with K='+str(k)+'.',
             '', '| Format | Competitor | Elo |', '| --- | --- | ---: |']
    for fmt, ratings in sorted(report['ratings'].items()):
        for competitor, rating in sorted(ratings.items(),key=lambda item:(-item[1],item[0])):
            lines.append(f'| {fmt} | {competitor} | {rating:.1f} |')
    lines += ['', f'Uncertainty uses {report["uncertainty"]["complete_blocks"]} complete map/seed blocks, seed {seed}.',
              'Duel intervals resample whole swapped-side pairs within each generator. Missing/unidentifiable fits are explicit in duel_rating_fit; sequential diagnostics are retained separately.',
              'Raw observations, failures, configurations, pairing and distribution summaries are in the adjacent JSON and CSV files.']
    if report['probability_scores']:
        lines += ['', '## Mean whole-game win probability', '',
                  'Trapezoidal area divided by observed duration; allies counted once. Higher is better.',
                  'Early stopping changes the observed duration. Compare matched stopping rules and models.',
                  '', '| Format | Build | Variant | Competitor | Role | Stop (permille) | W / L / D | Scored / games | Mean | 95% block interval |',
                  '| --- | --- | --- | --- | --- | ---: | --- | ---: | ---: | --- |']
        for score in report['probability_scores']:
            mean = f"{score['mean']:.4f}" if score['mean'] is not None else 'unavailable'
            lines.append(f"| {score['format']} | {score['build'][:12]} | {score['variant']} | {score['competitor']} | {score['role']} | {score['win_probability_permille']} | {score['wins']} / {score['losses']} / {score['draws']} | {score['scored_games']} / {score['games']} | {mean} | {score['interval']} |")
    if report['map_telemetry']['groups']:
        lines += ['', '## Map telemetry', '',
                  '| Generator | Revision | Maps | Missing traces | Incomplete traces |',
                  '| --- | --- | ---: | ---: | ---: |']
        for group in report['map_telemetry']['groups']:
            lines.append(f"| {group['generator']} | {group['revision']} | {group['maps']} | {group['missing']} | {group['incomplete']} |")
        lines += ['', 'Groups retain complete configuration and build identity in map-telemetry.json.',
                  'Numeric summaries average within each map before pooling maps. Raw sequence and subject are preserved.',
                  'Observed fallback/choice rates may undercount when traces are missing or incomplete.']
    (out/'report.md').write_text('\n'.join(lines)+'\n')
    return report
