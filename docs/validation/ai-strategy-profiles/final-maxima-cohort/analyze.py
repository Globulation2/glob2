"""Exploratory map associations, adjusted for opponent and binary cohort.

Randomize paired block scores within matchup/build strata. This preserves every
map's matchup/build schedule and swapped-side pairing. max-T adjustment covers
all tested generator/AI and catalog-tag/AI cells jointly under exchangeability.
"""
from collections import defaultdict,Counter
import gzip,json,math,random,subprocess
from pathlib import Path
import re
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--repository",type=Path,default=Path.cwd())
parser.add_argument("--evidence",type=Path,default=Path(__file__).resolve().parent)
parser.add_argument("--catalog", type=Path, default=Path(__file__).resolve().parent / "catalog.json")
args=parser.parse_args()
root=Path(__file__).resolve().parent
repo=args.repository.resolve()
revision='d37c0c353d9c2c68b4543286aa9b14e23ae7e526'
data=args.evidence.resolve()
games=json.load(gzip.open(data/'outcomes.json.gz','rt'))
coverage=json.loads(args.catalog.read_text())
names={g['method']:g['name'] for g in coverage['generators']}
files=subprocess.check_output(['git','ls-tree','-r','--name-only',revision,'src/map/generator/generators'],cwd=repo,text=True).splitlines()
tags={}
for file in files:
 if not file.endswith('.cpp'):continue
 text=subprocess.check_output(['git','show',revision+':'+file],cwd=repo,text=True)
 for method,name in names.items():
  if re.search(r'"'+re.escape(name)+r'"\s*,\s*'+str(method)+r'\s*,',text):
   tags[method]=sorted(set(re.findall(r'"((?:terrain|feature|style|fairness):[^"\n]+)"',text)))
assert set(tags)==set(names),(set(names)-set(tags))
counts=Counter(t for ts in tags.values() for t in ts)
allowed={t for t,n in counts.items() if 3<=n<60}
blocks=defaultdict(list)
for g in games:blocks[g['block']].append(g)
strata=defaultdict(list)
for key,gs in sorted(blocks.items()):
 assert len(gs)==2 and gs[0]['competitors']==gs[1]['competitors'][::-1]
 a,b=sorted(gs[0]['competitors']);score=0
 for g in gs:
  i=g['competitors'].index(a);pa,pb=g['placements'][i],g['placements'][1-i]
  score+=1 if pa<pb else 0 if pa>pb else .5
 method=gs[0]['generator']
 cats=['generator:'+names[method]]+[t for t in tags[method] if t in allowed]
 strata[(a,b,gs[0]['build'])].append({'score':score/2,'cats':cats,'cap':sum(g['cap'] for g in gs)/2,'ticks':sum(g['ticks'] for g in gs)/2})
cells={}
for (a,b,build),bs in strata.items():
 for block in bs:
  for cat in block['cats']:
   for name in (a,b):cells.setdefault((cat,name),len(cells))
keys=sorted(cells,key=cells.get);n=len(cells)
observed=[0.]*n;expected=[0.]*n;variance=[0.]*n;blocks_n=[0]*n;caps=[0.]*n
spec=[]
for (a,b,build),bs in sorted(strata.items()):
 values=[x['score'] for x in bs];N=len(values);mean=sum(values)/N;popvar=sum((x-mean)**2 for x in values)/N
 indices=[];alloc=Counter()
 for block in bs:
  target=[]
  for cat in block['cats']:
   ia,ib=cells[cat,a],cells[cat,b]
   target.append((ia,ib));alloc[ia]+=1;alloc[ib]+=1
   for idx,score,exp in ((ia,block['score'],mean),(ib,1-block['score'],1-mean)):
    observed[idx]+=score;expected[idx]+=exp;blocks_n[idx]+=1;caps[idx]+=block['cap']
  indices.append(target)
 for idx,m in alloc.items():variance[idx]+=m*(N-m)/(N-1)*popvar if N>1 else 0
 spec.append((values,indices))
sd=[math.sqrt(v) for v in variance]
z=[abs(o-e)/s if s else 0 for o,e,s in zip(observed,expected,sd)]
rng=random.Random(20260920);draws=2000;exceed=[0]*n;maximum=[]
for iteration in range(draws):
 totals=[0.]*n
 for values,indices in spec:
  perm=values[:];rng.shuffle(perm)
  for value,targets in zip(perm,indices):
   for ia,ib in targets:totals[ia]+=value;totals[ib]+=1-value
 perm_z=[abs(v-e)/s if s else 0 for v,e,s in zip(totals,expected,sd)]
 maximum.append(max(perm_z))
 for i,value in enumerate(perm_z):exceed[i]+=value>=z[i]-1e-12
 if iteration%500==0:print('permutations',iteration,flush=True)
rows=[]
for i,(cat,ai) in enumerate(keys):
 rows.append({'category':cat,'ai':ai,'games':2*blocks_n[i],'paired_blocks':blocks_n[i],
  'score_percent':100*observed[i]/blocks_n[i],'expected_percent':100*expected[i]/blocks_n[i],
  'difference_pp':100*(observed[i]-expected[i])/blocks_n[i],
  'cap_percent':100*caps[i]/blocks_n[i], 'z':z[i],
  'p_permutation':(1+exceed[i])/(draws+1),
  'p_maxT':(1+sum(m>=z[i]-1e-12 for m in maximum))/(draws+1)})
# Sensitivity only: count surviving capped duels as draws, preserving engine wins.
policy_groups=defaultdict(list)
for game in games:
 a,b=sorted(game['competitors']);i=game['competitors'].index(a)
 pa,pb=game['placements'][i],game['placements'][1-i]
 score=1.0 if pa<pb else 0.0 if pa>pb else .5
 if game['cap'] and not game['engine_outcome'] and game['teams'][i]['alive'] and game['teams'][1-i]['alive']:
  score=.5
 policy_groups[a,b,game['build']].append((game,score))
sensitivity=defaultdict(list)
for (a,b,build),gs in policy_groups.items():
 mean=sum(value for _,value in gs)/len(gs)
 for game,value in gs:
  cats=['generator:'+names[game['generator']]]+[t for t in tags[game['generator']] if t in allowed]
  for cat in cats:
   sensitivity[cat,a].append((value,mean));sensitivity[cat,b].append((1-value,1-mean))
for row in rows:
 vals=sensitivity[row['category'],row['ai']]
 row['survivor_draw_score_percent']=100*sum(a for a,b in vals)/len(vals)
 row['survivor_draw_difference_pp']=100*sum(a-b for a,b in vals)/len(vals)
rows.sort(key=lambda r:r['p_maxT'])
report={'games':len(games),'blocks':len(blocks),'source':revision,'permutations':draws,'seed':20260920,'tests':n,
 'method':'Opponent/build-adjusted paired score residuals; within-matchup/build block permutation, joint max-T correction',
 'notes':['Exploratory associations, not causal map-feature effects.','Catalog tags overlap; frozen source tags, minimum three generators per tag.','Score rate counts ties as half a win.','Tick-cap adjudication can change patterns.','Completed cohorts only; keep changed AI versions separate.'],
 'generator_tags':tags,'rows':rows}
(root/'patterns.json').write_text(json.dumps(report,indent=2)+'\n')
for kind in ('generator:','feature:','terrain:','style:'):
 print('\n',kind)
 for r in [r for r in rows if r['category'].startswith(kind)][:24]:print(r)
