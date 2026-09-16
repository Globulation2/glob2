#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the map fairness model to real games.

The tournament plays free-for-all games on randomly drawn maps -- random
generator, random controls, random size, random colony count -- with the *same*
AI in every slot, so what separates the players is their starting position and
nothing else. Each game contributes the order its colonies finished in.

The model is a conditional logit (McFadden) over start positions: every start
gets a fitness

    F_i = intercept + sum_k coefficient_k * feature_k(start_i)

and the probability that start i wins its game is softmax(F)_i. Fitting
maximises the Plackett-Luce likelihood of the observed finishing orders, which
is the same model read off the whole ranking instead of the winner alone.

Map fairness is then 1 - G(p), the normalised Gini coefficient of the predicted
win probabilities: 1 when every colony is equally likely to win, 0 when one
colony takes everything.

Commands:
  run      draw a fresh tournament and play it on the given hosts
  extend   add another round of games to an existing tournament
  status   report progress of every round
  collect  one collection pass without dispatching new work
  fit      fit the model to the played rounds (explore or final mode)

`fit --mode explore` screens every available start measurement and its
transforms and reports what predicts winning. `fit --mode final` fits the
screened, hand-checked feature list and writes the C++ coefficient header.
"""
import argparse
import itertools
import json
import math
import os
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json, read_json
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job, validate_experiment
from tools.tournaments.results import Results

SCHEMA_VERSION = 1
# Colony counts, and the size envelope games are drawn from. Width and height
# are power-of-two exponents, as everywhere else in the generator interface.
COLONY_CHOICES = (2, 3, 4, 5, 6, 7, 8)
EXPONENTS = (6, 7, 8, 9)
# Weights keep the run affordable: a 512x512 game costs roughly sixty times a
# 64x64 one, so the large sizes are sampled for coverage, not for bulk.
EXPONENT_WEIGHTS = {6: 0.55, 7: 0.38, 8: 0.065, 9: 0.005}
# A colony needs somewhere to live. Below this many tiles per colony the
# generators mostly fail or produce starts nobody could play.
MIN_TILES_PER_COLONY = 2500
# Below 128 tiles an axis several generators refuse outright, and a crowded map
# refuses more often still, so small maps are kept for small games.
MIN_AXIS_EXPONENT_FOR_CROWD = 7
CROWD_COLONIES = 4
DEFAULT_AIS = ('nicowar', 'castor', 'numbi', 'maxima')
# Controls whose value we never randomise: starting workers changes how long a
# game takes far more than it changes a starting position.
FIXED_CONTROLS = ('workers',)
SIZE_CONTROLS = ('width', 'height', 'teams')


def ticks_for(width_exp, height_exp, colonies):
    """Tick cap for a drawn map.

    90,000 ticks is the engine default and what the earlier fairness tournament
    used. Small maps mostly end outright well inside it and cost only the ticks
    they play, so they keep the full cap. Bigger maps almost never end at all,
    so they are stopped sooner and adjudicated: the cost is linear in ticks
    played, and a longer wait buys a finishing order we already have.
    """
    return 90000 if 2 ** (width_exp + height_exp) <= 16384 else 45000


def playable_generators(capabilities):
    return [g for g in capabilities['generators'] if not g.get('editorOnly')]


def draw_size(rng, colonies):
    """Draw width/height exponents with enough room for the colony count."""
    options = []
    for width, height in itertools.product(EXPONENTS, EXPONENTS):
        if abs(width - height) > 1:  # keep aspect ratios sane
            continue
        if 2 ** (width + height) < MIN_TILES_PER_COLONY * colonies:
            continue
        if colonies >= CROWD_COLONIES and min(width, height) < MIN_AXIS_EXPONENT_FOR_CROWD:
            continue
        options.append((width, height))
    if not options:
        return None
    weights = [EXPONENT_WEIGHTS[w] * EXPONENT_WEIGHTS[h] for w, h in options]
    return rng.choices(options, weights=weights, k=1)[0]


def draw_params(rng, generator, colonies, width, height, randomise):
    params = {'teams': colonies, 'width': width, 'height': height}
    for control in generator['controls']:
        name = control['id']
        if name in SIZE_CONTROLS or name in FIXED_CONTROLS:
            continue
        values = control.get('values')
        if not values:
            continue
        # Each control is re-rolled independently, so a drawn map sits somewhere
        # between the generator's defaults and a completely random setting.
        if randomise and rng.random() < 0.7:
            params[name] = rng.choice(values)
    return params


def draw_matches(capabilities, count, games_per_map, ais, seed, round_index):
    """Draw `count` maps and their games. Deterministic in (seed, round)."""
    generators = playable_generators(capabilities)
    matches = []
    for index in range(count):
        rng = random.Random(json.dumps([seed, round_index, index]))
        generator = rng.choice(generators)
        colonies = rng.choice(COLONY_CHOICES)
        size = draw_size(rng, colonies)
        if size is None:
            continue
        width, height = size
        randomise = rng.random() >= 0.25
        params = draw_params(rng, generator, colonies, width, height, randomise)
        matches.append({
            'index': index,
            'map_key': f'{round_index}:{index}',
            'generator': generator['method'],
            'generator_id': generator['id'],
            'colonies': colonies,
            'width': width, 'height': height,
            'params': params,
            'defaults': not randomise,
            'map_seed': rng.getrandbits(32),
            'ai': rng.choice(list(ais)),
            'ticks': ticks_for(width, height, colonies),
            'games': [{'game_seed': rng.getrandbits(32)} for _ in range(games_per_map)],
        })
    return matches


def plan_round(capabilities, build, matches, identifier, design):
    """One self-contained job per game.

    The engine can generate the map inside the game process, so a game carries
    its whole request -- generator, controls, map seed, AI roster -- and depends
    on nothing. That costs one extra map generation per game (tens of
    milliseconds against minutes of play) and buys a tournament with no
    artifacts to move between hosts and no job that can be blocked by another.
    """
    jobs = []
    for match in matches:
        generation = {'generator': match['generator'], 'params': match['params'],
                      'candidates': 0}
        for game in match['games']:
            jobs.append(job('game', build,
                            seeds={'game': game['game_seed'], 'map': match['map_seed']},
                            config={'players': [match['ai']] * match['colonies'],
                                    'ticks': match['ticks'], **generation},
                            outputs={},
                            limits={'timeout_seconds': 7200,
                                    'estimated_seconds': max(30, match['ticks'] // 1200)},
                            labels={'kind': 'fairness_game', 'map': match['map_key'],
                                    'generator': match['generator'],
                                    'generator_id': match['generator_id'],
                                    'colonies': match['colonies'], 'ai': match['ai'],
                                    'ticks': match['ticks'],
                                    'width': match['width'], 'height': match['height'],
                                    'defaults': match['defaults'], 'draw': match['index']}))
    jobs = list({value['id']: value for value in jobs}.values())
    manifest = {'schema_version': 1, 'id': identifier, 'kind': 'fairness_model',
                'jobs': jobs, 'settings': design.get('settings', {}),
                'labels': {'design': design}, 'design': design}
    return validate_experiment(manifest)


def round_directories(root):
    return sorted(p for p in Path(root).glob('round-*') if (p / 'experiment.json').exists())


def next_round_index(root):
    existing = [int(p.name.split('-')[1]) for p in round_directories(root)]
    return max(existing, default=0) + 1


def create_round(root, bundle_paths, games, games_per_map, ais, seed):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    bundles = [inspect_bundle(path) for path in bundle_paths]
    if len(bundles) != 1:
        raise ValueError('supply exactly one bundle: mixing builds would mix engines')
    bundle = bundles[0]
    index = next_round_index(root)
    maps = max(1, math.ceil(games / max(1, games_per_map)))
    matches = draw_matches(bundle['capabilities'], maps, games_per_map, ais, seed, index)
    design = {'schema_version': SCHEMA_VERSION, 'round': index, 'seed': seed,
              'maps': len(matches), 'games_per_map': games_per_map, 'ais': list(ais),
              'colony_choices': list(COLONY_CHOICES), 'exponents': list(EXPONENTS),
              'exponent_weights': {str(k): v for k, v in EXPONENT_WEIGHTS.items()},
              'min_tiles_per_colony': MIN_TILES_PER_COLONY,
              'min_axis_exponent_for_crowd': MIN_AXIS_EXPONENT_FOR_CROWD, 'candidates': 0,
              'inline_generation': True,
              'settings': {'transfer_slots': 8, 'prefetch': 3},
              'bundle': bundle['id'], 'revision': bundle.get('revision')}
    identifier = f"fairness-model-r{index:02d}-{seed}"
    manifest = plan_round(bundle['capabilities'], bundle['id'], matches, identifier, design)
    directory = root / f'round-{index:02d}'
    coordinator = Coordinator.submit(str(directory), manifest, bundle_paths)
    try:
        status = coordinator.status()
    finally:
        coordinator.close()
    atomic_json(root / f'design-{index:02d}.json', {'design': design, 'matches': matches})
    return {'round': index, 'directory': str(directory), 'maps': len(matches),
            'games': sum(len(m['games']) for m in matches), 'jobs': len(manifest['jobs']),
            'status': status}


def open_round(directory):
    """A round pinned to an older worker package is finished, not resumable here.

    Its committed results stay readable and keep counting towards the fit; only
    further play needs the preserved `worker.pyz` in its own directory.
    """
    try:
        return Coordinator(str(directory))
    except ValueError as error:
        if 'worker package' not in str(error):
            raise
        return None


def run_rounds(root, hosts, once=False, collect_only=False):
    report = []
    for directory in round_directories(root):
        coordinator = open_round(directory)
        if coordinator is None:
            report.append({'round': directory.name, 'skipped': 'pinned to another worker package'})
            continue
        try:
            report.append({'round': directory.name,
                           'result': coordinator.run(hosts, once or collect_only, collect_only)})
        finally:
            coordinator.close()
    return report


def status_rounds(root):
    report = []
    for directory in round_directories(root):
        coordinator = open_round(directory)
        if coordinator is None:
            report.append({'round': directory.name, 'skipped': 'pinned to another worker package'})
            continue
        try:
            report.append({'round': directory.name, 'status': coordinator.status()})
        finally:
            coordinator.close()
    return report


# ---------------------------------------------------------------------------
# Dataset
# ---------------------------------------------------------------------------
RESOURCES = ('wood', 'wheat', 'stone', 'algae', 'papyrus', 'cherry', 'orange', 'prune')
FRUITS = ('cherry', 'orange', 'prune')
BAND_STEPS = (12, 24, 48)
# A distance the walker never reached. Real catchment walking distances are
# bounded by the 48-step band, so this is far outside any measured value and is
# always paired with its own indicator feature.
UNREACHABLE = 512.0


def _distance(value):
    """Engine distances use -1 (and JSON null) for 'never reached'."""
    if value is None or value < 0:
        return UNREACHABLE, 1.0
    return float(value), 0.0


def colony_measurements(colony):
    """Every ColonyQuality number for one start, flattened and named.

    These are exactly the measurements `MapGeneration::scoreStarts` already
    computes in its single pass over the finished map, so anything selected
    here costs the generator nothing extra to evaluate.
    """
    raw = colony['raw']
    out = {}
    for key in ('catchment_tiles', 'reachable_tiles', 'catchment_grass_tiles',
                'catchment_buildable_tiles', 'catchment_fertile_grass_tiles',
                'catchment_growth_enabled_grass_tiles', 'exclusive_nearest_tiles',
                'tied_nearest_tiles', 'exclusive_catchment_tiles', 'tied_catchment_tiles',
                'build_sites_4x4', 'wheat_and_wood_amount', 'mean_fertility',
                'reachable_rivals', 'rivals_within_threat'):
        out[key] = float(raw.get(key) or 0)
    for key in ('wheat_distance', 'wood_distance'):
        out[key], out[key.replace('_distance', '_unreachable')] = _distance(raw.get(key))
    for key in ('nearest_rival_distance', 'farthest_rival_distance'):
        out[key], out[key + '_unreachable'] = _distance(raw.get(key))
    fruit_amount, fruit_tiles, fruit_nearest = 0.0, 0.0, UNREACHABLE
    for name in RESOURCES:
        entry = raw['resources'][name]
        distance, missing = _distance(entry.get('nearest_gather_distance'))
        out[f'{name}_distance'] = distance
        out[f'{name}_missing'] = missing
        out[f'{name}_catchment_tiles'] = float(entry.get('catchment_deposit_tiles') or 0)
        out[f'{name}_catchment_amount'] = float(entry.get('catchment_stored_amount') or 0)
        out[f'{name}_exclusive_amount'] = float(entry.get('exclusive_catchment_stored_amount') or 0)
        if name in FRUITS:
            fruit_amount += out[f'{name}_catchment_amount']
            fruit_tiles += out[f'{name}_catchment_tiles']
            fruit_nearest = min(fruit_nearest, distance)
    out['fruit_catchment_amount'] = fruit_amount
    out['fruit_catchment_tiles'] = fruit_tiles
    out['fruit_distance'] = fruit_nearest
    for band, steps in zip(colony.get('distance_bands', []), BAND_STEPS):
        prefix = f'band{steps}_'
        for key in ('reached_tiles', 'grass_tiles', 'buildable_tiles', 'fertile_grass_tiles',
                    'exclusive_nearest_tiles', 'tied_nearest_tiles'):
            out[prefix + key] = float(band.get(key) or 0)
        for name in ('wheat', 'wood', 'stone'):
            entry = band['resources'][name]
            out[f'{prefix}{name}_amount'] = float(entry.get('stored_amount') or 0)
            out[f'{prefix}{name}_tiles'] = float(entry.get('deposit_tiles') or 0)
            out[f'{prefix}{name}_exclusive_amount'] = float(entry.get('exclusive_stored_amount') or 0)
    # The incumbent scorer's own normalised factors, kept so the fit can be
    # compared against what the generators are ranked by today.
    for key, value in colony.get('normalized', {}).items():
        out['legacy_' + key] = float(value)
    out['legacy_total'] = float(colony.get('total') or 0)
    return out


def map_starts(generation):
    """Per-colony measurements of a generated map, in colony order.

    Colonies are joined to teams by index, not by coordinates: a map whose teams
    never had `startPosSet` reports a null start position, and those are exactly
    the maps a coordinate join would silently drop. The coordinates that are
    present are kept for a cross-check.
    """
    report = (generation or {}).get('map_report') or {}
    quality = report.get('canonical_quality') or {}
    if not quality.get('measured') or not quality.get('colonies'):
        return None
    starts = []
    for index, colony in enumerate(quality['colonies']):
        position = (report.get('map') or {}).get('colonies', [])
        position = position[index]['start'] if index < len(position) else {}
        starts.append({'measurements': colony_measurements(colony),
                       'position': (position.get('x'), position.get('y'))})
    return starts


def load_dataset(root, policy='prestige'):
    """Every played game joined to its map's start measurements.

    Games either carry their own generation result (the map was generated inside
    the game process) or name the separate generation job that produced their
    map file; both shapes appear across rounds and both are read here.
    """
    from tools.tournaments.analysis import adjudicate
    games, skipped = [], {'unmeasured': 0, 'failed_game': 0, 'start_mismatch': 0}
    for directory in round_directories(root):
        results = Results(str(directory))
        records = {record['job']['id']: record for record in results}
        separate = {}
        for record in records.values():
            if record['job']['type'] != 'generate_map':
                continue
            starts = map_starts(record.get('result') or {})
            if starts is None:
                skipped['unmeasured'] += 1
                continue
            separate[record['job']['id']] = starts
        for record in records.values():
            job_value = record['job']
            if job_value['type'] != 'game':
                continue
            if record.get('category') != 'success' or not (record.get('result') or {}).get('teams'):
                skipped['failed_game'] += 1
                continue
            result = record['result']
            labels = job_value['labels']
            starts = (map_starts(result.get('generation'))
                      if result.get('generation') else separate.get(labels.get('map_job')))
            if starts is None:
                skipped['unmeasured'] += 1
                continue
            key = directory.name + ':' + str(labels.get('map', labels.get('map_job', job_value['id'])))
            outcome = adjudicate(result, policy)
            # A rotated map file relabels the generator's colony t as team
            # (t + r) mod N; an inline map is written with no rotation at all.
            rotation = labels.get('rotation') or 0
            count = len(starts)
            entries, mismatch = [], False
            for team in result['teams']:
                index = (team['team'] - rotation) % count
                if index >= count or len(result['teams']) != count:
                    mismatch = True
                    break
                colony = starts[index]
                position = tuple(team['start'])
                if colony['position'][0] is not None and colony['position'] != position:
                    mismatch = True
                    break
                entries.append({'team': team['team'], 'start': position,
                                'placement': outcome['placements'][team['team']],
                                'won': team['team'] in outcome['winners'],
                                'measurements': dict(colony['measurements'],
                                                     team_index=float(team['team']))})
            if mismatch:
                skipped['start_mismatch'] += 1
                continue
            games.append({'map': key, 'round': directory.name, 'job': job_value['id'],
                          'generator': labels.get('generator_id'), 'ai': labels.get('ai'),
                          'colonies': labels.get('colonies'), 'width': labels.get('width'),
                          'height': labels.get('height'), 'defaults': labels.get('defaults'),
                          'ticks': result.get('ticks'),
                          'cap': result.get('termination') == 'tick_cap',
                          'engine_outcome': outcome['engine_outcome'], 'entries': entries})
    return {'games': games, 'skipped': skipped}


# ---------------------------------------------------------------------------
# Features and transforms
# ---------------------------------------------------------------------------
# Softmax is invariant to adding a per-game constant to every fitness, so only
# differences within one game carry information. `identity` therefore reads as
# an additive advantage, `log` as a ratio between colonies (and is the only
# transform that is automatically free of map scale), and `share` states a
# colony's cut of what the whole map has to give.
TRANSFORMS = {
    'identity': lambda x, total: x,
    'log': lambda x, total: math.log1p(max(x, 0.0)),
    'sqrt': lambda x, total: math.sqrt(max(x, 0.0)),
    'square': lambda x, total: x * x,
    'share': lambda x, total: (x / total) if total > 0 else 0.0,
    'decay24': lambda x, total: math.exp(-max(x, 0.0) / 24.0),
}
DISTANCE_PATTERN = re.compile(r'(_distance)$')
INDICATOR_PATTERN = re.compile(r'(_unreachable|_missing)$')


def transform_candidates(name):
    if name == 'team_index' or INDICATOR_PATTERN.search(name):
        return ('identity',)
    if DISTANCE_PATTERN.search(name):
        return ('identity', 'log', 'sqrt', 'decay24')
    if name.startswith('legacy_'):
        return ('identity',)
    return ('identity', 'log', 'sqrt', 'share')


def base_names(dataset):
    for game in dataset['games']:
        return sorted(game['entries'][0]['measurements'])
    return []


def feature_column(dataset, name, transform):
    """One transformed column, one row per colony per game, game-grouped."""
    function = TRANSFORMS[transform]
    columns = []
    for game in dataset['games']:
        values = [entry['measurements'].get(name, 0.0) for entry in game['entries']]
        total = sum(values)
        columns.append([function(value, total) for value in values])
    return columns


def orderings(dataset):
    """Finishing order per game as tied groups, best first."""
    result = []
    for game in dataset['games']:
        places = [entry['placement'] for entry in game['entries']]
        result.append([[index for index, place in enumerate(places) if place == value]
                       for value in sorted(set(places))])
    return result


def padded(dataset, features):
    """Pack the games into rectangular arrays: [game, slot, feature] plus masks.

    Games have between two and eight colonies, so every array is padded to the
    widest game and a mask marks the real slots. Everything downstream works a
    finishing stage at a time across all games at once, which keeps the fits
    fast enough to screen a hundred measurements.
    """
    import numpy as np
    games = dataset['games']
    widest = max(len(game['entries']) for game in games)
    columns = [feature_column(dataset, name, transform) for name, transform in features]
    matrix = np.zeros((len(games), widest, len(features)))
    mask = np.zeros((len(games), widest), dtype=bool)
    for position, game in enumerate(games):
        size = len(game['entries'])
        mask[position, :size] = True
        for index, column in enumerate(columns):
            matrix[position, :size, index] = column[position]
    return matrix, mask


def stages(dataset, widest, winner_only=False):
    """Who is removed at each Plackett-Luce stage, and who is still standing.

    Ties share a stage (the Breslow treatment), and the final group is dropped
    because with one group left there is nothing to predict.
    """
    import numpy as np
    games = dataset['games']
    order = orderings(dataset)
    depth = max(1, max(len(tied) - 1 for tied in order)) if not winner_only else 1
    removed = np.zeros((depth, len(games), widest), dtype=bool)
    alive = np.zeros((depth, len(games), widest), dtype=bool)
    active = np.zeros((depth, len(games)), dtype=bool)
    for position, tied in enumerate(order):
        standing = set(range(len(games[position]['entries'])))
        for step, members in enumerate(tied[:1] if winner_only else tied[:-1]):
            if step >= depth or len(standing) < 2:
                break
            alive[step, position, sorted(standing)] = True
            removed[step, position, members] = True
            active[step, position] = True
            standing -= set(members)
    return removed, alive, active


class Problem:
    """A fitting problem: padded design, stage masks and the ridge penalty."""

    def __init__(self, dataset, features, ridge=1e-3, winner_only=False, arrays=None):
        import numpy as np
        self.dataset, self.features, self.ridge = dataset, features, ridge
        if arrays is None:
            matrix, mask = padded(dataset, features)
            self.centre = matrix[mask].mean(axis=0)
            self.scale = matrix[mask].std(axis=0)
            self.scale[self.scale < 1e-12] = 1.0
            matrix = np.where(mask[:, :, None], (matrix - self.centre) / self.scale, 0.0)
            arrays = (matrix, mask, *stages(dataset, mask.shape[1], winner_only))
        self.matrix, self.mask, self.removed, self.alive, self.active = arrays

    def subset(self, index):
        import numpy as np
        value = Problem.__new__(Problem)
        value.dataset = {'games': [self.dataset['games'][i] for i in index]}
        value.features, value.ridge = self.features, self.ridge
        value.centre, value.scale = self.centre, self.scale
        value.matrix, value.mask = self.matrix[index], self.mask[index]
        value.removed, value.alive = self.removed[:, index], self.alive[:, index]
        value.active = self.active[:, index]
        return value

    def fitness(self, weights):
        return self.matrix @ weights

    def log_likelihood(self, weights):
        import numpy as np
        fitness = self.fitness(weights)
        total, gradient = 0.0, np.zeros_like(weights)
        for removed, alive, active in zip(self.removed, self.alive, self.active):
            if not active.any():
                continue
            masked = np.where(alive, fitness, -np.inf)
            top = masked.max(axis=1)
            shifted = np.where(alive, np.exp(fitness - top[:, None]), 0.0)
            # Games with no stage left contribute nothing; keep their arithmetic
            # finite so one padded row cannot poison the whole sum.
            denominator = np.where(active, shifted.sum(axis=1), 1.0)
            top = np.where(active, top, 0.0)
            size = removed.sum(axis=1)
            term = (fitness * removed).sum(axis=1) - size * (top + np.log(denominator))
            total += float(term[active].sum())
            probability = shifted / denominator[:, None]
            taken = np.einsum('gs,gsk->gk', removed.astype(float), self.matrix)
            expected = np.einsum('gs,gsk->gk', probability, self.matrix) * size[:, None]
            gradient += (taken - expected)[active].sum(axis=0)
        penalty = self.ridge * float(weights @ weights)
        return total - penalty, gradient - 2.0 * self.ridge * weights

    def fit(self):
        import numpy as np
        from scipy.optimize import minimize

        def objective(weights):
            value, gradient = self.log_likelihood(weights)
            return -value, -gradient

        result = minimize(objective, np.zeros(self.matrix.shape[2]), jac=True,
                          method='L-BFGS-B', options={'maxiter': 500, 'ftol': 1e-12})
        return result.x, -result.fun

    def evaluate(self, weights):
        """Winner-only log-loss, McFadden R2 and top-1 accuracy against uniform."""
        import numpy as np
        fitness = np.where(self.mask, self.fitness(weights), -np.inf)
        top = fitness.max(axis=1)
        shifted = np.where(self.mask, np.exp(fitness - top[:, None]), 0.0)
        probability = shifted / shifted.sum(axis=1)[:, None]
        winners = self.removed[0]
        size = self.mask.sum(axis=1)
        won = (probability * winners).sum(axis=1) / np.maximum(winners.sum(axis=1), 1)
        keep = winners.any(axis=1)
        if not keep.any():
            return {'games': 0}
        total = float(np.log(np.maximum(won[keep], 1e-12)).sum())
        baseline = float(np.log(1.0 / size[keep]).sum())
        best = np.argmax(np.where(self.mask, fitness, -np.inf), axis=1)
        hits = float(winners[np.arange(len(best)), best][keep].sum())
        chance = float((winners.sum(axis=1)[keep] / size[keep]).sum())
        count = int(keep.sum())
        return {'games': count, 'log_loss': -total / count,
                'baseline_log_loss': -baseline / count,
                'mcfadden_r2': 1.0 - total / baseline if baseline else 0.0,
                'accuracy': hits / count, 'chance_accuracy': chance / count}


def cross_validated(dataset, features, folds=5, ridge=1e-3, seed=1, winner_only=False,
                    problem=None):
    """Grouped by map: every game of a map lands in the same fold."""
    import numpy as np
    problem = problem or Problem(dataset, features, ridge, winner_only)
    keys = sorted({game['map'] for game in dataset['games']})
    rng = random.Random(seed)
    rng.shuffle(keys)
    assignment = {key: index % folds for index, key in enumerate(keys)}
    membership = np.array([assignment[game['map']] for game in dataset['games']])
    scores = []
    for fold in range(folds):
        train = np.flatnonzero(membership != fold)
        test = np.flatnonzero(membership == fold)
        if not len(train) or not len(test):
            continue
        weights, _ = problem.subset(train).fit()
        scores.append(problem.subset(test).evaluate(weights))
    scores = [score for score in scores if score.get('games')]
    if not scores:
        return {'games': 0}
    return {'folds': len(scores),
            'log_loss': sum(s['log_loss'] for s in scores) / len(scores),
            'baseline_log_loss': sum(s['baseline_log_loss'] for s in scores) / len(scores),
            'mcfadden_r2': sum(s['mcfadden_r2'] for s in scores) / len(scores),
            'accuracy': sum(s['accuracy'] for s in scores) / len(scores),
            'chance_accuracy': sum(s['chance_accuracy'] for s in scores) / len(scores)}


def fit_model(dataset, features, ridge=1e-3, winner_only=False, problem=None):
    """Fit and return coefficients on the raw (unstandardised) features."""
    problem = problem or Problem(dataset, features, ridge, winner_only)
    weights, value = problem.fit()
    raw = weights / problem.scale
    # Softmax fixes the fitness scale but not its offset. Anchor the offset so
    # the mean fitness over the fitted starts is zero; every reported absolute
    # level is relative to that convention, never something the games measured.
    intercept = -float(problem.centre @ raw)
    return {'features': [{'name': name, 'transform': transform, 'coefficient': float(coefficient)}
                         for (name, transform), coefficient in zip(features, raw)],
            'intercept': intercept, 'log_likelihood': float(value),
            'train': problem.evaluate(weights)}


# ---------------------------------------------------------------------------
# Fairness from fitness
# ---------------------------------------------------------------------------
def softmax(values):
    top = max(values)
    weights = [math.exp(value - top) for value in values]
    total = sum(weights)
    return [weight / total for weight in weights]


def gini(values):
    """Normalised Gini of a probability vector: 0 when flat, 1 when one takes all.

    The raw Gini of n numbers cannot exceed (n-1)/n, so a two-colony map could
    never look worse than 0.5. Dividing by that ceiling makes the number mean
    the same thing whatever the colony count.
    """
    count = len(values)
    if count < 2:
        return 0.0
    total = sum(values)
    if total <= 0:
        return 0.0
    ordered = sorted(values)
    weighted = sum((2 * (index + 1) - count - 1) * value for index, value in enumerate(ordered))
    raw = weighted / (count * total)
    return max(0.0, min(1.0, raw * count / (count - 1)))


def fairness(fitnesses):
    return 1.0 - gini(softmax(fitnesses))


def predicted(model, dataset):
    """Per-game predicted win probabilities under a fitted model."""
    features = [(item['name'], item['transform']) for item in model['features']]
    coefficients = [item['coefficient'] for item in model['features']]
    columns = [feature_column(dataset, name, transform) for name, transform in features]
    output = []
    for position, game in enumerate(dataset['games']):
        fitnesses = []
        for member in range(len(game['entries'])):
            value = model['intercept']
            for column, coefficient in zip(columns, coefficients):
                value += coefficient * column[position][member]
            fitnesses.append(value)
        output.append({'fitness': fitnesses, 'probability': softmax(fitnesses),
                       'fairness': fairness(fitnesses)})
    return output


# ---------------------------------------------------------------------------
# Screening (explore mode)
# ---------------------------------------------------------------------------
def screen(dataset, folds=5, ridge=1e-3, minimum_games=200):
    # Several measurements repeat by construction -- the 24-step band is the
    # catchment -- so identical columns are screened once under the first name
    # and never offered to selection twice.
    rows, seen = [], {}
    for name in base_names(dataset):
        for transform in transform_candidates(name):
            column = feature_column(dataset, name, transform)
            flat = [value for game in column for value in game]
            spread = max(flat) - min(flat) if flat else 0.0
            within = sum(1 for game in column if max(game) - min(game) > 1e-12)
            if spread <= 1e-12 or within < minimum_games:
                continue
            signature = hash(tuple(round(value, 9) for value in flat))
            if signature in seen:
                continue
            seen[signature] = name
            problem = Problem(dataset, [(name, transform)], ridge)
            model = fit_model(dataset, [(name, transform)], ridge, problem=problem)
            score = cross_validated(dataset, [(name, transform)], folds, ridge, problem=problem)
            rows.append({'name': name, 'transform': transform,
                         'coefficient': model['features'][0]['coefficient'],
                         'varying_games': within,
                         'train_r2': model['train']['mcfadden_r2'],
                         'cv_r2': score.get('mcfadden_r2', 0.0),
                         'cv_accuracy': score.get('accuracy', 0.0),
                         'cv_chance': score.get('chance_accuracy', 0.0)})
    rows.sort(key=lambda row: -row['cv_r2'])
    return rows


def forward_select(dataset, candidates, limit=8, folds=5, ridge=1e-3, tolerance=0.0005):
    """Greedy selection on cross-validated McFadden R2, one measurement each."""
    chosen, used, history = [], set(), []
    best = cross_validated(dataset, [('legacy_total', 'identity')], folds, ridge)
    current = 0.0
    while len(chosen) < limit:
        trials = []
        for row in candidates:
            if row['name'] in used:
                continue
            option = chosen + [(row['name'], row['transform'])]
            score = cross_validated(dataset, option, folds, ridge)
            trials.append((score.get('mcfadden_r2', 0.0), row, score))
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
        current = value
        history.append({'added': row['name'], 'transform': row['transform'],
                        'cv_r2': value, 'cv_accuracy': score.get('accuracy')})
    return chosen, history, {'legacy_total_cv_r2': best.get('mcfadden_r2', 0.0)}


# ---------------------------------------------------------------------------
# Robustness
# ---------------------------------------------------------------------------
def bootstrap_coefficients(dataset, features, draws=200, ridge=1e-3, seed=1):
    """Cluster bootstrap over maps.

    Two games share a map, so they share its starts and are not independent.
    Resampling whole maps keeps that dependence inside the interval instead of
    pretending every game is a fresh observation.
    """
    import numpy as np
    keys = sorted({game['map'] for game in dataset['games']})
    index = {key: [] for key in keys}
    for position, game in enumerate(dataset['games']):
        index[game['map']].append(position)
    problem = Problem(dataset, features, ridge)
    rng = random.Random(seed)
    samples = []
    for _ in range(draws):
        picked = [position for key in (rng.choice(keys) for _ in keys) for position in index[key]]
        try:
            weights, _ = problem.subset(np.asarray(picked)).fit()
        except (ValueError, FloatingPointError):
            continue
        samples.append(weights / problem.scale)
    if not samples:
        return []
    matrix = np.asarray(samples)
    return [{'name': name, 'transform': transform,
             'low': float(np.percentile(matrix[:, k], 2.5)),
             'high': float(np.percentile(matrix[:, k], 97.5)),
             'sign_stability': float(max((matrix[:, k] > 0).mean(), (matrix[:, k] < 0).mean()))}
            for k, (name, transform) in enumerate(features)]


SUBGROUPS = {
    'decided outright': lambda game: game['engine_outcome'],
    'stopped at the cap': lambda game: not game['engine_outcome'],
    'two or three colonies': lambda game: game['colonies'] <= 3,
    'four or five colonies': lambda game: 4 <= game['colonies'] <= 5,
    'six or more colonies': lambda game: game['colonies'] >= 6,
    'up to 128x128': lambda game: 2 ** (game['width'] + game['height']) <= 16384,
    'larger than 128x128': lambda game: 2 ** (game['width'] + game['height']) > 16384,
    'default controls': lambda game: game['defaults'],
    'randomised controls': lambda game: not game['defaults'],
}


def subgroup_fits(dataset, features, folds=5, ridge=1e-3, minimum=150):
    """Refit on slices of the games; a model worth keeping does not flip sign."""
    report = []
    groups = dict(SUBGROUPS)
    for ai in sorted({game['ai'] for game in dataset['games']}):
        groups['AI: ' + ai] = (lambda value: (lambda game: game['ai'] == value))(ai)
    for label, predicate in groups.items():
        subset = {'games': [game for game in dataset['games'] if predicate(game)],
                  'skipped': dataset['skipped']}
        if len(subset['games']) < minimum:
            report.append({'group': label, 'games': len(subset['games']), 'skipped': True})
            continue
        model = fit_model(subset, features, ridge)
        report.append({'group': label, 'games': len(subset['games']),
                       'coefficients': {item['name']: item['coefficient'] for item in model['features']},
                       'train_r2': model['train']['mcfadden_r2'],
                       'cv_r2': cross_validated(subset, features, folds, ridge).get('mcfadden_r2')})
    return report


def calibration(model, dataset, buckets=10):
    """Predicted against observed win rate, pooled over every colony of every game."""
    rows = predicted(model, dataset)
    pairs = []
    for row, game in zip(rows, dataset['games']):
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


def fairness_distribution(model, dataset):
    """What the new score says about the maps the tournament actually drew."""
    from collections import defaultdict
    rows = predicted(model, dataset)
    seen, by_generator = {}, defaultdict(list)
    for row, game in zip(rows, dataset['games']):
        if game['map'] in seen:
            continue
        seen[game['map']] = row['fairness']
        by_generator[game['generator']].append(row['fairness'])
    values = sorted(seen.values())
    def at(fraction):
        return values[min(len(values) - 1, int(fraction * len(values)))] if values else None
    return {'maps': len(values), 'min': values[0] if values else None,
            'p10': at(0.10), 'median': at(0.50), 'p90': at(0.90),
            'max': values[-1] if values else None,
            'by_generator': {name: {'maps': len(items), 'mean': sum(items) / len(items)}
                             for name, items in sorted(by_generator.items())}}


# ---------------------------------------------------------------------------
# Final model
# ---------------------------------------------------------------------------
# Screened on the played tournament and checked by hand; `fit --mode explore`
# is what produced this list, and `docs/map-generators/FAIRNESS_MODEL.md`
# records why each measurement is in it.
FINAL_FEATURES = []
HEADER_PATH = 'src/map/generator/shared/FairnessModelCoefficients.h'
IDENTIFIER_PATTERN = re.compile(r'[^A-Za-z0-9]+')


def constant_name(name, transform):
    label = IDENTIFIER_PATTERN.sub('_', f'{name}_{transform}').upper().strip('_')
    return 'FAIRNESS_MODEL_' + label


def emit_header(model, dataset, path, provenance):
    lines = [
        '// SPDX-License-Identifier: GPL-3.0-or-later',
        '// Generated by tools/fairness_model.py -- do not edit by hand.',
        '//',
        '// Conditional-logit coefficients for the start fitness F. The win',
        '// probability of a start is softmax(F) over the colonies of one map, and',
        '// map fairness is 1 - normalised Gini of those probabilities.',
        '//',
        f"// games:      {provenance['games']}",
        f"// maps:       {provenance['maps']}",
        f"// rounds:     {', '.join(provenance['rounds'])}",
        f"// AIs:        {', '.join(provenance['ais'])}",
        f"// fitted:     {provenance['revision']}",
        f"// McFadden R2 {provenance['cv_r2']:.4f} cross-validated, "
        f"{provenance['train_r2']:.4f} in sample",
        '#pragma once',
        '',
        'namespace MapGeneration',
        '{',
        '/// Fitness offset. Softmax fixes the scale of F but not its zero, so this',
        '/// anchors the mean fitness of the fitted starts at zero.',
        f'constexpr double FAIRNESS_MODEL_INTERCEPT = {model["intercept"]!r};',
        '',
    ]
    for item in model['features']:
        lines.append(f'/// {item["name"]} ({item["transform"]})')
        lines.append(f'constexpr double {constant_name(item["name"], item["transform"])}'
                     f' = {item["coefficient"]!r};')
    lines += ['', f'constexpr int FAIRNESS_MODEL_FEATURE_COUNT = {len(model["features"])};',
              f'constexpr int FAIRNESS_MODEL_GAMES = {provenance["games"]};',
              '} // namespace MapGeneration', '']
    Path(path).write_text('\n'.join(lines))
    return path


# ---------------------------------------------------------------------------
# Reports
# ---------------------------------------------------------------------------
def describe(dataset):
    from collections import Counter
    games = dataset['games']
    return {'games': len(games), 'maps': len({game['map'] for game in games}),
            'skipped': dataset['skipped'],
            'by_colonies': dict(sorted(Counter(game['colonies'] for game in games).items())),
            'by_ai': dict(sorted(Counter(game['ai'] for game in games).items())),
            'by_size': dict(sorted(Counter(f"{2**game['width']}x{2**game['height']}"
                                           for game in games).items())),
            'by_generator': dict(sorted(Counter(game['generator'] for game in games).items())),
            'decided_by_engine': sum(1 for game in games if game['engine_outcome']),
            'reached_cap': sum(1 for game in games if game['cap']),
            'default_controls': sum(1 for game in games if game['defaults'])}


def markdown_report(summary, screened, history, model, scores, legacy,
                    intervals=(), groups=(), curve=(), spread=None):
    lines = ['# Fairness model fit', '',
             f"{summary['games']} games on {summary['maps']} maps; "
             f"{summary['decided_by_engine']} decided outright, "
             f"{summary['reached_cap']} stopped at the tick cap.", '',
             '## Coverage', '',
             '| Dimension | Counts |', '| --- | --- |',
             f"| Colonies | {summary['by_colonies']} |",
             f"| AI | {summary['by_ai']} |",
             f"| Size | {summary['by_size']} |", '',
             '## What predicts winning', '',
             '| Measurement | Transform | Coefficient | CV McFadden R2 | CV top-1 | Chance |',
             '| --- | --- | ---: | ---: | ---: | ---: |']
    for row in screened[:30]:
        lines.append(f"| {row['name']} | {row['transform']} | {row['coefficient']:+.5g} | "
                     f"{row['cv_r2']:+.4f} | {row['cv_accuracy']:.3f} | {row['cv_chance']:.3f} |")
    lines += ['', '## Selection', '']
    for step in history:
        if step.get('stopped'):
            lines.append(f"- stopped: best remaining {step['best_candidate']} gained "
                         f"{step['gain']:+.5f}, below {step['tolerance']}")
        else:
            lines.append(f"- added **{step['added']}** ({step['transform']}), "
                         f"CV R2 {step['cv_r2']:.4f}, top-1 {step['cv_accuracy']:.3f}")
    if model:
        lines += ['', '## Fitted model', '',
                  '| Measurement | Transform | Coefficient |', '| --- | --- | ---: |']
        for item in model['features']:
            lines.append(f"| {item['name']} | {item['transform']} | {item['coefficient']:+.6g} |")
        lines += ['', f"Intercept {model['intercept']:+.6g}.", '',
                  f"In sample: McFadden R2 {model['train']['mcfadden_r2']:.4f}, "
                  f"top-1 {model['train']['accuracy']:.3f} against "
                  f"{model['train']['chance_accuracy']:.3f} chance.", '',
                  f"Cross-validated: McFadden R2 {scores.get('mcfadden_r2', 0):.4f}, "
                  f"top-1 {scores.get('accuracy', 0):.3f}.", '',
                  f"The incumbent start score alone reaches CV McFadden R2 "
                  f"{legacy.get('legacy_total_cv_r2', 0):.4f}."]
    if intervals:
        lines += ['', '## Coefficient intervals (cluster bootstrap over maps)', '',
                  '| Measurement | 2.5% | 97.5% | Sign stability |',
                  '| --- | ---: | ---: | ---: |']
        for item in intervals:
            lines.append(f"| {item['name']} | {item['low']:+.5g} | {item['high']:+.5g} | "
                         f"{item['sign_stability']:.2f} |")
    if groups:
        names = [item['name'] for item in model['features']] if model else []
        lines += ['', '## The same model refit on slices of the games', '',
                  '| Slice | Games | CV R2 | ' + ' | '.join(names) + ' |',
                  '| --- | ---: | ---: |' + ' ---: |' * len(names)]
        for item in groups:
            if item.get('skipped'):
                lines.append(f"| {item['group']} | {item['games']} | too few | "
                             + ' | '.join('' for _ in names) + ' |')
                continue
            cells = ' | '.join(f"{item['coefficients'][name]:+.4g}" for name in names)
            lines.append(f"| {item['group']} | {item['games']} | "
                         f"{item['cv_r2']:+.4f} | {cells} |")
    if curve:
        lines += ['', '## Calibration', '', '| Predicted | Observed | Colonies |',
                  '| ---: | ---: | ---: |']
        for bucket in curve:
            lines.append(f"| {bucket['predicted']:.3f} | {bucket['observed']:.3f} | "
                         f"{bucket['count']} |")
    if spread:
        lines += ['', '## Fairness of the maps the tournament drew', '',
                  f"{spread['maps']} maps: min {spread['min']:.3f}, p10 {spread['p10']:.3f}, "
                  f"median {spread['median']:.3f}, p90 {spread['p90']:.3f}, "
                  f"max {spread['max']:.3f}.", '',
                  '| Generator | Maps | Mean fairness |', '| --- | ---: | ---: |']
        for name, item in sorted(spread['by_generator'].items(),
                                 key=lambda pair: pair[1]['mean']):
            lines.append(f"| {name} | {item['maps']} | {item['mean']:.3f} |")
    return '\n'.join(lines) + '\n'


def run_fit(root, mode, output, folds, ridge, limit, policy, revision, draws=200):
    dataset = load_dataset(root, policy)
    if not dataset['games']:
        raise ValueError('no played games found; run the tournament first')
    summary = describe(dataset)
    output = Path(output or Path(root) / 'analysis')
    output.mkdir(parents=True, exist_ok=True)
    screened, history, legacy, model, scores = [], [], {}, None, {}
    if mode == 'explore':
        screened = screen(dataset, folds, ridge)
        strong = [row for row in screened if row['cv_r2'] > 0]
        selected, history, legacy = forward_select(dataset, strong, limit, folds, ridge)
        if selected:
            model = fit_model(dataset, selected, ridge)
            scores = cross_validated(dataset, selected, folds, ridge)
    else:
        if not FINAL_FEATURES:
            raise ValueError('FINAL_FEATURES is empty: screen with --mode explore first')
        selected = [tuple(item) for item in FINAL_FEATURES]
        model = fit_model(dataset, selected, ridge)
        scores = cross_validated(dataset, selected, folds, ridge)
        legacy = {'legacy_total_cv_r2':
                  cross_validated(dataset, [('legacy_total', 'identity')], folds, ridge)
                  .get('mcfadden_r2', 0.0)}
    intervals, groups, curve, spread = [], [], [], {}
    if model:
        intervals = bootstrap_coefficients(dataset, selected, draws, ridge)
        groups = subgroup_fits(dataset, selected, folds, ridge)
        curve = calibration(model, dataset)
        spread = fairness_distribution(model, dataset)
    report = {'schema_version': SCHEMA_VERSION, 'mode': mode, 'policy': policy,
              'summary': summary, 'screened': screened, 'selection': history,
              'legacy': legacy, 'model': model, 'cross_validated': scores,
              'intervals': intervals, 'subgroups': groups, 'calibration': curve,
              'fairness_distribution': spread}
    atomic_json(output / f'{mode}.json', report)
    (output / f'{mode}.md').write_text(markdown_report(summary, screened, history, model,
                                                       scores, legacy, intervals, groups,
                                                       curve, spread))
    if mode == 'final':
        rounds = sorted({game['round'] for game in dataset['games']})
        ais = sorted({game['ai'] for game in dataset['games']})
        emit_header(model, dataset, HEADER_PATH,
                    {'games': summary['games'], 'maps': summary['maps'], 'rounds': rounds,
                     'ais': ais, 'revision': revision,
                     'cv_r2': scores.get('mcfadden_r2', 0.0),
                     'train_r2': model['train']['mcfadden_r2']})
        report['header'] = HEADER_PATH
    return report


# ---------------------------------------------------------------------------
# Sampling study: what does another candidate roll buy?
# ---------------------------------------------------------------------------
# The lobby keeps the best-scoring of `kSampledCandidates` rolls. Every roll
# costs a full generation, and the whole search has to stay inside the map's
# time budget, so the question is how much fairness the k-th roll still adds.
SAMPLING_SHAPES = ((7, 7, 4), (8, 8, 4), (7, 7, 2), (8, 8, 8))
SAMPLING_BUDGET_SECONDS = 0.5
SAMPLING_TARGET = 0.80


def sampling_run(binary, generator, width, height, colonies, seed, candidates, directory):
    import subprocess
    output = Path(directory) / f'{generator}-{width}-{height}-{colonies}-{seed}'
    arguments = [binary, '--generate-map', '--generator', str(generator),
                 '--map-seed', str(seed), '--param', f'teams={colonies}',
                 '--param', f'width={width}', '--param', f'height={height}',
                 '--candidates', str(candidates), '--write-map', 'false',
                 '--output-dir', str(output), '--profile', 'fairness-sampling']
    subprocess.run(arguments, capture_output=True)
    try:
        return read_json(output / 'result.json').get('candidate_rolls') or []
    except (OSError, ValueError):
        return []


def sampling_curves(rolls, candidates):
    """Best-of-k score and cumulative cost for every k, from one root seed."""
    best, seconds, running, cost = [], [], -1.0, 0.0
    for index in range(candidates):
        roll = rolls[index] if index < len(rolls) else None
        if roll:
            cost += roll['seconds']
            if roll['generated'] and roll['score'] > running:
                running = roll['score']
        best.append(running)
        seconds.append(cost)
    return best, seconds


def sampling_study(binary, directory, generators, seeds, candidates, jobs, shapes):
    import concurrent.futures
    from collections import defaultdict
    work, rows = [], defaultdict(list)
    for generator in generators:
        for width, height, colonies in shapes:
            for seed in range(1, seeds + 1):
                work.append((generator, width, height, colonies, seed))
    Path(directory).mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(sampling_run, binary, item[0], item[1], item[2], item[3],
                               item[4], candidates, directory): item for item in work}
        for future in concurrent.futures.as_completed(futures):
            generator, width, height, colonies, seed = futures[future]
            rolls = future.result()
            if not rolls or not any(roll['generated'] for roll in rolls):
                continue
            best, seconds = sampling_curves(rolls, candidates)
            rows[(generator, width, height, colonies)].append({'seed': seed, 'best': best,
                                                               'seconds': seconds})
    return rows


def sampling_summary(rows, candidates, budget=SAMPLING_BUDGET_SECONDS, target=SAMPLING_TARGET):
    report = []
    for (generator, width, height, colonies), samples in sorted(rows.items()):
        if not samples:
            continue
        count = len(samples)
        mean_best = [sum(sample['best'][k] for sample in samples) / count
                     for k in range(candidates)]
        mean_cost = [sum(sample['seconds'][k] for sample in samples) / count
                     for k in range(candidates)]
        floor, ceiling = mean_best[0], mean_best[-1]
        span = ceiling - floor
        reached, affordable = None, None
        for k in range(candidates):
            if reached is None and (span <= 1e-9 or (mean_best[k] - floor) / span >= target):
                reached = k + 1
            if mean_cost[k] <= budget:
                affordable = k + 1
        report.append({'generator': generator, 'width': width, 'height': height,
                       'colonies': colonies, 'seeds': count,
                       'fairness_at_1': floor, 'fairness_at_max': ceiling,
                       'gain': span, 'mean_best': mean_best, 'mean_seconds': mean_cost,
                       'candidates_for_target': reached, 'candidates_within_budget': affordable,
                       'recommended': min(reached or candidates, affordable or 1)
                       if reached else (affordable or 1)})
    return report


def sampling_plot(report, path, candidates):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        return None
    shapes = sorted({(row['width'], row['height'], row['colonies']) for row in report})
    figure, axes = plt.subplots(len(shapes), 2, figsize=(13, 4 * len(shapes)), squeeze=False)
    steps = range(1, candidates + 1)
    for index, shape in enumerate(shapes):
        rows = [row for row in report if (row['width'], row['height'], row['colonies']) == shape]
        left, right = axes[index][0], axes[index][1]
        for row in rows:
            left.plot(steps, row['mean_best'], linewidth=0.9, alpha=0.65)
            right.plot(steps, [value * 1000 for value in row['mean_seconds']],
                       linewidth=0.9, alpha=0.65)
        pooled = [sum(row['mean_best'][k] for row in rows) / len(rows) for k in range(candidates)]
        left.plot(steps, pooled, color='black', linewidth=2.4, label='all generators')
        left.set_title(f'{2 ** shape[0]}x{2 ** shape[1]}, {shape[2]} colonies: fairness of the kept roll')
        left.set_xlabel('candidate rolls'); left.set_ylabel('fairness'); left.legend()
        right.axhline(SAMPLING_BUDGET_SECONDS * 1000, color='red', linestyle='--',
                      label=f'{int(SAMPLING_BUDGET_SECONDS * 1000)} ms budget')
        right.set_yscale('log')
        right.set_title('cumulative generation time')
        right.set_xlabel('candidate rolls'); right.set_ylabel('milliseconds'); right.legend()
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    plt.close(figure)
    return path


def run_sampling(binary, directory, catalog, seeds, candidates, jobs, output):
    generators = [g['method'] for g in playable_generators(catalog)]
    names = {g['method']: g['id'] for g in playable_generators(catalog)}
    rows = sampling_study(binary, Path(directory) / 'maps', generators, seeds, candidates,
                          jobs, SAMPLING_SHAPES)
    report = sampling_summary(rows, candidates)
    for row in report:
        row['generator_id'] = names.get(row['generator'], str(row['generator']))
    output = Path(output or Path(directory) / 'analysis')
    output.mkdir(parents=True, exist_ok=True)
    atomic_json(output / 'sampling.json', {'schema_version': SCHEMA_VERSION,
                                           'candidates': candidates, 'seeds': seeds,
                                           'budget_seconds': SAMPLING_BUDGET_SECONDS,
                                           'target': SAMPLING_TARGET, 'rows': report})
    sampling_plot(report, str(output / 'sampling.png'), candidates)
    lines = ['# What another candidate roll buys', '',
             f'{seeds} root seeds per generator and shape, {candidates} rolls each; the kept roll '
             f'is the best-scoring of the first k. Budget {int(SAMPLING_BUDGET_SECONDS * 1000)} ms '
             f'for the whole search, target {int(SAMPLING_TARGET * 100)}% of the available gain.',
             '', '| Generator | Shape | Fairness at 1 | at max | Gain | k for target | k in budget | Recommended |',
             '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for row in sorted(report, key=lambda item: (-item['gain'], item['generator_id'])):
        shape = f"{2 ** row['width']}x{2 ** row['height']}/{row['colonies']}"
        lines.append(f"| {row['generator_id']} | {shape} | {row['fairness_at_1']:.3f} | "
                     f"{row['fairness_at_max']:.3f} | {row['gain']:+.3f} | "
                     f"{row['candidates_for_target']} | {row['candidates_within_budget']} | "
                     f"{row['recommended']} |")
    (output / 'sampling.md').write_text('\n'.join(lines) + '\n')
    return {'shapes': len(SAMPLING_SHAPES), 'rows': len(report), 'output': str(output)}


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('run', 'extend'):
        p = sub.add_parser(name, help='draw and play a round of games')
        p.add_argument('directory')
        p.add_argument('--hosts', required=True)
        p.add_argument('--bundle', action='append', required=True)
        p.add_argument('--games', type=int, default=4000)
        p.add_argument('--games-per-map', type=int, default=2)
        p.add_argument('--ai', action='append')
        p.add_argument('--seed', type=int, default=20260915)
        p.add_argument('--once', action='store_true')
        p.add_argument('--no-play', action='store_true', help='submit only, play later')
    for name in ('status', 'collect'):
        p = sub.add_parser(name)
        p.add_argument('directory')
        if name == 'collect':
            p.add_argument('--hosts', required=True)
    p = sub.add_parser('play', help='keep playing an already drawn tournament')
    p.add_argument('directory'); p.add_argument('--hosts', required=True)
    p.add_argument('--once', action='store_true')
    p = sub.add_parser('sampling', help='measure what extra candidate rolls buy')
    p.add_argument('directory')
    p.add_argument('--binary', default='build/src/glob2')
    p.add_argument('--catalog', required=True)
    p.add_argument('--seeds', type=int, default=24)
    p.add_argument('--candidates', type=int, default=32)
    p.add_argument('--jobs', type=int, default=1)
    p.add_argument('--output')
    p = sub.add_parser('fit')
    p.add_argument('directory')
    p.add_argument('--mode', choices=('explore', 'final'), default='final')
    p.add_argument('--output')
    p.add_argument('--folds', type=int, default=5)
    p.add_argument('--ridge', type=float, default=1e-3)
    p.add_argument('--limit', type=int, default=8)
    p.add_argument('--policy', default='prestige')
    p.add_argument('--revision', default=os.environ.get('GLOB2_REVISION', 'unknown'))
    p.add_argument('--draws', type=int, default=200, help='cluster bootstrap draws')
    args = parser.parse_args()
    try:
        if args.command in ('run', 'extend'):
            created = create_round(args.directory, args.bundle, args.games,
                                   args.games_per_map, tuple(args.ai or DEFAULT_AIS), args.seed)
            value = {'created': created}
            if not args.no_play:
                value['played'] = run_rounds(args.directory, read_json(args.hosts), args.once)
        elif args.command == 'play':
            value = run_rounds(args.directory, read_json(args.hosts), args.once)
        elif args.command == 'collect':
            value = run_rounds(args.directory, read_json(args.hosts), True, True)
        elif args.command == 'status':
            value = status_rounds(args.directory)
        elif args.command == 'sampling':
            value = run_sampling(args.binary, args.directory, read_json(args.catalog),
                                 args.seeds, args.candidates, args.jobs, args.output)
        else:
            value = run_fit(args.directory, args.mode, args.output, args.folds,
                            args.ridge, args.limit, args.policy, args.revision, args.draws)
            value.pop('screened', None)
        print(json.dumps(value, indent=2, allow_nan=False, default=str))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f'error: {error}\n')


if __name__ == '__main__':
    main()
