"""Held-out stress for geometric pressure and unsigned seed boundaries.

The main corner grid uses a 256-square, four-colony map. Repeat its endpoint
interactions at the smallest supported map and at 12 colonies on a large map:
these exercise different home/edge geometry. High seed bits are a separate input
boundary; do not assume a run of small consecutive seeds covers that behavior.
"""
import itertools
import json
from pathlib import Path
import sys
from tools.tournaments.model import job,validate_experiment
main=json.loads(Path(sys.argv[1]).read_text());sample=main['jobs'][0]
defaults={c['id']:c['default'] for c in main['study']['catalog']['controls']}
jobs=[]
def add(variant,params,seed):
 jobs.append(job('generate_map',sample['build'],seeds={'map':seed},
    config={'generator':33,'params':defaults|params,'candidates':0},outputs={'map':False},
    limits={'timeout_seconds':120},labels={'variant':variant,'map_seed':seed}))
for size,teams in [(7,2),(9,12)]:
 for i,(r,p,w,pond,wheat,wood) in enumerate(itertools.product([3,11],[6,12],[0,100],[3,6],[0,300],[0,300])):
  add('small-corners' if size==7 else 'large-corners',dict(width=size,height=size,teams=teams,
      workers=8,**{'ridge-depth':r,'pass-width':p,'warp':w,'pond-size':pond,
      'wheat-amount':wheat,'wood-amount':wood,'algae-amount':300,'fruit-amount':300,
      'extra-passes':0,'wooded-saddles':100}),78000+(size-7)*100+i)
for seed in [0,1,2,2**16-1,2**16,2**31-1,2**31,2**32-1]:
 add('seed-boundaries',{},seed)
manifest=validate_experiment({'schema_version':1,'id':'highlands-bulk-boundaries-r4-20260915',
    'jobs':jobs,'settings':{'prefetch':8,'heartbeat_seconds':2,'transfer_slots':16},
    'study':{'purpose':'Held-out small/large corner interactions and uint32 seed boundaries'}})
Path(sys.argv[2]).write_text(json.dumps(manifest,indent=2)+'\n');print(len(jobs))
