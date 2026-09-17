#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the in-game win probability model to played games.

The map fairness model asks who *started* best. This one asks who is *winning*,
at any point while the game is still being played, and it is fitted to the same
kind of evidence: games whose outcome we know, sampled every 512 ticks.

Each 512-tick sample of each game is one contest: the surviving players are its
entries, their live state is the evidence, and the finishing order the game
eventually produced is the observed outcome. Every entry gets a fitness

    F_i = intercept + sum_k coefficient_k * transform_k(state_k(entry_i))

and the probability that entry i wins is softmax(F)_i, exactly as in the
fairness model -- the estimator is shared, in tools/conditional_logit.py.

A mid-game model has to know that the same lead means far less at tick 2,000
than at tick 80,000, and this one was fitted with an explicit confidence ramp on
game age to enforce that. The ramp was then removed, because measurement said it
earned nothing: fitted on cross-validated log-loss it went nearly flat, and a
model with no ramp scored the same (0.5219 against 0.5220). The selected
features are absolute counts, so they already carry the phase -- early on
everybody has few units, the fitness differences are small of their own accord,
and the model is unsure without being told to be. Held out by game, predicted
and observed win rates agree to three decimals in every tick bucket.

What the ramp turned out to be was a second confidence dial, redundant with the
decision threshold: fitting it to 1.0 and calling at 97% gives the same error
and the same saving as leaving it flat and calling at 95%. One dial is enough,
and the threshold is the one a player can read.

Features come only from TeamStat -- the per-team state the simulation itself
maintains. The richer GameplayMeasurements and the per-AI telemetry are
deliberately excluded: both are documented as diagnostic only and must never
feed the simulation, and a winning condition built on this model does exactly
that. Anything fitted here has to be computable in-engine.

Commands:
  dataset  extract the per-sample dataset from tournament results and cache it
  screen   report what each candidate state measurement predicts on its own
  fit      select features, fit the coefficients and emit the C++ header
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
# Only used as a fallback when a game's record does not state its own tick limit.
DEFAULT_TICK_CAP = 90000
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
# something confusing about it; counts get the rest.
#
# `log` is deliberately absent. The engine has to evaluate this model inside the
# synchronised simulation, where the result decides a winning condition, so every
# step of it must give bit-identical answers on every platform. identity, share
# (an integer division) and sqrt (an integer square root) are exactly
# representable in fixed point; log1p would need a polynomial approximation in
# the one code path where being a bit off changes who wins. It buys nothing
# anyway: selecting with log available scored a cross-validated McFadden R2 of
# 0.4160 against 0.4149 without it, and slightly worse top-1 accuracy.
RATIO_TRANSFORMS = ('identity',)
COUNT_TRANSFORMS = ('identity', 'sqrt', 'share')

# The selected, hand-checked model. Order is the order terms were added, which is
# also decreasing order of what each one contributed.
FINAL_FEATURES = [
    ('units', 'share'),
    ('prestige', 'identity'),
    ('barracks', 'sqrt'),
    ('explorers', 'sqrt'),
    ('starving_ratio', 'identity'),
    ('attack', 'share'),
]

# What each term reads, for the in-game breakdown. Phrases a player can read.
LABELS = {
    'units': 'Share of everyone alive',
    'prestige': 'Prestige',
    'barracks': 'Barracks',
    'explorers': 'Explorers',
    'starving_ratio': 'Share of your people starving',
    'attack': 'Share of all attack strength',
}

# How the engine reads each measurement off a competitor, as a C++ expression on
# a WinProbabilitySlot. Every one is a non-negative integer.
CPP_MEASUREMENT = {
    'units': 'slot.units',
    'prestige': 'slot.prestige',
    'barracks': 'slot.barracks',
    'explorers': 'slot.explorers',
    'food_critical': 'slot.foodCritical',
    'attack': 'slot.attack',
}
# Ratios are a division of two of those, done as an integer division in the
# engine and as the same division here.
CPP_RATIO = {'starving_ratio': ('slot.foodCritical', 'slot.units')}


def transform_candidates(name):
    return RATIO_TRANSFORMS if name in RATIOS else COUNT_TRANSFORMS


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
    cap = (result.get('resolved') or {}).get('tick_limit') or result.get('ticks') or DEFAULT_TICK_CAP
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


