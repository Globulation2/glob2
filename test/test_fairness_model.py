#!/usr/bin/env python3
"""Check the fairness model's fit, its fairness definition and its C++ emission.

Needs no build and plays no games: the tournament is simulated from the model
itself, so a fit that cannot recover coefficients it generated is a broken fit.
"""
import math
from pathlib import Path
import random
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import fairness_model as F

# Fitting needs numpy and scipy; the fairness definition, the draw and the C++
# emission do not, and those are the parts CI can check without them.
try:
    import numpy  # noqa: F401
    import scipy.optimize  # noqa: F401
    FITTING = True
except ImportError:
    FITTING = False


def synthetic(games, coefficients, seed=1, colonies=(2, 3, 4, 6, 8)):
    """Games whose finishing orders really are drawn from the model.

    Each colony gets a wheat stock and a rival distance, its fitness follows the
    supplied coefficients, and the finishing order is sampled by repeatedly
    drawing the next finisher in proportion to exp(fitness) -- which is exactly
    what the Plackett-Luce likelihood assumes.
    """
    rng = random.Random(seed)
    rows = []
    for index in range(games):
        count = rng.choice(colonies)
        entries, fitness = [], []
        for _ in range(count):
            wheat = rng.uniform(0, 400)
            rival = rng.uniform(10, 120)
            value = (coefficients['wheat'] * math.log1p(wheat)
                     + coefficients['rival'] * rival)
            fitness.append(value)
            entries.append({'wheat': wheat, 'rival': rival})
        remaining = list(range(count))
        order = []
        while remaining:
            weights = [math.exp(fitness[i]) for i in remaining]
            total = sum(weights)
            pick = rng.uniform(0, total)
            running = 0.0
            for position, i in enumerate(remaining):
                running += weights[position]
                if running >= pick:
                    order.append(i)
                    remaining.remove(i)
                    break
            else:
                order.append(remaining.pop())
        placement = {colony: rank + 1 for rank, colony in enumerate(order)}
        rows.append({'map': f'map-{index}', 'round': 'round-01', 'job': str(index),
                     'generator': 'synthetic', 'ai': 'nicowar', 'colonies': count,
                     'width': 7, 'height': 7, 'defaults': True, 'ticks': 1000, 'cap': False,
                     'engine_outcome': True,
                     'entries': [{'team': i, 'start': (i, 0), 'placement': placement[i],
                                  'won': placement[i] == 1,
                                  'measurements': {
                                      'band48_wheat_exclusive_amount': entry['wheat'],
                                      'nearest_rival_distance': entry['rival']}}
                                 for i, entry in enumerate(entries)]})
    return {'games': rows, 'skipped': {}}


class Fairness(unittest.TestCase):
    def test_equal_chances_are_perfectly_fair(self):
        for count in (2, 3, 4, 8):
            self.assertAlmostEqual(F.fairness([0.0] * count), 1.0)

    def test_one_colony_taking_everything_is_zero_at_every_count(self):
        # The raw Gini of n numbers cannot exceed (n-1)/n, so without the
        # normalisation two colonies could never score below 0.5.
        for count in (2, 4, 8):
            probabilities = [1.0] + [0.0] * (count - 1)
            self.assertAlmostEqual(F.gini(probabilities), 1.0)
            self.assertAlmostEqual(1.0 - F.gini(probabilities), 0.0)

    def test_a_single_colony_has_nobody_to_be_unfair_to(self):
        self.assertEqual(F.fairness([3.7]), 1.0)

    def test_fairness_ignores_the_unidentified_offset(self):
        # Softmax is invariant to a constant added to every fitness, so fairness
        # must be too: it is the part of the model the games actually determined.
        base = [0.4, -0.2, 1.1, 0.0]
        self.assertAlmostEqual(F.fairness(base), F.fairness([v + 9.0 for v in base]))

    def test_sharper_differences_are_less_fair(self):
        self.assertGreater(F.fairness([0.0, 0.2]), F.fairness([0.0, 2.0]))

    def test_softmax_sums_to_one_and_ranks_with_fitness(self):
        probabilities = F.softmax([0.0, 1.0, 2.0])
        self.assertAlmostEqual(sum(probabilities), 1.0)
        self.assertLess(probabilities[0], probabilities[1])
        self.assertLess(probabilities[1], probabilities[2])


