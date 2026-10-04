"""Independent MM validation on a reproducible, synthetic 20,000-game cohort."""
import itertools
import json
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.tournaments.duel_ratings import fit, summarize
rng = random.Random(339359)
names = list('abcdefgh')
pairs = list(itertools.combinations(range(8), 2))
games = []
wins = [0.0] * 8
totals = [[0] * 8 for _ in names]
for block in range(10000):
    a, b = pairs[block % len(pairs)]
    probability = 1 / (1 + math.exp((b - a) / 4))
    for side in range(2):
        won = rng.random() < probability
        players = [names[a], names[b]]
        placements = [1, 2] if won else [2, 1]
        if side:
            players.reverse()
            placements.reverse()
        games.append({'job_id': str(2*block+side), 'block': str(block),
            'generator': block % 60, 'build': 'synthetic', 'format': '1v1',
            'seeds': {'map': block, 'game': block},
            'competitors': players, 'placements': placements})
        wins[a if won else b] += 1
        totals[a][b] += 1
        totals[b][a] += 1
# MM multiplicative updates independently maximize the same likelihood.
strength = [1.0] * 8
for iteration in range(10000):
    updated = [wins[i] / sum(totals[i][j] / (strength[i]+strength[j])
                            for j in range(8) if j != i) for i in range(8)]
    centre = math.exp(sum(map(math.log, updated)) / 8)
    updated = [value/centre for value in updated]
    delta = max(abs(math.log(a/b)) for a,b in zip(updated,strength))
    strength = updated
    if delta < 1e-13:
        break
else:
    raise AssertionError('independent MM failed to converge')
expected = {name: 1500+400*math.log10(value) for name,value in zip(names,strength)}
actual = fit(games)
error = max(abs(actual[name]-expected[name]) for name in names)
assert error < 1e-7, error
for _ in range(10):
    rng.shuffle(games)
    assert fit(games) == actual
intervals = summarize(games, draws=20, seed=1)
rng.shuffle(games)
assert summarize(games, draws=20, seed=1) == intervals
print(json.dumps({'games':len(games),'paired_blocks':10000,'generators':60,
    'seed':339359,'independent_mm_iterations':iteration+1,'max_elo_difference':error,
    'identical_shuffled_fits':10,'identical_bootstrap_shuffles':True,
    'bootstrap_draws':20,'ratings':actual},indent=2))
