#!/usr/bin/env python3
"""Recover detailed swarm staffing/retirement events retained in raw logs."""
import gzip,json,sys
from pathlib import Path
from collections import Counter,defaultdict
from bisect import bisect_right
root=Path(sys.argv[1]);out=Path(sys.argv[2]);games=json.loads((out/'per-game.json').read_text());data=[]
for g in games:
 staffing=[];summary=[];remote=defaultdict(set);assigned={};zero=[];events=Counter();retired=[]
 with gzip.open(root/f"logs/match-{g['id']:05d}.log.gz",'rt') as h:
  for line in h:
   if not line.startswith('MAXIMA_TELEMETRY\t'):continue
   parts=line.rstrip().split('\t');event=parts[3]
   if event not in ('swarm_staffing','swarm_retirement','swarm_retirement_summary','swarm_retirement_issued','colony_swarm_selected','colony_swarm_completed'):continue
   e=dict(p.split('=',1) for p in parts[4:] if '=' in p);tick=int(e['game_tick']);ai=parts[1];events[event]+=1
   if event=='swarm_staffing':
    assigned[e['building_id']]=int(e['workers']);staffing.append({'tick':tick,**e})
    if int(e['colony_budget'])>0 and int(e['workers'])==0:zero.append(e)
   if event=='swarm_retirement':remote[ai].add(e['building_id'])
   if event=='swarm_retirement_issued':retired.append(e);assigned.pop(e['building_id'],None)
   if event=='swarm_retirement_summary':
    summary.append({'tick':tick,'completed':int(e['useful_swarms'])+len(remote[ai]),**e})
 cps=[];times=[e['tick'] for e in summary]
 for t in (5000,10000,15000,20000,25000,30000):
  i=bisect_right(times,t)-1
  if i>=0 and t<g['life_end']:cps.append({'tick':t,**{k:summary[i][k] for k in ('completed','useful_swarms','desired_swarms','ready','safe')}})
 data.append({'id':g['id'],'block':g['block'],'checkpoints':cps,'zero_assignment_with_positive_budget':zero,'retirements':retired,'events':dict(events),'summary':summary,'staffing':staffing})
(out/'swarm-events.json').write_text(json.dumps(data,separators=(',',':'))+'\n')
from statistics import mean
for t in (5000,10000,15000,20000,25000,30000):
 cps=[c for g in data for c in g['checkpoints'] if c['tick']==t];print(t,len(cps),{k:mean(float(c[k]) for c in cps) for k in ('completed','useful_swarms','desired_swarms','ready')})
print('games with zero-worker assignment at positive budget',sum(bool(g['zero_assignment_with_positive_budget']) for g in data))
print('retirements',sum(len(g['retirements']) for g in data))
