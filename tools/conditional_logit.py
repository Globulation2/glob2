#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Conditional-logit estimation over contests with several competing entries.

This is the estimator shared by the models that rank competitors inside a
contest and read a win probability off the ranking. It knows nothing about what
a contest is: `tools/fairness_model.py` fits one per map, where the entries are
starting positions, and `tools/win_probability_model.py` fits one per 512-tick
slice of a game, where the entries are the surviving players.

Every entry gets a fitness

    F_i = intercept + sum_k coefficient_k * transform_k(measurement_k(entry_i))

and the probability that entry i wins its contest is softmax(F)_i. Fitting
maximises the Plackett-Luce likelihood of the observed finishing orders, which
is the same model read off the whole ranking instead of the winner alone. Ties
share a stage (the Breslow treatment).

A dataset is a dict with a 'games' list; each game has an 'entries' list, and
each entry a 'measurements' dict plus a 'placement' (lower is better). The key
names are historical -- a "game" here is any contest.
"""
import math
import random

# Softmax is invariant to adding a per-contest constant to every fitness, so only
# differences within one contest carry information. `identity` therefore reads as
# an additive advantage, `log` as a ratio between entries (and is the only
# transform that is automatically free of scale), and `share` states an entry's
# cut of what the whole contest has to give.
TRANSFORMS = {
    'identity': lambda x, total: x,
    'log': lambda x, total: math.log1p(max(x, 0.0)),
    'sqrt': lambda x, total: math.sqrt(max(x, 0.0)),
    'square': lambda x, total: x * x,
    'share': lambda x, total: (x / total) if total > 0 else 0.0,
    'decay24': lambda x, total: math.exp(-max(x, 0.0) / 24.0),
}


def feature_column(dataset, name, transform):
    """One transformed column, one row per entry per contest, contest-grouped.

    A measurement absent from a contest is zero for every entry of it, which
    softmax absorbs: that contest simply says nothing about this feature, rather
    than claiming all its entries were equal on it.
    """
    function = TRANSFORMS[transform]
    columns = []
    for game in dataset['games']:
        values = [entry['measurements'].get(name, 0.0) for entry in game['entries']]
        total = sum(values)
        columns.append([function(value, total) for value in values])
    return columns


def orderings(dataset):
    """Finishing order per contest as tied groups, best first."""
    result = []
    for game in dataset['games']:
        places = [entry['placement'] for entry in game['entries']]
        result.append([[index for index, place in enumerate(places) if place == value]
                       for value in sorted(set(places))])
    return result


def padded(dataset, features):
    """Pack the contests into rectangular arrays: [contest, slot, feature] plus masks.

    Contests differ in how many entries they have, so every array is padded to
    the widest one and a mask marks the real slots. Everything downstream works a
    finishing stage at a time across all contests at once, which keeps the fits
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
            # Contests with no stage left contribute nothing; keep their arithmetic
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
    # the mean fitness over the fitted entries is zero; every reported absolute
    # level is relative to that convention, never something the contests measured.
    intercept = -float(problem.centre @ raw)
    return {'features': [{'name': name, 'transform': transform, 'coefficient': float(coefficient)}
                         for (name, transform), coefficient in zip(features, raw)],
            'intercept': intercept, 'log_likelihood': float(value),
            'train': problem.evaluate(weights)}


def softmax(values):
    top = max(values)
    weights = [math.exp(value - top) for value in values]
    total = sum(weights)
    return [weight / total for weight in weights]
