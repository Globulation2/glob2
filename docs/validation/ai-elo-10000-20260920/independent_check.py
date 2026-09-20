"""Independent multiplicative MM fit to validate the Newton rating solver."""
import gzip
import json
import math
import random
from pathlib import Path
from rating_model import fit

folder=Path(__file__).resolve().parent
games=json.load(gzip.open(folder/'outcomes.json.gz','rt'))
reference=fit(games)
names=sorted(reference)
wins={name:0.0 for name in names}
matches={}
for game in games:
    a,b=game['competitors'];pa,pb=game['placements']
    score=1.0 if pa<pb else 0.0 if pa>pb else .5
    wins[a]+=score;wins[b]+=1-score
    pair=tuple(sorted((a,b)));matches[pair]=matches.get(pair,0)+1
strength={name:1.0 for name in names}
for iteration in range(10000):
    next_strength={name:wins[name]/sum(count/(strength[a]+strength[b])
        for (a,b),count in matches.items() if name in (a,b)) for name in names}
    centre=sum(math.log(v) for v in next_strength.values())/len(names)
    next_strength={name:math.exp(math.log(v)-centre) for name,v in next_strength.items()}
    if max(abs(math.log(next_strength[n]/strength[n])) for n in names)<1e-12:break
    strength=next_strength
else:raise AssertionError('Independent MM fit did not converge')
ratings={name:1500+400*math.log10(v) for name,v in next_strength.items()}
error=max(abs(ratings[n]-reference[n]) for n in names)
assert error<1e-7
for seed in range(10):
    random.Random(seed).shuffle(games)
    assert fit(games)==reference
print(f'Independent MM vs Newton maximum difference: {error:.12g} Elo')
print(f'MM iterations: {iteration+1}')
print('10 input shuffles: identical ratings')
print(f'All {len(games)} original games retained.')
