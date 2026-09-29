# SPDX-License-Identifier: GPL-3.0-or-later
"""Existing fairness statistics, shared without changing their estimators."""
import math
import random
import statistics

Z95 = 1.959963984540054
# Two-sided 95% Student t quantiles for 1..30 degrees of freedom.
T975 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145,
        2.131, 2.120, 2.110, 2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048,
        2.045, 2.042]
MIN_MAPS_FOR_INTERVAL = 5  # below this a bootstrap over maps cannot say much
FACTORS = ['wheat', 'wood', 'fertility', 'depth', 'room', 'isolation']
COLONY_FIELDS = ['wheat_distance', 'wood_distance', 'catchment_tiles', 'build_sites',
                 'resource_amount', 'rival_distance', 'rivals_within_threat', 'mean_fertility',
                 *FACTORS, 'total']

def gamma_q(a, x):
    """Regularized upper incomplete gamma Q(a, x)."""
    if x <= 0:
        return 1.0
    log_prefix = -x + a * math.log(x) - math.lgamma(a)
    if x < a + 1:
        term = total = 1.0 / a
        ap = a
        for _ in range(100000):
            ap += 1
            term *= x / ap
            total += term
            if abs(term) < abs(total) * 1e-15:
                break
        return max(0.0, 1.0 - total * math.exp(log_prefix))
    tiny = 1e-300
    b = x + 1 - a
    c = 1 / tiny
    d = 1 / b
    h = d
    for i in range(1, 100000):
        an = -i * (i - a)
        b += 2
        d = an * d + b
        d = d if abs(d) > tiny else tiny
        c = b + an / c
        c = c if abs(c) > tiny else tiny
        d = 1 / d
        h *= d * c
        if abs(d * c - 1) < 1e-15:
            break
    return min(1.0, math.exp(log_prefix) * h)


def chi_square_uniform(counts):
    n, k = sum(counts), len(counts)
    if n == 0 or k < 2:
        return None, None
    expected = n / k
    statistic = sum((c - expected) ** 2 / expected for c in counts)
    return statistic, gamma_q((k - 1) / 2, statistic / 2)


def compositions(n, k):
    if k == 1:
        yield (n,)
        return
    for first in range(n + 1):
        for rest in compositions(n - first, k - 1):
            yield (first,) + rest


def exact_uniform_p(counts, seed, draws=20000):
    """Multinomial goodness of fit against uniform: P(an outcome no more likely than observed).

    Exact by enumeration while the outcome space is small, otherwise a seeded Monte Carlo."""
    n, k = sum(counts), len(counts)
    if n == 0 or k < 2:
        return None, 'none'
    base = math.lgamma(n + 1) - n * math.log(k)
    log_p = lambda xs: base - sum(math.lgamma(x + 1) for x in xs)
    observed = log_p(counts) + 1e-9
    if math.comb(n + k - 1, k - 1) <= 250000:
        return min(1.0, sum(math.exp(log_p(c)) for c in compositions(n, k) if log_p(c) <= observed)), 'exact'
    rng = random.Random(seed)
    hits = 0
    for _ in range(draws):
        xs = [0] * k
        for _ in range(n):
            xs[rng.randrange(k)] += 1
        hits += log_p(xs) <= observed
    return (hits + 1) / (draws + 1), 'monte-carlo'


def wilson(k, n):
    if n == 0:
        return None, None
    p = k / n
    denominator = 1 + Z95 ** 2 / n
    centre = (p + Z95 ** 2 / (2 * n)) / denominator
    half = Z95 * math.sqrt(p * (1 - p) / n + Z95 ** 2 / (4 * n * n)) / denominator
    return max(0.0, centre - half), min(1.0, centre + half)


def benjamini_hochberg(p_values):
    m = len(p_values)
    order = sorted(range(m), key=lambda i: p_values[i])
    adjusted = [1.0] * m
    running = 1.0
    for rank in range(m, 0, -1):
        i = order[rank - 1]
        running = min(running, p_values[i] * m / rank)
        adjusted[i] = running
    return adjusted


def holm(p_values):
    m = len(p_values)
    order = sorted(range(m), key=lambda i: p_values[i])
    adjusted = [1.0] * m
    running = 0.0
    for rank, i in enumerate(order):
        running = max(running, min(1.0, (m - rank) * p_values[i]))
        adjusted[i] = running
    return adjusted


def ranks(values):
    order = sorted(range(len(values)), key=lambda i: values[i])
    result = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and values[order[j + 1]] == values[order[i]]:
            j += 1
        for position in range(i, j + 1):
            result[order[position]] = (i + j) / 2 + 1
        i = j + 1
    return result


