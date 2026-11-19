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

Features come from TeamStat and from the published gameplay measurements, both
computed by the simulation at fixed sample deadlines, so the engine can evaluate
exactly what was fitted. The measurement values the model reads are covered by
the routine statistics checksum because a winning condition acts on them. Windowed
measurements are per-team changes over WINDOW_SAMPLES samples, taken before allies
are summed. At a decision tick T the engine has not yet published the gameplay
sample for T, so the model reads the one for T - 512, and the fitter joins that
same sample to the decision. Per-AI telemetry stays excluded. Anything fitted here has to be
computable in-engine.

Collect calibration/refit games with --win-probability 0 (or the tournament
setting win_probability_permille=0). Probability-called winners and final-
probability adjudication are predictions, not independent outcome labels.

Programmatic entry points:
  build_dataset  extract the per-sample dataset from tournament results
  screen/select compare candidate measurements and select fitted terms
  emit_header   emit the fitted model as the C++ header
  savings       estimate early-victory savings on independently labelled games
"""
import gzip
import hashlib
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

SCHEMA_VERSION = 2
# The cadence TeamStats samples at (END_OF_GAME_STAT_INTERVAL_MASK in
# src/team/stats/TeamStat.h). The model is only ever evaluated on these boundaries, in the
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
# evaluated inside a running game. Historical building-column names are semantic
# service counts in catalog-aware output, not concrete family IDs; hybrids may
# appear in more than one field, so these columns are never summed as a total.
TIMELINE_FIELDS = {
    'units': ('tl', 'units'), 'buildings': ('tl', 'bld'), 'prestige': ('tl', 'prestige'),
    'hp': ('tl', 'hp'), 'attack': ('tl', 'atk'), 'defense': ('tl', 'def'),
    'workers': ('econ', 'workers'), 'warriors': ('econ', 'warriors'),
    'explorers': ('econ', 'explorers'), 'food_critical': ('econ', 'foodCritical'),
    'need_food': ('econ', 'needFood'), 'swarms': ('econ', 'swarm'), 'inns': ('econ', 'inn'),
    'schools': ('econ', 'school'), 'barracks': ('econ', 'barracks'), 'towers': ('econ', 'tower'),
}
# Gameplay measurements the engine reads (WinProbability::teamInputs), from the
# GLOB2_MEASURE record of the periodic sample published before the decision:
# PUBLICATION_LAG_SAMPLES behind it. Windowed values are the change over
# WINDOW_SAMPLES samples (WinProbability::ACTIVITY_WINDOW_SAMPLES). The layout
# constants below mirror the C++ enums; a test checks them against the headers.
WINDOW_SAMPLES = 4
PUBLICATION_LAG_SAMPLES = 1
GAMEPLAY_MEASUREMENTS = ('feeding', 'barracks_levels', 'school_levels', 'delivered_window',
                         'inn_walk_distance_window', 'swarm_filling_window')
ATTACK_BUILDING, SCIENCE_BUILDING, BUILDING_LEVELS, MATERIAL_SLOTS, SWARM_JOB, LABOUR_PHASES = 5, 6, 6, 15, 0, 4

# Fields the engine prints as "stored/capacity"; split into two measurements.
PAIR_FIELDS = {'food': ('food_stored', 'food_capacity'), 'fooded': ('fed', 'feedable')}

RATIOS = {
    'warrior_ratio': ('warriors', 'units'),
    'fed_ratio': ('fed', 'feedable'),
    'food_fill': ('food_stored', 'food_capacity'),
    'starving_ratio': ('food_critical', 'units'),
}

# Prestige still needed to win (Game::prestigeNeeded), from GLOB2_TL. Recorded
# only while prestige can end the game, so a missing value is 0 for every side
# and the term cancels, as in the engine. Allies take their closest member's
# value rather than a sum: it is a distance, not an amount.
PRESTIGE_NEEDED = 'prestige_needed'
MIN_OVER_ALLIES = (PRESTIGE_NEEDED,)
# Own prestige less the best surviving opponent's (Game::prestigeLead), from
# GLOB2_TL: the leader's margin, or a deficit to the leader. Signed, the only
# signed measurement; allies take their furthest-ahead member. Recorded, and 0
# otherwise, on the same terms as prestige needed.
PRESTIGE_LEAD = 'prestige_lead'
MAX_OVER_ALLIES = (PRESTIGE_LEAD,)
SIGNED = (PRESTIGE_LEAD,)

MEASUREMENTS = tuple(TIMELINE_FIELDS) + tuple(
    name for pair in PAIR_FIELDS.values() for name in pair) + tuple(RATIOS) + GAMEPLAY_MEASUREMENTS + (
    PRESTIGE_NEEDED, PRESTIGE_LEAD)

# Measurements that say the same thing in different words. Greedy selection takes
# one term per family so it cannot fill a model with readings of the same army;
# the stepwise refinement that follows may add a second term from a family when
# cross-validated fit says it carries separate information, and the published
# model has two economy and two food terms.
FAMILIES = {
    'army': ('hp', 'attack', 'defense', 'warriors', 'warrior_ratio'),
    'economy': ('units', 'workers', 'buildings', 'swarms', 'delivered_window', 'swarm_filling_window'),
    'food': ('food_stored', 'food_capacity', 'fed', 'feedable', 'fed_ratio', 'food_fill',
             'food_critical', 'need_food', 'inns', 'starving_ratio', 'feeding',
             'inn_walk_distance_window'),
    'prestige': ('prestige', PRESTIGE_NEEDED, PRESTIGE_LEAD),
    'schooling': ('schools', 'school_levels'),
    'fortification': ('barracks', 'towers', 'barracks_levels'),
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
SIGNED_TRANSFORMS = ('identity', 'slog', 'ssqrt', 'spow25')

# A measurement with a hard upper bound can use any transform exactly: the
# emitter writes its fixed-point values for every integer 0..bound into the
# header, and the engine and fixed_term below read the same table. Values above
# the bound are clamped to it, in the fit as in the engine. Prestige needed can
# never exceed the largest prestige goal, MIN/TEAM_MAX_PRESTIGE in Game.h times
# TEAM_MAX_COUNT teams.
TABLE_SHIFT = 20
TABLE_BOUNDS = {PRESTIGE_NEEDED: 150 * 16, PRESTIGE_LEAD: 150 * 16}
TABLE_TRANSFORMS = {
    'log': math.log1p,
    'pow25': lambda x: x ** 0.25,
}
# A signed measurement's table holds the transform of its magnitude; the engine
# and fixed_term restore the sign (WinProbability::signedTableValue).
SIGNED_TABLE_TRANSFORMS = {
    'slog': math.log1p,
    'ssqrt': math.sqrt,
    'spow25': lambda x: x ** 0.25,
}

# The selected, hand-checked model: greedy forward selection followed by
# stepwise add/drop/swap refinement on cross-validated fit. Ordered by how much a
# typical within-contest gap on the term moves the odds, largest first.
FINAL_FEATURES = [
    ('delivered_window', 'share'),
    ('feeding', 'sqrt'),
    ('school_levels', 'identity'),
    (PRESTIGE_LEAD, 'slog'),
    (PRESTIGE_NEEDED, 'log'),
    ('attack', 'share'),
    ('swarm_filling_window', 'sqrt'),
    ('explorers', 'sqrt'),
    ('inn_walk_distance_window', 'share'),
    ('barracks_levels', 'identity'),
]

# Every published coefficient moves linearly through the game:
# beta(t) = coefficient + slope * min(t, TIME_CAP_TICKS) / TIME_CAP_TICKS, where t
# is the decision tick. The game's length is unknown while it is played, so the
# clock is absolute ticks, clamped at the tournament cap the games ran to. A
# straight line keeps the fit a conditional logit (each term gains a column scaled
# by the clock) and the engine exact (one multiply and one division per term).
# Kept in step with WinProbability::TIME_CAP_TICKS.
TIME_CAP_TICKS = 180000


def time_fraction(tick):
    return min(max(tick, 0), TIME_CAP_TICKS) / TIME_CAP_TICKS


def time_column(dataset, name, transform):
    """Columns named NAME@t are the transformed measurement scaled by the clock."""
    if not name.endswith('@t'):
        return feature_column(dataset, name, transform)
    values = feature_column(dataset, name[:-2], transform)
    return [[value * time_fraction(game['tick']) for value in row] for row, game in zip(values, dataset['games'])]


# The terms whose coefficient moves with the clock. Forward selection on
# game-grouped CV over all ten slopes kept only this one: it alone lifted CV R2
# from 0.284 to 0.291 (every 4th sample), more than all ten slopes together
# (0.290); no other slope added 0.0005. Every other term has a slope of 0.
TIMED_TERMS = (PRESTIGE_LEAD,)


def published_columns(features, timed=TIMED_TERMS):
    return list(features) + [(name + '@t', transform) for name, transform in features if name in timed]


def fit_published(dataset, features, ridge=1e-3, timed=TIMED_TERMS):
    """Fit the published form: each term's coefficient, and a clock slope for the timed ones."""
    model = fit_model(dataset, published_columns(features, timed), ridge, column=time_column)
    by_name = {item['name']: item['coefficient'] for item in model['features']}
    model['features'] = [{'name': name, 'transform': transform, 'coefficient': by_name[name],
                          'slope': by_name.get(name + '@t', 0.0)} for name, transform in features]
    return model

