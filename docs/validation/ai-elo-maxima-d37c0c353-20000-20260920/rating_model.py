"""Order-independent Bradley–Terry fit in Elo points, using only the stdlib.

Each duel contributes one unit: win=1, draw=0.5, loss=0. Mean rating is
1500; differences use 400/log(10) points per log odds. No prior or K factor.
Requires a strongly connected directed score graph for a finite unique fit.
"""
from collections import defaultdict
import math
import random


def _solve(matrix, vector):
    a = [list(row) + [value] for row, value in zip(matrix, vector)]
    n = len(a)
    for col in range(n):
        pivot = max(range(col, n), key=lambda row: abs(a[row][col]))
        a[col], a[pivot] = a[pivot], a[col]
        if abs(a[col][col]) < 1e-14:
            raise ValueError('Singular rating fit; more connected comparisons are required')
        scale = a[col][col]
        a[col] = [v / scale for v in a[col]]
        for row in range(n):
            if row != col:
                scale = a[row][col]
                a[row] = [x - scale*y for x, y in zip(a[row], a[col])]
    return [row[-1] for row in a]


def fit(games):
    names = sorted({name for g in games for name in g['competitors']})
    if len(names) < 2:
        raise ValueError('At least two competitors are required')
    index = {name: i for i, name in enumerate(names)}
    pairs = defaultdict(lambda: [0, 0.0])
    edges = [set() for _ in names]
    for g in games:
        if len(g['competitors']) != 2 or len(g['placements']) != 2:
            raise ValueError('Batch duel ratings require exactly two competitors')
        a, b = (index[name] for name in g['competitors'])
        if a == b:
            raise ValueError('Self-play cannot identify relative strength')
        pa, pb = g['placements']
        if not all(math.isfinite(p) for p in (pa, pb)):
            raise ValueError('Non-finite placement')
        score = 1.0 if pa < pb else 0.0 if pa > pb else 0.5
        if score > 0: edges[a].add(b)
        if score < 1: edges[b].add(a)
        if a > b: a, b, score = b, a, 1-score
        pairs[a, b][0] += 1
        pairs[a, b][1] += score
    # Strong connectivity is the finite-MLE condition, including fractional draws.
    for start in range(len(names)):
        seen, todo = {start}, [start]
        while todo:
            for nxt in edges[todo.pop()] - seen:
                seen.add(nxt)
                todo.append(nxt)
        if len(seen) != len(names):
            raise ValueError('No finite batch rating: disconnected or undefeated group; collect more comparisons')
    pairs = sorted(pairs.items())
    n = len(names)-1  # Last coefficient fixed to zero while solving.
    theta = [0.0]*len(names)

    def loss(x):
        return math.fsum(total*(max(d, 0)+math.log1p(math.exp(-abs(d))))-wins*d
                         for (a, b), (total, wins) in pairs for d in [x[a]-x[b]])

    for _ in range(100):
        gradient, hessian = [0.0]*n, [[0.0]*n for _ in range(n)]
        for (a, b), (total, wins) in pairs:
            d = theta[a]-theta[b]
            p = 1/(1+math.exp(-d)) if d >= 0 else math.exp(d)/(1+math.exp(d))
            residual, weight = total*p-wins, total*p*(1-p)
            if a < n: gradient[a] += residual; hessian[a][a] += weight
            if b < n: gradient[b] -= residual; hessian[b][b] += weight
            if a < n and b < n: hessian[a][b] -= weight; hessian[b][a] -= weight
        if max(abs(g) for g in gradient) < 1e-8:
            centre = math.fsum(theta)/len(theta)
            return {name: 1500+(value-centre)*400/math.log(10) for name, value in zip(names, theta)}
        step = _solve(hessian, gradient)
        scale, before = 1.0, loss(theta)
        while scale > 1e-10:
            candidate = [theta[i]-scale*step[i] for i in range(n)]+[0.0]
            if loss(candidate) <= before + 1e-10:
                theta = candidate
                break
            scale *= 0.5
        else:
            raise ValueError('Batch rating line search failed')
    raise ValueError('Batch rating fit did not converge')


