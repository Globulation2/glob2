#!/usr/bin/env python3
"""Audit swarm capacity, staffing and birth-budget throttles from saved games."""
import json,gzip,sys
from pathlib import Path
from statistics import mean
from collections import defaultdict,Counter
from bisect import bisect_right
root=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
metrics=('population','workers','swarms','swarm_assigned','swarm_working','swarm_empty','swarm_corn','swarm_corn_reachable','swarm_corn_distance','inns','inn_assigned','inn_working','inn_corn','building_sites','construction_assigned','construction_working','technology_assigned','technology_working','military_assigned','military_working','critical_food','unserved_food','free_workers','clearing_flag_units')
rows=[];checkpoint_rows=[]
for file in sorted((root/'match-telemetry').glob('*.json.gz')):
 with gzip.open(file,'rt') as h:m=json.load(h)
 team=next(x['team'] for x in m['players'] if x['role']=='candidate');obs=defaultdict(dict)
 for e in m['observer']:obs[e['tick']][e['team']]=e
 own=sorted((e for e in m['observer'] if e['team']==team),key=lambda e:e['tick']);end=next((e['tick'] for e in own if int(e['alive'])==0),m['steps']);first_invasion=next((e['tick'] for e in own if int(e.get('enemy_warriors_near_colony',0))>=3),end)
 def val(e,k):return int(e.get(k,0))
 for tick in (5000,10000,15000,20000,25000,30000):
  es=obs[tick];o=es[team];ns=[e for t,e in es.items() if t!=team]
  checkpoint_rows.append({'id':m['id'],'block':m['block'],'tick':tick,'alive':int(o['alive']),**{'maxima_'+k:val(o,k) for k in metrics},**{'nicowar_'+k:mean(val(e,k) for e in ns) for k in metrics}})
 ds=sorted((e for e in m['telemetry'] if e['event']=='director_snapshot'),key=lambda e:int(e['game_tick']));duration=Counter();pre=Counter();budgethist=Counter();examples=[]
 for i,e in enumerate(ds):
  start=int(e['game_tick']);nxt=int(ds[i+1]['game_tick']) if i+1<len(ds) else end
  lo=max(5000,start);hi=min(30000,end,nxt);dt=max(0,hi-lo);pdt=max(0,min(hi,first_invasion)-lo)
  if not dt:continue
  pop=val(e,'population');critical=val(e,'critical_food');unserved=val(e,'unserved_food');budget=val(e,'swarm_worker_budget');swarms=val(e,'swarms');desired=val(e,'desired_swarms')
  stress=pop>0 and (critical*100>=pop*12 or unserved*100>=pop*12 or max(critical,unserved)*100>=pop*20)
  flags={'total':True,'budget_zero':budget==0,'budget_at_most_two':budget<=2,'service_stress':stress,'zero_with_service_stress':budget==0 and stress,'zero_without_service_stress':budget==0 and not stress,'demand_more_with_budget_zero':desired>swarms and budget==0,'at_least_four_swarms':swarms>=4,'four_swarms_budget_at_most_ten':swarms>=4 and budget<=10,'budget_twenty':budget==20,'birth_staffing_below_plan':val(e,'swarm_workers')<budget}
  for k,v in flags.items():
   if v:duration[k]+=dt;pre[k]+=pdt
  budgethist[str(budget)]+=dt
  if desired>swarms and budget==0 and len(examples)<2:examples.append(e)
 rows.append({'id':m['id'],'block':m['block'],'seat':m['candidate_seat'],'life_end':end,'first_invasion':first_invasion,'duration':dict(duration),'pre_invasion_duration':dict(pre),'budget_ticks':dict(budgethist),'examples':examples})
summary=[]
for tick in (5000,10000,15000,20000,25000,30000):
 rs=[r for r in checkpoint_rows if r['tick']==tick];summary.append({'tick':tick,'games':len(rs),'alive_rate':mean(r['alive'] for r in rs),**{k:mean(r[k] for r in rs) for k in rs[0] if k.startswith(('maxima_','nicowar_'))}})
pooled={}
for k in ('duration','pre_invasion_duration','budget_ticks'):
 c=Counter()
 for r in rows:c.update(r[k])
 pooled[k]=dict(c)
(out/'analysis.json').write_text(json.dumps({'games':len(rows),'checkpoints':summary,**pooled},indent=2)+'\n');(out/'per-game.json').write_text(json.dumps(rows,indent=2)+'\n');(out/'checkpoint-rows.json').write_text(json.dumps(checkpoint_rows,separators=(',',':'))+'\n')
for r in summary:print(r['tick'],{k:round(r[k],2) for k in ('maxima_population','nicowar_population','maxima_swarms','nicowar_swarms','maxima_swarm_assigned','nicowar_swarm_assigned','maxima_swarm_working','nicowar_swarm_working','maxima_swarm_empty','nicowar_swarm_empty','maxima_construction_working','nicowar_construction_working')})
print(json.dumps(pooled,indent=2))