# What each term reads, for the in-game breakdown. Phrases a player can read.
LABELS = {
    'barracks_levels': 'Barracks, counted by level',
    'school_levels': 'Schools, counted by level',
    'explorers': 'Explorers',
    'attack': 'Share of all attack strength',
    'feeding': 'Units eating at inns',
    'delivered_window': 'Share of recent deliveries',
    'swarm_filling_window': 'Recent work feeding swarms',
    'inn_walk_distance_window': 'Share of recent walking to inns',
    PRESTIGE_NEEDED: 'Prestige still needed to win',
    PRESTIGE_LEAD: 'Prestige lead over the best opponent',
}

# How the engine reads each measurement off a competitor, as a C++ expression on
# a WinProbability::Slot. Every one is a non-negative integer.
CPP_MEASUREMENT = {
    'feeding': 'slot.feeding',
    'explorers': 'slot.explorers',
    'barracks_levels': 'slot.barracksLevels',
    'school_levels': 'slot.schoolLevels',
    'delivered_window': 'slot.delivered',
    'inn_walk_distance_window': 'slot.innWalkDistance',
    'swarm_filling_window': 'slot.swarmFilling',
    'attack': 'slot.attack',
    PRESTIGE_NEEDED: 'slot.prestigeNeeded',
    PRESTIGE_LEAD: 'slot.prestigeLead',
}
# Ratios are a division of two of those, done as an integer division in the
# engine and as the same division here. The published model uses none.
CPP_RATIO = {}


