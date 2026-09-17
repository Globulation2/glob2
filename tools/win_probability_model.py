#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the in-game win probability model to played games.

The map fairness model asks who *started* best. This one asks who is *winning*,
at any point while the game is still being played, and it is fitted to the same
kind of evidence: games whose outcome we know, sampled every 512 ticks.

Each 512-tick sample of each game is one contest: the surviving players are its
entries, their live state is the evidence, and the finishing order the game
eventually produced is the observed outcome. Every entry gets a fitness

    F_i = intercept + ramp(tick) * sum_k coefficient_k * transform_k(state_k(entry_i))

and the probability that entry i wins is softmax(F)_i, exactly as in the
fairness model -- the estimator is shared, in tools/conditional_logit.py.

The ramp is what makes this a mid-game model rather than a snapshot scorer. The
same lead means far less at tick 2,000 than at tick 80,000: measured over the
2,016 games of the AI Elo campaign, whoever first held the HP lead for five
straight samples went on to win only 40% of the time. Without a ramp one set of
coefficients would have to call both cases, and would call the early one far too
confidently.

Features come only from TeamStat -- the per-team state the simulation itself
maintains. The richer GameplayMeasurements and the per-AI telemetry are
deliberately excluded: both are documented as diagnostic only and must never
feed the simulation, and a winning condition built on this model does exactly
that. Anything fitted here has to be computable in-engine.

Commands:
  dataset  extract the per-sample dataset from tournament results and cache it
  screen   report what each candidate state measurement predicts on its own
  fit      select features, fit the ramp and the coefficients, emit the header
  savings  what the winning condition would have saved on games already played