@unittest.skipUnless(FITTING, 'fitting needs numpy and scipy')
class Likelihood(unittest.TestCase):
    def setUp(self):
        self.dataset = synthetic(120, {'wheat': 0.8, 'rival': -0.01}, seed=7)
        self.features = [('band48_wheat_exclusive_amount', 'log'),
                         ('nearest_rival_distance', 'identity')]

    def test_gradient_matches_a_numeric_one(self):
        import numpy as np
        problem = F.Problem(self.dataset, self.features)
        weights = np.array([0.4, -0.3])
        gradient = problem.log_likelihood(weights)[1]
        step = 1e-5
        for k in range(len(weights)):
            up, down = weights.copy(), weights.copy()
            up[k] += step
            down[k] -= step
            numeric = (problem.log_likelihood(up)[0] - problem.log_likelihood(down)[0]) / (2 * step)
            self.assertAlmostEqual(numeric / gradient[k], 1.0, places=6)

    def test_the_fit_recovers_the_coefficients_it_generated(self):
        dataset = synthetic(4000, {'wheat': 0.8, 'rival': -0.01}, seed=11)
        model = F.fit_model(dataset, self.features, ridge=1e-6)
        fitted = {item['name']: item['coefficient'] for item in model['features']}
        self.assertAlmostEqual(fitted['band48_wheat_exclusive_amount'], 0.8, delta=0.12)
        self.assertAlmostEqual(fitted['nearest_rival_distance'], -0.01, delta=0.003)
        self.assertGreater(model['train']['mcfadden_r2'], 0.05)

    def test_a_measurement_that_decides_nothing_is_fitted_near_zero(self):
        dataset = synthetic(1500, {'wheat': 0.8, 'rival': 0.0}, seed=13)
        model = F.fit_model(dataset, self.features, ridge=1e-6)
        fitted = {item['name']: item['coefficient'] for item in model['features']}
        self.assertLess(abs(fitted['nearest_rival_distance']), 0.004)

    def test_cross_validation_keeps_a_map_whole(self):
        dataset = synthetic(200, {'wheat': 0.8, 'rival': -0.01}, seed=17)
        score = F.cross_validated(dataset, self.features, folds=4, ridge=1e-6)
        self.assertEqual(score['folds'], 4)
        self.assertGreater(score['mcfadden_r2'], 0.0)
        self.assertGreater(score['accuracy'], score['chance_accuracy'])

    def test_predictions_are_probabilities_of_the_right_shape(self):
        model = F.fit_model(self.dataset, self.features)
        for row, game in zip(F.predicted(model, self.dataset), self.dataset['games']):
            self.assertEqual(len(row['probability']), len(game['entries']))
            self.assertAlmostEqual(sum(row['probability']), 1.0)
            self.assertGreaterEqual(row['fairness'], 0.0)
            self.assertLessEqual(row['fairness'], 1.0)