def transform_candidates(name):
    if name in SIGNED:
        return SIGNED_TRANSFORMS
    return RATIO_TRANSFORMS if name in RATIOS else COUNT_TRANSFORMS


# ---------------------------------------------------------------------------
# Dataset
# ---------------------------------------------------------------------------
def gameplay_fields(line):
    """The raw GLOB2_MEASURE counters the engine's model inputs are built from.

    A measurement record carries well over a thousand fields; only these are
    kept, and the rest are never converted.
    """
    wanted = {'feeding', 'eatWalkDistance'}
    wanted.update(f'buildings_{family}_{level}' for family in (ATTACK_BUILDING, SCIENCE_BUILDING)
                  for level in range(BUILDING_LEVELS))
    wanted.update(f'delivered_{material}' for material in range(MATERIAL_SLOTS))
    wanted.update(f'filling_{SWARM_JOB}_{phase}' for phase in range(LABOUR_PHASES))
    wanted.update(('team', 'tick', 'final'))
    fields = {}
    for token in line.split()[1:]:
        key, _, value = token.partition('=')
        if key in wanted:
            fields[key] = int(value)
    return fields


def timeline(source, record):
    """Per-team state by tick, from one game's verified log.

    TeamStat state comes from GLOB2_TL/GLOB2_ECON and gameplay measurements from
    the periodic (non-final) GLOB2_MEASURE records. The prefix test comes before
    the parser because a game's log is tens of megabytes of mostly performance
    and per-AI samples, and this has to run over a thousand games.
    """
    samples = defaultdict(dict)
    with source.open_artifact(record, 'stdout.log') as stream:
        for line in stream:
            if line.startswith('GLOB2_MEASURE '):
                fields = gameplay_fields(line)
                if fields.get('final') == 0 and 'tick' in fields and 'team' in fields:
                    samples[(fields['tick'], fields['team'])]['gameplay'] = fields
                continue
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


