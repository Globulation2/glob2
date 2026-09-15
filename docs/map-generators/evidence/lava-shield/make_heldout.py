"""Disjoint seeds for intermediate colony counts and dense legal controls.

The first matrix emphasizes capacity boundaries. These jobs cover every missing
registered team count and independently confirm high-density geometry. They use
the same immutable build but no previously studied map seed.
"""
import json
from pathlib import Path
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json
from tools.tournaments.model import job,validate_experiment

ROOT=Path(__file__).resolve().parent
BUILD=inspect_bundle(Path('artifacts/lava-shield/playtest/bundles/687e1efa6c12e9ef2bba17f472af4eb204ff9a3a35406716dfd42bd44371584e').resolve())['id']
jobs=[]

def add(w,h,teams,seed,controls,label):
    p={'width':w,'height':h,'teams':teams,'workers':4,
       'tongue-count':5,'long-tongues':50,'branching':2,'rim-width':8,
       'wheat-amount':100,'wood-amount':100,'stone-amount':100,
       'algae-amount':100,'fruit-amount':100}
    p.update(controls)
    jobs.append(job('generate_map',BUILD,seeds={'map':seed},
                    config={'generator':33,'params':p,'candidates':0},
                    outputs={'map':False},limits={'timeout_seconds':120},
                    labels={'variant':label,'shape':f'{1<<w}x{1<<h}x{teams}',
                            'map_seed':seed,'generator':33}))

# One team is supported by the shared control; check it at every size. The
# middle counts otherwise receive both default and legal geometry endpoint.
for i,(w,h,teams) in enumerate([(7,7,1),(8,8,1),(9,9,1),
                                 (8,8,3),(8,8,5),(8,8,6),(8,8,7),
                                 (9,9,3),(9,9,5),(9,9,6),(9,9,7),
                                 (9,9,9),(9,9,10),(9,9,11)]):
    for seed in (61001,61002):
        add(w,h,teams,seed,{},'intermediate-default')
    compact=w==7 or (w==8 and teams>4)
    high={'tongue-count':5 if compact else 9,'rim-width':8 if compact else 12,
          'long-tongues':75,'branching':3,'workers':8}
    for seed in (61003,61004):
        add(w,h,teams,seed,high,'intermediate-high')

# Fresh dense seeds challenge the exact condition that needed a narrow beach
# repair in prior studies, without choosing seeds after seeing this run.
for seed in range(62001,62025):
    add(9,9,12,seed,{'tongue-count':9,'rim-width':12,
                      'long-tongues':75,'branching':3,'workers':8},'dense-heldout')

manifest={'schema_version':1,'id':'lava-shield-heldout-20260915-v1',
          'kind':'generator_stress','jobs':jobs,
          'design':{'hypothesis':'Intermediate colony counts and dense extremes retain reliable construction.',
                    'seeds':'61001-61004 and 62001-62024; disjoint from bulk study'},
          'labels':{'build':BUILD}}
validate_experiment(manifest)
atomic_json(ROOT/'heldout-plan.json',manifest)
print(json.dumps({'jobs':len(jobs),'build':BUILD}))
