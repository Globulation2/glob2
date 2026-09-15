"""Reproducible broad sweep; run from repo root with PYTHONPATH=.

This plans jobs through the shared tournament framework, never launches engines.
Topology is exhaustive at two seeds. The much larger joint slider space is sampled,
with a separate full ridge/pass/warp interaction grid and adversarial resource ends.
No request is filtered using a prediction of whether the generator will accept it.
"""
import argparse
import itertools
import json
from pathlib import Path
import random
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.model import job, validate_experiment

p = argparse.ArgumentParser()
p.add_argument('bundle'); p.add_argument('output')
a = p.parse_args()
b = inspect_bundle(a.bundle)
assert b['capabilities']['map_report_version'] == 2
assert b['capabilities']['generation_telemetry_version'] == 1
g = next(g for g in b['capabilities']['generators'] if g['method'] == 33)
assert g['revision'] == 4
# The supplied immutable bundle identifies the tested source and executable;
# reproducing its jobs does not require the current checkout to match it.
controls = {c['id']: c['values'] for c in g['controls']}
defaults = {c['id']: c['default'] for c in g['controls']}
rng = random.Random(20260915)
jobs = []
def add(cohort, changes, seed):
    params = defaults | changes
    assert all(v in controls[k] for k, v in params.items())
    jobs.append(job('generate_map', b['id'], seeds={'map': seed},
        config={'generator': 33, 'params': params, 'candidates': 0},
        outputs={'map': False}, limits={'timeout_seconds': 120},
        labels={'variant': cohort, 'map_seed': seed, 'sample': len(jobs)}))

# All 960 size/colony/valley settings, including unsupported settings. Paired seeds
# expose seed-dependent failures without disguising them as envelope exclusions.
for w,h,t,v in itertools.product(controls['width'],controls['height'],controls['teams'],controls['valley-size']):
    for seed in (71001,71002):
        add('topology',dict(width=w,height=h,teams=t,**{'valley-size':v}),seed)
# Individual levels on spacious home ground isolate each control from overcrowding.
for key,values in controls.items():
    for value in values:
        for seed in (72001,72002):
            add('individual-levels',dict(width=9,height=9) | {key:value},seed)
# Every ridge/pass/warp combination: rasterized crossing geometry is the risky
# interaction. Alternate the two pond endpoints and include the tightest cells.
for i,(r,p,w) in enumerate(itertools.product(controls['ridge-depth'],controls['pass-width'],controls['warp'])):
    add('crossing-grid',{'ridge-depth':r,'pass-width':p,'warp':w,'pond-size':3 if i%2 else 6},73000+i)
# Uniform registered-level draws, using only >=128 dimensions to spend more samples
# on actual construction. These still include oversized valleys/too many colonies.
for i in range(1024):
    params = {k:rng.choice(v) for k,v in controls.items()}
    params.update(width=rng.choice([7,8,9]),height=rng.choice([7,8,9]))
    add('mixed',params,74000+i)
# All geometry and resource endpoint combinations on a valid 256-square layout.
for i,(r,p,w,pond,wheat,wood) in enumerate(itertools.product([3,11],[6,12],[0,100],[3,6],[0,300],[0,300])):
    add('corners',{'ridge-depth':r,'pass-width':p,'warp':w,'pond-size':pond,
        'wheat-amount':wheat,'wood-amount':wood,'algae-amount':300,'fruit-amount':300,
        'workers':8,'extra-passes':0,'wooded-saddles':100},76000+i)
for seed in range(77000,77128):
    add('default-seeds',{},seed)
manifest = validate_experiment({'schema_version':1,'id':'highlands-bulk-r4-20260915',
    'jobs':jobs,'settings':{'prefetch':8},
    'study':{'hypotheses':['Supported layouts generate without seed-dependent failure.',
        'Crossing extremes preserve ridges, containment and connected walk routes.',
        'Scarce and abundant starts retain crop access and construction room.'],
        'catalog':g,'sampling_seed':20260915,'platform':'linux-x86_64'}})
Path(a.output).write_text(json.dumps(manifest,indent=2)+'\n')
from collections import Counter
print(len(jobs),dict(Counter(j['labels']['variant'] for j in jobs)))