def gameplay_inputs(now, then):
    """The engine's WinProbability::teamInputs, from one team's raw records.

    `then` is the same team's record WINDOW_SAMPLES samples earlier, or None
    when it was not retained, in which case the window runs from coverage start.
    """
    then = then or {}

    def window(key):
        return float(max(now.get(key, 0) - then.get(key, 0), 0))

    return {
        'feeding': float(now.get('feeding', 0)),
        'barracks_levels': float(sum(now.get(f'buildings_{ATTACK_BUILDING}_{level}', 0) * (level + 1)
                                     for level in range(BUILDING_LEVELS))),
        # Schools and their sites, weighted like barracks: 1 (level-1 site) to 6.
        'school_levels': float(sum(now.get(f'buildings_{SCIENCE_BUILDING}_{level}', 0) * (level + 1)
                                   for level in range(BUILDING_LEVELS))),
        'delivered_window': sum(window(f'delivered_{material}') for material in range(MATERIAL_SLOTS)),
        'inn_walk_distance_window': window('eatWalkDistance'),
        'swarm_filling_window': sum(window(f'filling_{SWARM_JOB}_{phase}') for phase in range(LABOUR_PHASES)),
    }


def measurements_of(pair, published=None, earlier=None):
    """One entry's measurements, or None if a TeamStat record type is missing.

    `published` is the team's sample PUBLICATION_LAG_SAMPLES before this one --
    the gameplay record the engine can read at this decision -- and `earlier` the
    one WINDOW_SAMPLES before that. Either may be None; with no published sample
    the gameplay inputs are zero, as in the engine.
    """
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
    needed = pair['tl'].get('prestigeNeeded', 0)
    # Clamped to its table bound here so the fit sees what the engine reads.
    values[PRESTIGE_NEEDED] = float(min(max(needed, 0), TABLE_BOUNDS[PRESTIGE_NEEDED])) if isinstance(needed, int) else 0.0
    lead, bound = pair['tl'].get('prestigeLead', 0), TABLE_BOUNDS[PRESTIGE_LEAD]
    values[PRESTIGE_LEAD] = float(min(max(lead, -bound), bound)) if isinstance(lead, int) else 0.0
    values.update(gameplay_inputs((published or {}).get('gameplay', {}), (earlier or {}).get('gameplay')))
    return values


CALIBRATION_POLICIES = ('prestige', 'survivor_draw', 'military')


def _validate_calibration_labels(policy, game=None):
    if policy not in CALIBRATION_POLICIES:
        raise ValueError('probability-model calibration requires independent outcome labels; '
                         'use prestige, survivor_draw or military, not win_probability')
    if game is not None and game.get('termination') == 'win_probability':
        raise ValueError('probability-called games cannot label probability-model calibration; '
                         'collect games with --win-probability 0 (win_probability_permille=0)')