def by_game(game):
    return game['job_id']


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------
def screen(data, folds=5, ridge=1e-3):
    """What each candidate measurement and transform predicts on its own."""
    rows = []
    for name in MEASUREMENTS:
        for transform in transform_candidates(name):
            score = cross_validated(data, [(name, transform)], folds, ridge, group=by_game)
            if not score.get('folds'):
                continue
            rows.append({'name': name, 'transform': transform, 'family': FAMILY_OF[name],
                         'cv_r2': score['mcfadden_r2'], 'cv_accuracy': score['accuracy'],
                         'cv_chance': score['chance_accuracy']})
    rows.sort(key=lambda row: -row['cv_r2'])
    return rows


def select(data, candidates, limit=6, folds=5, ridge=1e-3, tolerance=0.0005):
    """Greedy forward selection on cross-validated fit, one term per family."""
    chosen, used, history, current = [], set(), [], 0.0
    while len(chosen) < limit:
        trials = []
        for row in candidates:
            if row['name'] in used or row['family'] in used:
                continue
            option = chosen + [(row['name'], row['transform'])]
            score = cross_validated(data, option, folds, ridge, group=by_game)
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


def probabilities(model, data):
    """Per-contest fitness and win probability under a fitted model."""
    features = [(item['name'], item['transform']) for item in model['features']]
    coefficients = [item['coefficient'] for item in model['features']]
    columns = [feature_column(data, name, transform) for name, transform in features]
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


def savings(cache, features, threshold=0.97, dwell=3, folds=4, ridge=1e-3, seed=1):
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
        model = fit_model(train, features, ridge)
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


# ---------------------------------------------------------------------------
# The fixed-point model, mirrored exactly
# ---------------------------------------------------------------------------
# The engine evaluates this model inside the synchronised simulation, where the
# answer decides a winning condition, so it may not use floating point: two
# machines that disagree by one bit would end the same game on different ticks.
# Everything below is the integer algorithm src/WinProbability.cpp implements,
# written again in Python so a test can assert the two agree exactly rather than
# approximately. Python's ints are unbounded and its >> and // truncate towards
# negative infinity for positives just as C++ does on non-negative values, so
# the two really are the same arithmetic.
FITNESS_SHIFT = 32
FRACTION_SHIFT = 20
ROOT_SHIFT = 10
SERIES_SHIFT = 30
FITNESS_ONE = 1 << FITNESS_SHIFT
FRACTION_ONE = 1 << FRACTION_SHIFT
COUNT_CLAMP = 1 << 24
LOG_TWO_FIXED = 2977044472  # ln 2, with FITNESS_SHIFT fractional bits


def to_fixed(value, shift=FITNESS_SHIFT):
    """A coefficient as an integer, rounded here so the rounding is the fit's."""
    return int(math.floor(value * (1 << shift) + 0.5))


def clamp_count(value):
    return 0 if value < 0 else min(int(value), COUNT_CLAMP)


def share_value(value, total):
    return 0 if total <= 0 else (clamp_count(value) << FRACTION_SHIFT) // int(total)


def integer_sqrt(value):
    return 0 if value <= 0 else math.isqrt(int(value))


def root_value(value):
    return integer_sqrt(clamp_count(value) << (2 * ROOT_SHIFT))