def percentile(values, p):
    values = sorted(values)
    position = p*(len(values)-1)
    low = int(position)
    return values[low]+(values[min(low+1,len(values)-1)]-values[low])*(position-low)


def summarize(games, draws=1000, seed=1):
    """Resample whole side-swap blocks within each generator, keeping its quota.

    Sort before seeded resampling, so both fit and intervals ignore input order.
    Invalid bootstrap fits fail explicitly rather than silently dropping draws.
    """
    if draws < 1: raise ValueError('At least one bootstrap draw is required')
    if len({g['job_id'] for g in games}) != len(games):
        raise ValueError('Duplicate job IDs would count games more than once')
    blocks = defaultdict(list)
    for game in games: blocks[game['block']].append(game)
    strata = defaultdict(list)
    for key, block in sorted(blocks.items()):
        if len(block) != 2 or block[0]['competitors'] != block[1]['competitors'][::-1]:
            raise ValueError('Every block must contain exactly two swapped-side games')
        if block[0]['generator'] != block[1]['generator'] or block[0]['build'] != block[1]['build']:
            raise ValueError('Paired games must share generator and build')
        if block[0].get('seeds') != block[1].get('seeds'):
            raise ValueError('Paired games must share seeds')
        strata[str(block[0]['generator'])].append(key)
    rng, samples = random.Random(seed), defaultdict(list)
    for _ in range(draws):
        sampled = [game for _, keys in sorted(strata.items()) for _ in keys
                   for game in blocks[rng.choice(keys)]]
        for name, value in fit(sampled).items(): samples[name].append(value)
    ratings = fit(games)
    return {'method': 'bradley_terry_batch', 'rating_centre': 1500,
            'points_per_tenfold_odds': 400, 'draw_score': 0.5, 'regularization': None,
            'order': 'independent', 'games': len(games), 'blocks': len(blocks),
            'ratings': ratings, 'displayed_ratings': {k: round(v) for k,v in ratings.items()},
            'bootstrap_draws': draws, 'bootstrap_seed': seed,
            'bootstrap_unit': 'swapped-side pair, stratified by generator',
            'intervals_95_percent': {k: [percentile(v,.025),percentile(v,.975)] for k,v in sorted(samples.items())}}


def report(rows, draws=1000, seed=1, pool_builds=False):
    """Report separate build cohorts unless the caller explicitly pools them.

    Partial progress may lack a finite fit or complete paired blocks. Report that
    explicitly; never substitute sequential Elo or silently omit bootstrap draws.
    """
    groups = defaultdict(list)
    builds = {row['build'] for row in rows}
    for row in rows:
        if row['format'] == '1v1':
            key = '1v1' if pool_builds or len(builds)==1 else '1v1:'+row['build'][:12]
            groups[key].append(row)
    ratings, details = {}, {}
    for key, games in sorted(groups.items()):
        detail = {'games':len(games), 'method':'bradley_terry_batch'}
        try:
            ratings[key] = fit(games)
        except ValueError as error:
            detail['unavailable_reason'] = str(error)
            details[key] = detail
            continue
        blocks = defaultdict(list)
        for row in games: blocks[row['block']].append(row)
        complete = [row for block in blocks.values() if len(block)==2 for row in block]
        detail['complete_paired_games'] = len(complete)
        try:
            uncertainty = summarize(complete,draws,seed)
            detail['uncertainty'] = {k:v for k,v in uncertainty.items()
                                     if k not in ('ratings','displayed_ratings')}
        except ValueError as error:
            detail['uncertainty_unavailable_reason'] = str(error)
        details[key] = detail
    return {'ratings':ratings, 'cohorts':details, 'pool_builds':pool_builds}