def _validate_calibration_dataset(data):
    policy = data.get('policy', 'prestige')
    _validate_calibration_labels(policy)
    for game in data.get('games', []):
        _validate_calibration_labels(policy, game)


def group_allies(by_team, key_of):
    """One competitor's measurements from its teams': summed, except distances."""
    grouped = defaultdict(lambda: defaultdict(float))
    for team, values in by_team.items():
        entry = grouped[key_of[team]]
        for name, value in values.items():
            if name in MIN_OVER_ALLIES and name in entry:
                entry[name] = min(entry[name], value)
            elif name in MAX_OVER_ALLIES and name in entry:
                entry[name] = max(entry[name], value)
            else:
                entry[name] += value
    return grouped


def game_slices(source, record, policy='prestige'):
    """One contest per 512-tick sample of one game, labelled by how it ended.

    The label is the game's finishing order, not anything about the sample: that
    is the whole point, since what we want to predict at tick T is the result
    that only arrives later. Tick-capped games default to the historical economic
    adjudication, keeping their labels independent of the model being fitted.
    Probability-ended games and probability-based labels are rejected.
    """
    result = record.get('result') or {}
    _validate_calibration_labels(policy, result)
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
        lag = PUBLICATION_LAG_SAMPLES * SAMPLE_INTERVAL
        published = samples.get((tick - lag, team))
        earlier = samples.get((tick - lag - WINDOW_SAMPLES * SAMPLE_INTERVAL, team))
        values = measurements_of(pair, published, earlier)
        if values is not None:
            by_tick[tick][team] = values
    slices = []
    for tick in sorted(by_tick):
        # Allies are one competitor: they win or lose together, so counting them
        # as two independent entries would double-count the evidence.
        grouped = group_allies(by_tick[tick], key_of)
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
    """Extract independent labels; reject model-called outcomes, including old logs."""
    _validate_calibration_labels(policy)
    games, skipped = [], defaultdict(int)
    for root in roots:
        source = Results(root)
        for record in source:
            if limit is not None and len(games) >= limit:
                break
            if not record.get('accepted') or (record.get('result') or {}).get('job_type') != 'game':
                skipped['not an accepted game'] += 1
                continue
            # Reject before the unreadable-log handler: this is an unsuitable
            # experiment design, not missing diagnostic data to skip silently.
            _validate_calibration_labels(policy, record['result'])
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
        data = json.load(stream)
    _validate_calibration_dataset(data)
    return data


def contests(cache, stride=1, formats=None, minimum_tick=0):
    """Flatten the cached games into the estimator's contest list."""
    _validate_calibration_dataset(cache)
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
    return {'games': games, 'policy': cache.get('policy', 'prestige'), 'skipped': cache.get('skipped', {})}


def by_game(game):
    return game['job_id']


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------
def screen(data, folds=5, ridge=1e-3):
    """What each candidate measurement and transform predicts on its own."""
    _validate_calibration_dataset(data)
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
    _validate_calibration_dataset(data)
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
    columns = [feature_column(data, name, transform) for name, transform in features]
    output = []
    for position, game in enumerate(data['games']):
        clock = time_fraction(game['tick'])
        coefficients = [item['coefficient'] + item.get('slope', 0.0) * clock for item in model['features']]
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
    _validate_calibration_dataset(cache)
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
        model = fit_published(train, features, ridge)
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
    _validate_calibration_dataset(data)
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
# Everything below is the integer algorithm src/team/stats/WinProbability.cpp implements,
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


def truncating_divide(numerator, denominator):
    """C++ integer division: rounds toward zero, unlike Python's //."""
    quotient = abs(numerator) // abs(denominator)
    return quotient if (numerator >= 0) == (denominator > 0) else -quotient


def fixed_coefficient(item, tick):
    """A term's fixed-point coefficient at a decision tick (WinProbability::timeScaled)."""
    clock = min(max(int(tick), 0), TIME_CAP_TICKS)
    return to_fixed(item['coefficient']) + truncating_divide(to_fixed(item.get('slope', 0.0)) * clock, TIME_CAP_TICKS)