"""
import argparse
import gzip
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.conditional_logit import (TRANSFORMS, Problem, cross_validated, feature_column,
                                     fit_model, softmax)
from tools.tournaments.analysis import adjudicate
from tools.tournaments.game_telemetry import parse
from tools.tournaments.results import Results

SCHEMA_VERSION = 1
# The cadence TeamStats samples at (END_OF_GAME_STAT_INTERVAL_MASK in
# src/TeamStat.h). The model is only ever evaluated on these boundaries, in the
# fit and in the engine alike.
SAMPLE_INTERVAL = 512
# The ramp is anchored here rather than at each game's own tick limit: a real
# game has no known end, so "fraction of the way through" is not something the
# engine could compute. This is the AI Elo campaign's cap, and the tick by which
# the ramp has reached full confidence.
RAMP_REFERENCE_TICK = 90000
# Below this tick nothing is called, whatever the state says. Very early samples
# can look extreme for silly reasons -- one team with two units and the other
# with none -- and no useful model has to defend that region.
MINIMUM_DECISION_TICK = 5120

# Per-team state read from the timeline, and how each name is built from the
# GLOB2_TL/GLOB2_ECON fields. Every one is a non-negative integer that the
# engine can read straight off TeamStat, which is what lets the same model be
# evaluated inside a running game.
TIMELINE_FIELDS = {
    'units': ('tl', 'units'), 'buildings': ('tl', 'bld'), 'prestige': ('tl', 'prestige'),
    'hp': ('tl', 'hp'), 'attack': ('tl', 'atk'), 'defense': ('tl', 'def'),
    'workers': ('econ', 'workers'), 'warriors': ('econ', 'warriors'),
    'explorers': ('econ', 'explorers'), 'food_critical': ('econ', 'foodCritical'),
    'need_food': ('econ', 'needFood'), 'swarms': ('econ', 'swarm'), 'inns': ('econ', 'inn'),
    'schools': ('econ', 'school'), 'barracks': ('econ', 'barracks'), 'towers': ('econ', 'tower'),
}
# Fields the engine prints as "stored/capacity"; split into two measurements.
PAIR_FIELDS = {'food': ('food_stored', 'food_capacity'), 'fooded': ('fed', 'feedable')}

RATIOS = {
    'warrior_ratio': ('warriors', 'units'),
    'fed_ratio': ('fed', 'feedable'),
    'food_fill': ('food_stored', 'food_capacity'),
    'starving_ratio': ('food_critical', 'units'),
}

MEASUREMENTS = tuple(TIMELINE_FIELDS) + tuple(
    name for pair in PAIR_FIELDS.values() for name in pair) + tuple(RATIOS)

# Measurements that say the same thing in different words. Greedy selection will
# otherwise take three readings of the same army and leave a model whose
# coefficients cannot be read. One term per family, as the fairness model does.
FAMILIES = {
    'army': ('hp', 'attack', 'defense', 'warriors', 'warrior_ratio'),
    'economy': ('units', 'workers', 'buildings', 'swarms'),
    'food': ('food_stored', 'food_capacity', 'fed', 'feedable', 'fed_ratio', 'food_fill',
             'food_critical', 'need_food', 'inns', 'starving_ratio'),
    'prestige': ('prestige',),
    'schooling': ('schools',),
    'fortification': ('barracks', 'towers'),
    'exploration': ('explorers',),
}
FAMILY_OF = {name: label for label, names in FAMILIES.items() for name in names}

# A ratio is already scale-free, so a share of the contest total would say
# something confusing about it; counts get the full set.
RATIO_TRANSFORMS = ('identity',)
COUNT_TRANSFORMS = ('identity', 'log', 'sqrt', 'share')


def transform_candidates(name):
    return RATIO_TRANSFORMS if name in RATIOS else COUNT_TRANSFORMS


def ramp(tick, gamma):
    """How much the state at `tick` is allowed to say, from 0 at the start to 1.

    Pinned to 1 at the reference tick so the coefficients mean "what this feature
    is worth late in the game" and the ramp only bends the approach to it; that
    keeps the ramp identified against the coefficient scale instead of the two
    trading off freely.
    """
    return min(max(tick / RAMP_REFERENCE_TICK, 0.0), 1.0) ** gamma


# ---------------------------------------------------------------------------
# Dataset
# ---------------------------------------------------------------------------
def timeline(source, record):
    """Per-team state by tick, from one game's verified log.

    Only the two TeamStat-derived record types are read. The prefix test comes
    before the parser because a game's log is tens of megabytes of mostly
    performance and per-AI samples, and this has to run over a thousand games.
    """
    samples = defaultdict(dict)
    with source.open_artifact(record, 'stdout.log') as stream:
        for line in stream:
            if not (line.startswith('GLOB2_TL ') or line.startswith('GLOB2_ECON ')):
                continue
            parsed = parse(line)
            if not parsed:
                continue
            prefix, fields = parsed
            tick, team = fields.get('tick'), fields.get('team')
            if tick is None or team is None:
                continue
            samples[(tick, team)]['tl' if prefix == 'GLOB2_TL' else 'econ'] = fields
    return samples


def measurements_of(pair):
    """One entry's measurements, or None if either record type is missing."""
    if 'tl' not in pair or 'econ' not in pair:
        return None
    values = {}
    for name, (which, field) in TIMELINE_FIELDS.items():
        value = pair[which].get(field)
        if value is None or not isinstance(value, int):
            return None
        values[name] = float(max(value, 0))
    for field, (left, right) in PAIR_FIELDS.items():
        raw = pair['econ'].get(field)
        if not isinstance(raw, str) or '/' not in raw:
            return None
        first, _, second = raw.partition('/')
        try:
            values[left], values[right] = float(max(int(first), 0)), float(max(int(second), 0))
        except ValueError:
            return None
    for name, (top, bottom) in RATIOS.items():
        values[name] = values[top] / values[bottom] if values[bottom] > 0 else 0.0
    return values