def ratio_value(numerator, denominator):
    if denominator <= 0:
        return 0
    return min((clamp_count(numerator) << FRACTION_SHIFT) // int(denominator), FRACTION_ONE)


def exp_negative(value):
    """exp(-value), both sides with FITNESS_SHIFT fractional bits."""
    if value <= 0:
        return FITNESS_ONE
    whole = value // LOG_TWO_FIXED
    if whole >= FITNESS_SHIFT + 1:
        return 0
    rest = value - whole * LOG_TWO_FIXED
    series_one = 1 << SERIES_SHIFT
    r = rest >> (FITNESS_SHIFT - SERIES_SHIFT)
    term = series_one
    for n in range(10, 1, -1):
        term = series_one - ((r * term) >> SERIES_SHIFT) // n
    result = series_one - ((r * term) >> SERIES_SHIFT)
    result = max(result, 0)
    return (result << (FITNESS_SHIFT - SERIES_SHIFT)) >> whole


def fixed_term(model, slots, index, term):
    """One term's contribution to a slot's fitness, as the engine computes it."""
    item = model['features'][term]
    name, transform = item['name'], item['transform']
    coefficient = to_fixed(item['coefficient'])
    slot = slots[index]
    if name in RATIOS:
        top, bottom = RATIOS[name]
        return (coefficient * ratio_value(slot[top], slot[bottom])) >> FRACTION_SHIFT
    if transform == 'share':
        total = sum(clamp_count(other[name]) for other in slots if other.get('alive', True))
        return (coefficient * share_value(slot[name], total)) >> FRACTION_SHIFT
    if transform == 'sqrt':
        return (coefficient * root_value(slot[name])) >> ROOT_SHIFT
    return coefficient * clamp_count(slot[name])


def fixed_fitness(model, slots, index):
    value = to_fixed(model['intercept'])
    for term in range(len(model['features'])):
        value += fixed_term(model, slots, index, term)
    return value


def fixed_permille(model, slots):
    """Each slot's win chance in permille, as the engine reports it."""
    alive = [i for i, slot in enumerate(slots) if slot.get('alive', True)]
    result = [0] * len(slots)
    if not alive:
        return result
    fitness = {i: fixed_fitness(model, slots, i) for i in alive}
    best = max(fitness.values())
    weights = {i: exp_negative(best - fitness[i]) for i in alive}
    total = sum(weights.values())
    if total <= 0:
        return result
    for i in alive:
        result[i] = (weights[i] * 1000) // total
    return result


# ---------------------------------------------------------------------------
# C++ emission
# ---------------------------------------------------------------------------
HEADER_PATH = 'src/WinProbabilityModel.h'


def constant_name(name, transform):
    return f'WIN_PROBABILITY_{name.upper()}_{transform.upper()}'


def cpp_kind(name, transform):
    """How one term is read in C++: (kind, expression(s), shift)."""
    if name in RATIOS:
        if name not in CPP_RATIO:
            raise ValueError(f'{name} has no engine expression; add one to CPP_RATIO')
        top, bottom = CPP_RATIO[name]
        return 'ratio', (top, bottom), 'FRACTION_SHIFT'
    if name not in CPP_MEASUREMENT:
        raise ValueError(f'{name} has no engine expression; add one to CPP_MEASUREMENT')
    expression = CPP_MEASUREMENT[name]
    if transform == 'share':
        return 'share', (expression,), 'FRACTION_SHIFT'
    if transform == 'sqrt':
        return 'sqrt', (expression,), 'ROOT_SHIFT'
    if transform == 'identity':
        return 'identity', (expression,), None
    # Anything else would need an approximation in the one code path that must be
    # exact on every platform. Refuse rather than emit it.
    raise ValueError(f'transform {transform!r} has no exact integer form; '
                     'it must not reach the winning condition')


def emit_header(model, path, provenance):
    features = model['features']
    lines = []
    add = lines.append
    add('// SPDX-License-Identifier: GPL-3.0-or-later')
    add('// Generated by tools/win_probability_model.py -- do not edit by hand.')
    add('//')
    add("// How likely each side is to win, from the state of play. Every competitor")
    add('// gets a fitness F; its probability of winning is softmax(F) over the sides')
    add('// still standing. Allies are one competitor, because they win together.')
    add('//')
    for line in provenance:
        add(f'// {line}')
    add('// See docs/win-probability-model.md.')
    add('#pragma once')
    add('#include "WinProbability.h"')
    add('#include <cstddef>')
    add('#include <vector>')
    add('')
    add('namespace WinProbability')
    add('{')
    add('/// Softmax fixes the scale of F but not its zero, so this level is a')
    add('/// convention and cancels between competitors. Differences are what the')
    add('/// games determined.')
    add(f'constexpr double WIN_PROBABILITY_INTERCEPT = {model["intercept"]!r};')
    add(f'const Sint64 WIN_PROBABILITY_INTERCEPT_FIXED = {to_fixed(model["intercept"])}LL;')
    add('')
    for item in features:
        name, transform = item['name'], item['transform']
        constant = constant_name(name, transform)
        add(f'/// {name} ({transform})')
        add(f'constexpr double {constant} = {item["coefficient"]!r};')
        add(f'const Sint64 {constant}_FIXED = {to_fixed(item["coefficient"])}LL;')
    add('')
    add(f'constexpr int WIN_PROBABILITY_FEATURE_COUNT = {len(features)};')
    add(f'constexpr int WIN_PROBABILITY_GAMES = {model["games"]};')
    add(f'constexpr int WIN_PROBABILITY_SAMPLES = {model["samples"]};')
    add('')
    add('/// The fitted terms by name, for the statistics screen\'s breakdown.')
    add('struct WinProbabilityTerm')
    add('{')
    add('\tconst char *name;')
    add('\tconst char *transform;')
    add('\tconst char *label;   ///< a phrase a player can read')
    add('\tdouble coefficient;')
    add('};')
    add('inline const WinProbabilityTerm *winProbabilityTerms()')
    add('{')
    add('\tstatic const WinProbabilityTerm terms[] = {')
    for item in features:
        name, transform = item['name'], item['transform']
        label = LABELS.get(name, name)
        add(f'\t\t{{"{name}", "{transform}", "{label}", {constant_name(name, transform)}}},')
    add('\t};')
    add('\treturn terms;')
    add('}')
    add('')
    add('/// One term\'s contribution to a competitor\'s fitness, in fixed point.')
    add('///')
    add('/// Integer arithmetic throughout: this is read by the optional win')
    add('/// probability victory condition from inside the synchronised simulation, so')
    add('/// every platform has to reach the same answer bit for bit.')
    add('inline Sint64 winProbabilityTermFixed(const std::vector<Slot> &slots, std::size_t index, int term)')
    add('{')
    add('\tconst Slot &slot = slots[index];')
    add('\tswitch (term)')
    add('\t{')
    for position, item in enumerate(features):
        name, transform = item['name'], item['transform']
        kind, expressions, shift = cpp_kind(name, transform)
        constant = constant_name(name, transform) + '_FIXED'
        add(f'\tcase {position}: // {name} ({transform})')
        if kind == 'identity':
            add(f'\t\treturn {constant} * countValue({expressions[0]});')
        elif kind == 'sqrt':
            add(f'\t\treturn ({constant} * rootValue({expressions[0]})) >> {shift};')
        elif kind == 'ratio':
            top, bottom = expressions
            add(f'\t\treturn ({constant} * ratioValue({top}, {bottom})) >> {shift};')
        else:
            field = expressions[0].split('.', 1)[1]
            add('\t{')
            add('\t\tSint64 total = 0;')
            add('\t\tfor (std::size_t i = 0; i < slots.size(); ++i)')
            add('\t\t\tif (slots[i].alive)')
            add(f'\t\t\t\ttotal += clampCount(slots[i].{field});')
            add(f'\t\treturn ({constant} * shareValue({expressions[0]}, total)) >> {shift};')
            add('\t}')
    add('\t}')
    add('\treturn 0;')
    add('}')
    add('')
    add('/// A competitor\'s fitness, given every competitor still standing.')
    add('inline Sint64 winProbabilityFitness(const std::vector<Slot> &slots, std::size_t index)')
    add('{')
    add('\tSint64 fitness = WIN_PROBABILITY_INTERCEPT_FIXED;')
    add('\tfor (int term = 0; term < WIN_PROBABILITY_FEATURE_COUNT; ++term)')
    add('\t\tfitness += winProbabilityTermFixed(slots, index, term);')
    add('\treturn fitness;')
    add('}')
    add('')
    add('/// What one term measures, before its coefficient: the number the')
    add('/// breakdown shows next to the term. Display only, so plain arithmetic.')
    add('inline double winProbabilityMeasurement(const std::vector<Slot> &slots, std::size_t index, int term)')
    add('{')
    add('\tconst Slot &slot = slots[index];')
    add('\tswitch (term)')
    add('\t{')
    for position, item in enumerate(features):
        name, transform = item['name'], item['transform']
        kind, expressions, _ = cpp_kind(name, transform)
        add(f'\tcase {position}:')
        if kind == 'ratio':
            top, bottom = expressions
            add(f'\t\treturn {bottom} > 0 ? double({top}) / double({bottom}) : 0.0;')
        elif kind == 'share':
            field = expressions[0].split('.', 1)[1]
            add('\t{')
            add('\t\tdouble total = 0;')
            add('\t\tfor (std::size_t i = 0; i < slots.size(); ++i)')
            add('\t\t\tif (slots[i].alive)')
            add(f'\t\t\t\ttotal += double(slots[i].{field});')
            add(f'\t\treturn total > 0 ? double({expressions[0]}) / total : 0.0;')
            add('\t}')
        else:
            add(f'\t\treturn double({expressions[0]});')
    add('\t}')
    add('\treturn 0;')
    add('}')
    add('')
    add('/// That term\'s share of the fitness, as a readable number. Display only.')
    add('inline double winProbabilityContribution(const std::vector<Slot> &slots, std::size_t index, int term)')
    add('{')
    add('\treturn double(winProbabilityTermFixed(slots, index, term)) / double(FITNESS_ONE);')
    add('}')
    add('} // namespace WinProbability')
    Path(path).write_text('\n'.join(lines) + '\n')
    return len(lines)