def fixed_term(model, slots, index, term):
    """One term's contribution to a slot's fitness, as the engine computes it."""
    item = model['features'][term]
    name, transform = item['name'], item['transform']
    slot = slots[index]
    coefficient = fixed_coefficient(item, slot.get('tick', 0))
    if name in RATIOS:
        top, bottom = RATIOS[name]
        return (coefficient * ratio_value(slot[top], slot[bottom])) >> FRACTION_SHIFT
    if transform == 'share':
        total = sum(clamp_count(other[name]) for other in slots if other.get('alive', True))
        return (coefficient * share_value(slot[name], total)) >> FRACTION_SHIFT
    if transform == 'sqrt':
        return (coefficient * root_value(slot[name])) >> ROOT_SHIFT
    if transform in SIGNED_TABLE_TRANSFORMS:
        return (coefficient * signed_table_value(table_of(name, transform), slot[name])) >> TABLE_SHIFT
    if transform in TABLE_TRANSFORMS:
        return (coefficient * table_of(name, transform)[table_index(name, slot[name])]) >> TABLE_SHIFT
    if name in SIGNED:
        return coefficient * signed_count(slot[name])
    return coefficient * clamp_count(slot[name])


def table_of(name, transform):
    """The fixed-point values of a bounded measurement's transform, 0..bound."""
    function = SIGNED_TABLE_TRANSFORMS.get(transform) or TABLE_TRANSFORMS[transform]
    return [int(round(function(float(i)) * (1 << TABLE_SHIFT))) for i in range(TABLE_BOUNDS[name] + 1)]


def table_index(name, value):
    return min(clamp_count(value), TABLE_BOUNDS[name])


def signed_count(value):
    """WinProbability::signedCountValue."""
    return max(-COUNT_CLAMP, min(int(value), COUNT_CLAMP))