def game_slices(source, record, policy='prestige'):
    """One contest per 512-tick sample of one game, labelled by how it ended.

    The label is the game's finishing order, not anything about the sample: that
    is the whole point, since what we want to predict at tick T is the result
    that only arrives later. Tick-capped games are labelled by the same economic
    adjudication the ratings use, so the games that most need calling early are
    not the ones missing from the fit.
    """
    result = record.get('result') or {}
    if result.get('job_type') != 'game' or not result.get('teams'):
        return []
    labels = record['job'].get('labels', {})
    fmt = labels.get('format') or ('1v1' if len(result['teams']) == 2 else 'ffa')
    allied = fmt == '2v2'
    verdict = adjudicate(result, policy, alliances=allied)
    placements = verdict['placements']
    if len(placements) < 2:
        return []
    teams = result['teams']
    key_of = {t['team']: (t['alliance'] if allied else t['team']) for t in teams}
    eliminated = {t['team']: (t.get('eliminated_tick', -1)) for t in teams}
    cap = (result.get('resolved') or {}).get('tick_limit') or result.get('ticks') or RAMP_REFERENCE_TICK
    samples = timeline(source, record)
    by_tick = defaultdict(dict)
    for (tick, team), pair in samples.items():
        if team not in key_of:
            continue
        gone = eliminated.get(team, -1)
        if gone is not None and gone >= 0 and tick > gone:
            continue  # left the game before this sample; not a competitor here
        values = measurements_of(pair)
        if values is not None:
            by_tick[tick][team] = values
    slices = []
    for tick in sorted(by_tick):
        # Allies are one competitor: they win or lose together, so counting them
        # as two independent entries would double-count the evidence.
        grouped = defaultdict(lambda: defaultdict(float))
        for team, values in by_tick[tick].items():
            for name, value in values.items():
                grouped[key_of[team]][name] += value
        if len(grouped) < 2:
            continue  # nothing left to predict
        for key, values in grouped.items():
            for name, (top, bottom) in RATIOS.items():
                values[name] = values[top] / values[bottom] if values[bottom] > 0 else 0.0
        entries = [{'key': key, 'placement': placements[key],
                    'won': key in verdict['winners'], 'measurements': dict(values)}
                   for key, values in sorted(grouped.items())]
        slices.append({'job_id': record['job']['id'], 'map': labels.get('map', ''),
                       'format': fmt, 'tick': tick, 'tick_cap': cap,
                       'termination': result.get('termination'),
                       'engine_outcome': verdict['engine_outcome'], 'entries': entries})
    return slices


def build_dataset(roots, output, policy='prestige', limit=None):
    games, skipped = [], defaultdict(int)
    for root in roots:
        source = Results(root)
        for record in source:
            if limit is not None and len(games) >= limit:
                break
            if not record.get('accepted') or (record.get('result') or {}).get('job_type') != 'game':
                skipped['not an accepted game'] += 1
                continue
            if not any(a['path'] == 'stdout.log' for a in record['artifacts']):
                skipped['no log'] += 1
                continue
            try:
                slices = game_slices(source, record, policy)
            except (ValueError, OSError, KeyError) as error:
                skipped[f'unreadable: {type(error).__name__}'] += 1
                continue
            if not slices:
                skipped['no usable samples'] += 1
                continue
            games.append({'job_id': record['job']['id'], 'map': slices[0]['map'],
                          'format': slices[0]['format'], 'tick_cap': slices[0]['tick_cap'],
                          'termination': slices[0]['termination'],
                          'engine_outcome': slices[0]['engine_outcome'],
                          'ticks': (record['result'] or {}).get('ticks'),
                          'seconds': record.get('seconds'),
                          'slices': [{'tick': s['tick'], 'entries': s['entries']} for s in slices]})
    payload = {'schema_version': SCHEMA_VERSION, 'policy': policy,
               'measurements': list(MEASUREMENTS), 'games': games, 'skipped': dict(skipped)}
    with gzip.open(output, 'wt') as stream:
        json.dump(payload, stream)
    return payload


def load_cache(path):
    with gzip.open(path, 'rt') as stream:
        return json.load(stream)


def contests(cache, stride=1, formats=None, minimum_tick=0):
    """Flatten the cached games into the estimator's contest list."""
    games = []
    for game in cache['games']:
        if formats and game['format'] not in formats:
            continue
        for index, piece in enumerate(game['slices']):
            if index % stride or piece['tick'] < minimum_tick:
                continue
            games.append({'job_id': game['job_id'], 'map': game['map'], 'format': game['format'],
                          'tick': piece['tick'], 'tick_cap': game['tick_cap'],
                          'termination': game['termination'], 'entries': piece['entries']})
    return {'games': games, 'skipped': cache.get('skipped', {})}


def ramped_column(gamma):
    """Feature columns scaled by the ramp: the transform first, then the ramp."""
    def column(dataset, name, transform):
        base = feature_column(dataset, name, transform)
        return [[value * ramp(game['tick'], gamma) for value in row]
                for row, game in zip(base, dataset['games'])]
    return column


