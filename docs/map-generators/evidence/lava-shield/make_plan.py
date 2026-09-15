"""Predeclared, reproducible single-candidate Lava shield parameter sweep.

The study spans each legal size/density corner, all layout endpoints, and resource
scarcity/crowding. Combined settings are sampled only inside the documented
request envelope; an invalid request is not counted as a generator failure.
Maps are not written because the unrelated 12-team cyclic-rotation artifact
verifier rejects some otherwise valid maps. Native map reports and telemetry are
still collected for every logical job.
"""
import json
import random
from pathlib import Path
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json
from tools.tournaments.model import job, validate_experiment

ROOT = Path(__file__).resolve().parent
BUNDLE = Path('artifacts/lava-shield/playtest/bundles/687e1efa6c12e9ef2bba17f472af4eb204ff9a3a35406716dfd42bd44371584e').resolve()
BUILD = inspect_bundle(BUNDLE)['id']
RNG = random.Random(20260915)
# Width/height are power-of-two exponents in the structured engine interface.
# Include both rectangle orientations because coast and start rasterization can
# respond differently to the stretch axis and toroidal wrapping.
SHAPES = [(7,7,2),(7,8,2),(8,7,2),(8,8,2),(8,8,4),(8,8,8),
          (8,9,2),(8,9,4),(8,9,8),(9,8,2),(9,8,4),(9,8,8),
          (9,9,2),(9,9,4),(9,9,8),(9,9,12)]
DEFAULT = {'tongue-count':5,'long-tongues':50,'branching':2,'rim-width':8,
           'wheat-amount':100,'wood-amount':100,'stone-amount':100,
           'algae-amount':100,'fruit-amount':100,'workers':4}
RESOURCE = ['wheat-amount','wood-amount','stone-amount','algae-amount','fruit-amount']
jobs = []
seen = set()

def legal(p):
    short = min(1 << p['width'], 1 << p['height'])
    return not ((short == 128 or (short == 256 and p['teams'] > 4)) and
                (p['tongue-count'] > 5 or p['rim-width'] > 8))

def add(shape, changes, seed, label):
    w,h,teams = shape
    p = dict(DEFAULT, width=w, height=h, teams=teams)
    p.update(changes)
    if not legal(p):
        return
    key = (tuple(sorted(p.items())),seed)
    if key in seen:
        return
    seen.add(key)
    jobs.append(job('generate_map', BUILD, seeds={'map':seed},
                    config={'generator':33,'params':p,'candidates':0},
                    outputs={'map':False}, limits={'timeout_seconds':120},
                    labels={'variant':label,'shape':f'{1<<w}x{1<<h}x{teams}',
                            'map_seed':seed,'generator':33}))

for i,shape in enumerate(SHAPES):
    # Two independent baselines per shape give every shape a seed tail.
    for seed in (41001,41002):
        add(shape,{},seed,'baseline')
    compact = shape[0] == 7 or shape[1] == 7 or (min(shape[:2]) == 8 and shape[2] > 4)
    high = {'tongue-count':5 if compact else 9,'rim-width':8 if compact else 12,
            'long-tongues':75,'branching':3}
    low = {'tongue-count':3,'rim-width':6,'long-tongues':25,'branching':0}
    for seed in (42001,42002,42003):
        add(shape,high,seed,'high-geometry')
        add(shape,low,seed,'low-geometry')
    add(shape,{'workers':1},43001,'workers-min')
    add(shape,{'workers':8},43002,'workers-max')

# Exercise every registered resource endpoint independently and in conjunction.
for shape in (SHAPES[0],SHAPES[4],SHAPES[-1]):
    for k in RESOURCE:
        for v in (0,300):
            for seed in (44001,44002):
                add(shape,{k:v},seed,f'{k}-{v}')
    for v in (0,300):
        for seed in (44003,44004):
            add(shape,{k:v for k in RESOURCE},seed,f'all-resources-{v}')

# Fixed random settings cross independent controls and map geometry. Sample the
# entire legal envelope, including the compact conditional range, rather than
# filtering failed jobs after seeing outcomes.
for i in range(192):
    shape = SHAPES[i % len(SHAPES)]
    compact = shape[0] == 7 or shape[1] == 7 or (min(shape[:2]) == 8 and shape[2] > 4)
    changes = {'tongue-count':RNG.randint(3,5 if compact else 9),
               'long-tongues':RNG.choice((25,50,75)),
               'branching':RNG.randint(0,3),
               'rim-width':RNG.choice((6,8) if compact else (6,8,10,12)),
               'workers':RNG.randint(1,8)}
    changes.update({k:RNG.randrange(0,301,25) for k in RESOURCE})
    add(shape,changes,50000+i,'combined-random')

manifest = {'schema_version':1,'id':'lava-shield-bulk-20260915-v1',
            'kind':'generator_stress','jobs':jobs,
            'labels':{'hypothesis':'Legal controls mostly construct valid crater-rim islands on compact, rectangular, and dense layouts.'},
            'design':{'shapes':SHAPES,'random_seed':20260915,
                      'random_samples':192,'candidates':0,'map_files':False}}
validate_experiment(manifest)
atomic_json(ROOT/'plan.json',manifest)
print(json.dumps({'jobs':len(jobs),'build':BUILD,'shapes':len(SHAPES)}))