def pearson(x, y):
    if len(x) < 3:
        return None
    mx, my = sum(x) / len(x), sum(y) / len(y)
    sxx = sum((a - mx) ** 2 for a in x)
    syy = sum((b - my) ** 2 for b in y)
    if sxx <= 1e-18 or syy <= 1e-18:
        return None
    return sum((a - mx) * (b - my) for a, b in zip(x, y)) / math.sqrt(sxx * syy)


def spearman(x, y):
    return pearson(ranks([round(v, 9) for v in x]), ranks([round(v, 9) for v in y]))


def bootstrap(items, statistic, draws, seed):
    """Percentile 95% interval of statistic over items resampled with replacement."""
    if len(items) < 2:
        return None
    rng = random.Random(seed)
    values = []
    for _ in range(draws):
        value = statistic([items[rng.randrange(len(items))] for _ in items])
        if value is not None:
            values.append(value)
    if len(values) < draws / 2:
        return None
    values.sort()
    return [values[int(0.025 * (len(values) - 1))], values[int(0.975 * (len(values) - 1))]]


def t975(df):
    """Two-sided 95% Student t quantile; past 30 degrees of freedom a close approximation."""
    return T975[df - 1] if df <= len(T975) else Z95 + 2.4 / df


def squared_bias(counts):
    """Unbiased estimate of sum_s (p_s - 1/k)^2 from multinomial counts.

    E[chi2] = (k - 1) + (n - 1) k sum_s (p_s - 1/k)^2, so the plain squared deviation of the
    observed shares, which is positive even for a perfectly fair map, is corrected for chance."""
    n, k = sum(counts), len(counts)
    if n < 2 or k < 2:
        return None
    statistic, _ = chi_square_uniform(counts)
    return (statistic - (k - 1)) / ((n - 1) * k)


def rms_points(mean_squared_bias, k):
    """Root-mean-square deviation of per-start win probability from 1/k, in percentage points."""
    if mean_squared_bias is None:
        return None
    return 100 * math.sqrt(max(0.0, mean_squared_bias) / k)


def bias_interval(biases, k):
    """95% t interval over maps for the mean squared bias, expressed as position bias in points.

    Each map's estimate carries its own game-to-game noise, so the spread across maps covers that
    noise as well as real differences between maps. With few maps it is honestly wide."""
    if len(biases) < 2:
        return None
    mean = statistics.mean(biases)
    half = t975(len(biases) - 1) * statistics.stdev(biases) / math.sqrt(len(biases))
    return [rms_points(mean - half, k), rms_points(mean + half, k)]


_FLOOR_CACHE = {}


def fair_map_floor(game_counts, k, draws=2000):
    """95th percentile of the position-bias headline if every map were perfectly fair, given the
    same number of decided games per map: the level a headline has to clear to mean anything."""
    if not game_counts:
        return None
    key = (tuple(sorted(game_counts)), k, draws)
    if key not in _FLOOR_CACHE:
        rng = random.Random(hash_text(repr(key)))
        values = []
        for _ in range(draws):
            biases = []
            for n in key[0]:
                counts = [0] * k
                for _ in range(n):
                    counts[rng.randrange(k)] += 1
                biases.append(squared_bias(counts))
            values.append(rms_points(statistics.mean(biases), k))
        values.sort()
        if len(_FLOOR_CACHE) >= 256:
            _FLOOR_CACHE.pop(next(iter(_FLOOR_CACHE)))
        _FLOOR_CACHE[key] = values[int(0.95 * (len(values) - 1))]
    return _FLOOR_CACHE[key]


def share_table(counts, seed):
    n, k = sum(counts), len(counts)
    statistic, p_chi2 = chi_square_uniform(counts)
    p_exact, method = exact_uniform_p(counts, seed)
    best = max(range(k), key=lambda i: (counts[i], -i)) if n else None
    table = {'counts': counts, 'n': n, 'chi2': statistic, 'p_chi2': p_chi2, 'p': p_exact,
             'p_method': method, 'best': best, 'best_share': counts[best] / n if n else None,
             'best_share_wilson': list(wilson(counts[best], n)) if n else None,
             'dominance': counts[best] / n * k if n else None,
             'squared_bias': squared_bias(counts)}
    table['rms_points'] = rms_points(table['squared_bias'], k)
    table['shares_wilson'] = [list(wilson(c, n)) if n else None for c in counts]
    return table


def hash_text(text):
    value = 2166136261
    for ch in text.encode():
        value = ((value ^ ch) * 16777619) % (2 ** 32)
    return value


def seed_for(*parts):
    return hash_text('/'.join(str(p) for p in parts))


# ---------------------------------------------------------------------------- analysis