def by_game(game):
    return game['job_id']


# ---------------------------------------------------------------------------
# Selection and the ramp
# ---------------------------------------------------------------------------
def screen(data, gamma, folds=5, ridge=1e-3):
    """What each candidate measurement and transform predicts on its own."""
    column = ramped_column(gamma)
    rows = []
    for name in MEASUREMENTS:
        for transform in transform_candidates(name):
            score = cross_validated(data, [(name, transform)], folds, ridge,
                                    group=by_game, column=column)
            if not score.get('folds'):
                continue
            rows.append({'name': name, 'transform': transform, 'family': FAMILY_OF[name],
                         'cv_r2': score['mcfadden_r2'], 'cv_accuracy': score['accuracy'],
                         'cv_chance': score['chance_accuracy']})
    rows.sort(key=lambda row: -row['cv_r2'])
    return rows


def select(data, candidates, gamma, limit=6, folds=5, ridge=1e-3, tolerance=0.0005):
    """Greedy forward selection on cross-validated fit, one term per family."""
    column = ramped_column(gamma)
    chosen, used, history, current = [], set(), [], 0.0
    while len(chosen) < limit:
        trials = []
        for row in candidates:
            if row['name'] in used or row['family'] in used:
                continue
            option = chosen + [(row['name'], row['transform'])]
            score = cross_validated(data, option, folds, ridge, group=by_game, column=column)
            if score.get('folds'):
                trials.append((score['mcfadden_r2'], row, score))
        if not trials:
            break
        trials.sort(key=lambda item: -item[0])
        value, row, score = trials[0]
        if value - current < tolerance:
            history.append({'stopped': True, 'best_candidate': row['name'],
                            'gain': value - current, 'tolerance': tolerance})
            break
        chosen.append((row['name'], row['transform']))
        used.add(row['name'])
        used.add(row['family'])
        current = value
        history.append({'added': row['name'], 'transform': row['transform'],
                        'family': row['family'], 'cv_r2': value,
                        'cv_accuracy': score.get('accuracy')})
    return chosen, history


RAMP_GRID = (0.4, 0.6, 0.8, 1.0, 1.3, 1.7, 2.2, 3.0)


def tune_ramp(data, features, grid=RAMP_GRID, folds=5, ridge=1e-3):
    """Pick the ramp exponent on cross-validated log-loss, not on the training fit.

    Log-loss rather than R2 because the ramp's whole job is calibration: it
    decides how confident the model is allowed to be early, and an overconfident
    early call is exactly what log-loss punishes and accuracy does not.
    """
    rows = []
    for gamma in grid:
        score = cross_validated(data, features, folds, ridge, group=by_game,
                                column=ramped_column(gamma))
        if score.get('folds'):
            rows.append({'gamma': gamma, 'cv_log_loss': score['log_loss'],
                         'cv_r2': score['mcfadden_r2'], 'cv_accuracy': score['accuracy']})
    best = min(rows, key=lambda row: row['cv_log_loss']) if rows else None
    return best, rows


def probabilities(model, data):
    """Per-contest fitness and win probability under a fitted model."""
    features = [(item['name'], item['transform']) for item in model['features']]
    coefficients = [item['coefficient'] for item in model['features']]
    columns = [ramped_column(model['gamma'])(data, name, transform)
               for name, transform in features]
    output = []
    for position, game in enumerate(data['games']):
        fitnesses = []
        for member in range(len(game['entries'])):
            # The intercept is the same for every entry of a contest, so softmax
            # cancels it; it is carried only so reported fitness levels match the
            # fairness model's convention.
            value = model['intercept']
            for column, coefficient in zip(columns, coefficients):
                value += coefficient * column[position][member]
            fitnesses.append(value)
        output.append({'fitness': fitnesses, 'probability': softmax(fitnesses)})
    return output