class Emission(unittest.TestCase):
    def test_every_measurement_the_model_may_use_has_a_cpp_expression(self):
        # Whatever a refit selects has to be emittable, so every name the
        # measurement extractor can produce needs an expression waiting for it.
        sample = F.colony_measurements(EMPTY_COLONY)
        missing = [name for name in sorted(sample)
                   if F.eligible_measurement(name) and not _has_expression(name)]
        self.assertEqual(missing, [])

    def test_a_share_term_sums_over_the_map(self):
        model = {'intercept': 0.0, 'features': [
            {'name': 'tied_nearest_tiles', 'transform': 'share', 'coefficient': -0.7}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'FairnessModel.h'
            F.emit_header(model, str(path), {'games': 1, 'maps': 1, 'rounds': ['round-01'],
                                             'ais': ['nicowar'], 'revision': 'test',
                                             'cv_r2': 0.0, 'train_r2': 0.0})
            text = path.read_text()
        self.assertIn('for (const ColonyQuality &other : colonies)', text)
        self.assertIn('total += double(other.tiedNearestTiles);', text)

    def test_transforms_all_have_a_cpp_form(self):
        for transform in F.TRANSFORMS:
            self.assertTrue(F.cpp_transform(transform, 'x', 'total'))

    def test_header_carries_the_coefficients_and_the_function(self):
        model = {'intercept': -1.25, 'features': [
            {'name': 'band48_wheat_exclusive_amount', 'transform': 'log', 'coefficient': 0.8},
            {'name': 'nearest_rival_distance', 'transform': 'identity', 'coefficient': -0.01}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'FairnessModel.h'
            F.emit_header(model, str(path), {'games': 200, 'maps': 200, 'rounds': ['round-01'],
                                             'ais': ['nicowar'], 'revision': 'test',
                                             'cv_r2': 0.1, 'train_r2': 0.11})
            text = path.read_text()
        self.assertIn('FAIRNESS_MODEL_INTERCEPT', text)
        self.assertIn('constexpr int FAIRNESS_MODEL_FEATURE_COUNT = 2;', text)
        self.assertIn('inline double startFitness', text)
        self.assertIn('fairnessModelMeasurement', text)
        self.assertIn('fairnessModelContribution', text)
        self.assertIn('distanceBands[2].exclusiveStoredAmount[WHEAT]', text)
        # A label a player can read, not the internal name.
        self.assertIn('Uncontested wheat stock within 48 steps', text)

    def test_labels_read_as_english(self):
        self.assertEqual(F.measurement_label('nearest_rival_distance'),
                         'Steps to the nearest rival')
        self.assertEqual(F.measurement_label('band24_wood_tiles'),
                         'Wood patches within 24 steps')
        self.assertEqual(F.measurement_label('build_sites_4x4'), 'Building sites')

    def test_the_legacy_factors_and_the_team_index_cannot_enter_the_model(self):
        self.assertFalse(F.eligible_measurement('legacy_total'))
        self.assertFalse(F.eligible_measurement('legacy_isolation'))
        self.assertFalse(F.eligible_measurement('team_index'))
        self.assertTrue(F.eligible_measurement('band48_wheat_exclusive_amount'))


def _has_expression(name):
    try:
        F.cpp_expression(name)
        return True
    except ValueError:
        return False


# A ColonyQuality report entry with every field present and zero, so the
# measurement list can be derived without generating a map.
EMPTY_COLONY = {
    'raw': {'resources': {name: {} for name in F.RESOURCES}},
    'distance_bands': [{'resources': {name: {} for name in F.RESOURCES}} for _ in range(3)],
    'normalized': {},
}


class Drawing(unittest.TestCase):
    def test_draws_are_deterministic_in_seed_and_round(self):
        catalog = {'generators': [{'method': 1, 'id': 'a', 'editorOnly': False, 'controls': [
            {'id': 'width', 'values': [6, 7, 8, 9]}, {'id': 'height', 'values': [6, 7, 8, 9]},
            {'id': 'teams', 'values': list(range(1, 13))},
            {'id': 'water', 'values': [10, 20, 30]}]}]}
        first = F.draw_matches(catalog, 20, 2, ('nicowar',), 5, 1)
        again = F.draw_matches(catalog, 20, 2, ('nicowar',), 5, 1)
        other = F.draw_matches(catalog, 20, 2, ('nicowar',), 5, 2)
        self.assertEqual(first, again)
        self.assertNotEqual(first, other)

    def test_a_drawn_map_has_room_for_its_colonies(self):
        catalog = {'generators': [{'method': 1, 'id': 'a', 'editorOnly': False, 'controls': [
            {'id': 'width', 'values': [6, 7, 8, 9]}, {'id': 'height', 'values': [6, 7, 8, 9]},
            {'id': 'teams', 'values': list(range(1, 13))}]}]}
        for match in F.draw_matches(catalog, 200, 1, ('nicowar',), 9, 1):
            area = 2 ** (match['width'] + match['height'])
            self.assertGreaterEqual(area, F.MIN_TILES_PER_COLONY * match['colonies'])
            self.assertIn(match['colonies'], F.COLONY_CHOICES)
            self.assertEqual(match['params']['teams'], match['colonies'])

    def test_best_of_k_curves_never_fall(self):
        rolls = [{'seed': 1, 'generated': True, 'score': 0.5, 'seconds': 0.01},
                 {'seed': 2, 'generated': False, 'score': 0.0, 'seconds': 0.01},
                 {'seed': 3, 'generated': True, 'score': 0.9, 'seconds': 0.02},
                 {'seed': 4, 'generated': True, 'score': 0.7, 'seconds': 0.01}]
        best, seconds = F.sampling_curves(rolls, 4)
        self.assertEqual(best, [0.5, 0.5, 0.9, 0.9])
        self.assertAlmostEqual(seconds[-1], 0.05)
        self.assertTrue(all(b <= c for b, c in zip(best, best[1:])))
        self.assertTrue(all(a <= b for a, b in zip(seconds, seconds[1:])))


if __name__ == '__main__':
    unittest.main(verbosity=2)