def signed_table_value(table, value):
    """WinProbability::signedTableValue: table[|value|] with value's sign."""
    value = int(value)
    entry = table[min(abs(value), len(table) - 1)]
    return -entry if value < 0 else entry


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
HEADER_PATH = 'src/team/stats/WinProbabilityModel.h'


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
    if name in SIGNED:
        # share and sqrt read magnitudes; a signed value has only odd forms.
        if transform == 'identity':
            return 'signed_identity', (expression,), None
        if transform in SIGNED_TABLE_TRANSFORMS:
            if name not in TABLE_BOUNDS:
                raise ValueError(f'{name} has no bound, so {transform!r} cannot be tabulated exactly')
            return 'signed_table', (expression,), 'TABLE_SHIFT'
        raise ValueError(f'{name} is signed; {transform!r} is not an odd transform it can take')
    if transform == 'share':
        return 'share', (expression,), 'FRACTION_SHIFT'
    if transform == 'sqrt':
        return 'sqrt', (expression,), 'ROOT_SHIFT'
    if transform in SIGNED_TABLE_TRANSFORMS:
        raise ValueError(f'{transform!r} is for signed measurements; {name} is a count')
    if transform == 'identity':
        return 'identity', (expression,), None
    if transform in TABLE_TRANSFORMS:
        if name not in TABLE_BOUNDS:
            raise ValueError(f'{name} has no bound, so {transform!r} cannot be tabulated exactly')
        return 'table', (expression,), 'TABLE_SHIFT'
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
    add('// See docs/ai/architecture/win-probability-model.md.')
    add('#pragma once')
    add('#include "WinProbability.h"')
    if any(cpp_kind(f['name'], f['transform'])[0] == 'table' for f in features):
        add('#include <algorithm>')
    add('#include <cstddef>')
    add('#include <vector>')
    add('')
    add('namespace WinProbability')
    add('{')
    identity = json.dumps({'intercept': to_fixed(model['intercept']),
                           'features': [(f['name'], f['transform'], to_fixed(f['coefficient']),
                                         to_fixed(f.get('slope', 0.0))) for f in features]},
                          sort_keys=True, separators=(',', ':')).encode()
    add('inline constexpr char MODEL_ID[] = "conditional-logit-v1-' + hashlib.sha256(identity).hexdigest() + '";')
    add('/// Softmax fixes the scale of F but not its zero, so this level is a')
    add('/// convention and cancels between competitors. Differences are what the')
    add('/// games determined.')
    add(f'constexpr double WIN_PROBABILITY_INTERCEPT = {model["intercept"]!r};')
    add(f'const Sint64 WIN_PROBABILITY_INTERCEPT_FIXED = {to_fixed(model["intercept"])}LL;')
    add('')
    for item in features:
        name, transform = item['name'], item['transform']
        constant = constant_name(name, transform)
        add(f'/// {name} ({transform}): coefficient at tick 0, and its change by TIME_CAP_TICKS')
        add(f'constexpr double {constant} = {item["coefficient"]!r};')
        add(f'const Sint64 {constant}_FIXED = {to_fixed(item["coefficient"])}LL;')
        add(f'constexpr double {constant}_SLOPE = {item.get("slope", 0.0)!r};')
        add(f'const Sint64 {constant}_SLOPE_FIXED = {to_fixed(item.get("slope", 0.0))}LL;')
        kind = cpp_kind(name, transform)[0]
        if kind in ('table', 'signed_table'):
            values = table_of(name, transform)
            if kind == 'table':
                add(f'/// {transform}(value) for value 0..{len(values) - 1}, with TABLE_SHIFT fractional bits;')
                add('/// larger values read the last entry. Generated, so every platform reads the same numbers.')
            else:
                add(f'/// {transform}: the transform of |value| for 0..{len(values) - 1}, with TABLE_SHIFT fractional')
                add('/// bits; larger magnitudes read the last entry, and the value\'s sign is restored.')
            add(f'constexpr Sint64 {constant}_TABLE_LIMIT = {len(values) - 1};')
            add(f'inline constexpr Sint32 {constant}_TABLE[{len(values)}] = {{')
            for start in range(0, len(values), 12):
                add('\t' + ', '.join(str(v) for v in values[start:start + 12]) + ',')
            add('};')
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
    add('\tdouble coefficient;   ///< at tick 0')
    add('\tdouble slope;         ///< change by TIME_CAP_TICKS')
    add('};')
    add('inline const WinProbabilityTerm *winProbabilityTerms()')
    add('{')
    add('\tstatic const WinProbabilityTerm terms[] = {')
    for item in features:
        name, transform = item['name'], item['transform']
        label = LABELS.get(name, name)
        add(f'\t\t{{"{name}", "{transform}", "{label}", {constant_name(name, transform)}, {constant_name(name, transform)}_SLOPE}},')
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
        constant = 'coefficient'
        add(f'\tcase {position}: // {name} ({transform})')
        add('\t{')
        add(f'\t\tconst Sint64 coefficient = {constant_name(name, transform)}_FIXED + '
            f'timeScaled({constant_name(name, transform)}_SLOPE_FIXED, slot.tick);')
        if kind == 'identity':
            add(f'\t\treturn {constant} * countValue({expressions[0]});')
        elif kind == 'signed_identity':
            add(f'\t\treturn {constant} * signedCountValue({expressions[0]});')
        elif kind == 'signed_table':
            table = constant_name(name, transform) + '_TABLE'
            add(f'\t\treturn ({constant} * signedTableValue({table}, {table}_LIMIT, {expressions[0]})) >> {shift};')
        elif kind == 'sqrt':
            add(f'\t\treturn ({constant} * rootValue({expressions[0]})) >> {shift};')
        elif kind == 'table':
            table = constant_name(name, transform) + '_TABLE'
            add(f'\t\treturn ({constant} * {table}[std::min<Sint64>(countValue({expressions[0]}), {table}_LIMIT)]) >> {shift};')
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