def trigger_tick(model, game_slices, threshold, dwell):
    """The first tick the condition would fire at, and who it would crown.

    `dwell` consecutive qualifying samples are required, so a single freak sample
    cannot end a game. Returns (tick, key, won) or None if it never fires.
    """
    data = {'games': game_slices}
    if not game_slices:
        return None
    rows = probabilities(model, data)
    streak, holder = 0, None
    for row, piece in zip(rows, game_slices):
        best = max(range(len(piece['entries'])), key=lambda i: row['probability'][i])
        entry = piece['entries'][best]
        if row['probability'][best] >= threshold and piece['tick'] >= MINIMUM_DECISION_TICK:
            streak = streak + 1 if holder == entry['key'] else 1
            holder = entry['key']
            if streak >= dwell:
                return {'tick': piece['tick'], 'key': entry['key'], 'won': bool(entry['won'])}
        else:
            streak, holder = 0, None
    return None


def savings(cache, features, gamma, threshold=0.97, dwell=3, folds=4, ridge=1e-3, seed=1):
    """What the condition would have saved on games already played.

    Every game is scored by a model that never saw it: without cross-fitting, the
    model has met each game's own outcome and would report a precision the engine
    would not reproduce.
    """
    import random as _random
    games = [g for g in cache['games'] if g['slices']]
    ids = sorted({g['job_id'] for g in games})
    rng = _random.Random(seed)
    rng.shuffle(ids)
    fold_of = {job: index % folds for index, job in enumerate(ids)}
    rows = []
    for fold in range(folds):
        train = contests({'games': [g for g in games if fold_of[g['job_id']] != fold]},
                         stride=2, minimum_tick=MINIMUM_DECISION_TICK)
        if not train['games']:
            continue
        model = fit_model(train, features, ridge, column=ramped_column(gamma))
        model['gamma'] = gamma
        for game in games:
            if fold_of[game['job_id']] != fold:
                continue
            pieces = [{'job_id': game['job_id'], 'map': game['map'], 'format': game['format'],
                       'tick': s['tick'], 'tick_cap': game['tick_cap'],
                       'termination': game['termination'], 'entries': s['entries']}
                      for s in game['slices']]
            fired = trigger_tick(model, pieces, threshold, dwell)
            ticks = game['ticks'] or game['tick_cap']
            rows.append({'job_id': game['job_id'], 'format': game['format'],
                         'termination': game['termination'], 'ticks': ticks,
                         'seconds': game['seconds'] or 0.0, 'fired': fired})
    return rows


def summarise_savings(rows):
    """Ticks and wall clock saved, and what the condition got wrong."""
    total = {'games': len(rows), 'fired': 0, 'wrong': 0,
             'ticks': 0, 'ticks_saved': 0, 'seconds': 0.0, 'seconds_saved': 0.0}
    groups = defaultdict(lambda: dict(total))
    for row in rows:
        ticks, seconds = row['ticks'] or 0, row['seconds'] or 0.0
        fired = row['fired']
        # Wall clock is apportioned by tick fraction. Late ticks are slower --
        # more units to simulate -- so this understates what stopping early saves.
        saved_ticks = max(ticks - fired['tick'], 0) if fired else 0
        saved_seconds = seconds * (saved_ticks / ticks) if ticks else 0.0
        for bucket in (total, groups[row['termination']], groups[row['format']]):
            bucket['games'] = bucket.get('games', 0)
            bucket['ticks'] += ticks
            bucket['seconds'] += seconds
            bucket['ticks_saved'] += saved_ticks
            bucket['seconds_saved'] += saved_seconds
            if fired:
                bucket['fired'] += 1
                if not fired['won']:
                    bucket['wrong'] += 1
    for bucket in groups.values():
        bucket['games'] = 0
    for row in rows:
        groups[row['termination']]['games'] += 1
        groups[row['format']]['games'] += 1
    return total, dict(groups)


def calibration(model, data, buckets=12):
    """Predicted against observed win rate, pooled over every entry of every contest."""
    pairs = []
    for row, game in zip(probabilities(model, data), data['games']):
        for probability, entry in zip(row['probability'], game['entries']):
            pairs.append((probability, 1.0 if entry['won'] else 0.0))
    pairs.sort()
    size = max(1, len(pairs) // buckets)
    report = []
    for start in range(0, len(pairs), size):
        block = pairs[start:start + size]
        if len(block) < size // 2:
            break
        report.append({'count': len(block),
                       'predicted': sum(p for p, _ in block) / len(block),
                       'observed': sum(w for _, w in block) / len(block)})
    return report
