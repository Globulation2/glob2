#!/usr/bin/env python3
"""Paired, map-clustered evaluation of the unified swarm controller."""
import argparse,gzip,json,math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
fit=json.loads((a.output/'multimap-fit.json').read_text());resolved=json.loads((a.after/'snapshot/resolved.json').read_text());values={r['key']:r['value'] for r in resolved['parameters']};params={k:values['economy.'+k] for k in fit['parameters']};scale=params['swarm_labor_scale_percent'];q=params['swarm_food_per_worker_percent'];s=params['swarm_pressure_sensitivity'];k=params['swarm_workers_per_building']
def policy(w,pop,critical,unserved,food):
 baseline=min(w*1000,math.isqrt(w*1000000)*scale//100);supply=food*100000;den=supply+q*baseline
 funded=baseline*supply//den if den else 0;pop=max(1,pop);funded=funded*pop//(pop+s*min(pop,max(critical,unserved)))
 workers=(funded+500)//1000;return workers,max(1,(workers+k-1)//k)
# Visualize the final, actual quantized policy, including dynamically rising food.
fig,axes=plt.subplots(1,3,figsize=(14,4));ws=np.arange(1,301)
for food,color in [(5,'#bf6a36'),(30,'#36927e'),(200,'#3d68ae')]:
 vals=[policy(int(w),int(w*1.5),0,0,food) for w in ws]
 for j in (0,1):axes[j].plot(ws,[v[j] for v in vals],label=f'Food capacity {food}',color=color)
axes[0].set(ylabel='Colony birth-worker budget',xlabel='Living workers');axes[1].set(ylabel='Desired swarms',xlabel='Living workers');axes[0].legend(fontsize=8)
fs=np.arange(0,201)
for pressure,color in [(0,'#3d68ae'),(.1,'#36927e'),(.3,'#bf6a36')]:
 axes[2].plot(fs,[policy(100,150,int(150*pressure),0,int(f))[0] for f in fs],label=f'Food pressure {pressure:.0%}',color=color)
axes[2].set(xlabel='Growing reachable food capacity',ylabel='Birth-worker budget (100 workers)');axes[2].legend(fontsize=8)
for ax in axes:ax.grid(alpha=.2);ax.spines[['top','right']].set_visible(False)
fig.suptitle(f'One controller: growth {scale/100:g}, food requirement {q/100:g}, hunger sensitivity {s}, workers/swarm {k}');fig.tight_layout();fig.savefig(a.output/'controller-curves.png',dpi=160);plt.close(fig)

def read(root):
 manifest=json.loads((root/'manifest.json').read_text());files=sorted((root/'match-telemetry').glob('*.json.gz'))
 if len(files)!=manifest['matches']:raise SystemExit(f'Incomplete validation {root}: {len(files)}/{manifest["matches"]}; curves saved, no outcome analysis')
 rows={};violations=[]
 for file in files:
  m=json.load(gzip.open(file,'rt'))
  if m['status']!='completed':raise ValueError('Process failed: '+str(file))
  player=next(p for p in m['players'] if p['role']=='candidate');team=player['team'];obs=[e for e in m['observer'] if e['team']==team];ds=[e for e in m['telemetry'] if e['event']=='director_snapshot']
  end=next((e['tick'] for e in obs if not int(e['alive'])),m['steps']);win=bool(player['won']);loss=bool(player['lost'])
  row={'id':m['id'],'map':m['map'],'seed':m['seed'],'seat':m['candidate_seat'],'won':int(win),'lost':int(loss),'unresolved':int(not win and not loss),'alive_or_won_30k':int(win or end>=30000),'steps':m['steps']}
  launches=[int(e['game_tick']) for e in m['telemetry'] if e['event']=='mission_phase_changed' and e.get('phase')=='transit']
  row['offensive_launch_any']=int(bool(launches));row['offensive_launch_by_30000']=int(any(t<=30000 for t in launches))
  for tick in [5000,10000,15000,20000,30000]:
   e=next((e for e in obs if e['tick']==tick),None)
   for metric in ['population','workers','swarms','swarm_assigned','swarm_working','critical_food','unserved_food']:
    row[f'{metric}_{tick}']=int(e[metric]) if e is not None else None
  duration=critical=unserved=0
  for i,e in enumerate(obs):
   lo=max(5000,e['tick']);hi=min(30000,end,obs[i+1]['tick'] if i+1<len(obs) else m['steps']);dt=max(0,hi-lo)
   duration+=dt;critical+=dt*int(e['critical_food'])/max(1,int(e['population']));unserved+=dt*int(e['unserved_food'])/max(1,int(e['population']))
  row['critical_fraction']=critical/duration if duration else None;row['unserved_fraction']=unserved/duration if duration else None
  dtot=zero=unfunded=0
  for i,e in enumerate(ds):
   tick=int(e['game_tick']);hi=min(30000,end,int(ds[i+1]['game_tick']) if i+1<len(ds) else end);dt=max(0,hi-max(5000,tick));dtot+=dt
   zero+=dt*(int(e['swarm_worker_budget'])==0);unfunded+=dt*(int(e['swarm_worker_budget'])==0 and int(e['desired_swarms'])>int(e['swarms']))
   if root==a.after and tick>=1000 and tick<end:
    expected=policy(int(e['workers']),int(e['population']),int(e['critical_food']),int(e['unserved_food']),int(e['accessible_corn']))
    actual=(int(e['swarm_worker_budget']),int(e['desired_swarms']))
    if expected!=actual:violations.append({'id':m['id'],'tick':tick,'expected':expected,'actual':actual})
  row['zero_birth_fraction']=zero/dtot if dtot else None;row['unfunded_expansion_fraction']=unfunded/dtot if dtot else None;rows[m['id']]=row
 return manifest,rows,violations
mb,b,_=read(a.before);ma,v,violations=read(a.after)
assert b.keys()==v.keys()
for i in b:
 assert [b[i][key] for key in ['map','seed','seat']]==[v[i][key] for key in ['map','seed','seat']]
maps=sorted({r['map'] for r in b.values()});rng=np.random.default_rng(2026090588)
metrics=['won','offensive_launch_any','offensive_launch_by_30000','alive_or_won_30k','population_10000','population_20000','swarms_20000','swarm_assigned_20000','swarm_working_20000','critical_fraction','unserved_fraction','zero_birth_fraction','unfunded_expansion_fraction']
result={'parameters':params,'games_per_arm':len(b),'maps':maps,'seeds_per_map':2,'max_game_ticks':120000,'outcomes':{},'paired':{},'per_map':{},'controller_violations':violations,'uncertainty':'95% paired bootstrap over whole maps, equal map weighting; 20000 resamples. Nine map clusters and two fresh seeds/map are a screening experiment, not proof of optimality. Checkpoints use only pairs with a recorded sample in both arms; eliminated teams are included if the game continued.'}
for arm,rows in [('before',b),('after',v)]:result['outcomes'][arm]={key:sum(r[key] for r in rows.values()) for key in ['won','lost','unresolved']}
for metric in metrics:
 groups=[];bv=[];vv=[]
 for name in maps:
  ids=[i for i in b if b[i]['map']==name and b[i].get(metric) is not None and v[i].get(metric) is not None]
  if ids:groups.append(np.mean([v[i][metric]-b[i][metric] for i in ids]));bv.extend(b[i][metric] for i in ids);vv.extend(v[i][metric] for i in ids)
 if groups:
  boot=np.mean(rng.choice(groups,size=(20000,len(groups)),replace=True),axis=1)
  result['paired'][metric]={'pairs':len(bv),'before_mean':float(np.mean(bv)),'after_mean':float(np.mean(vv)),'equal_map_delta':float(np.mean(groups)),'map_cluster_ci95':np.quantile(boot,[.025,.975]).tolist()}
for name in maps:
 ids=[i for i in b if b[i]['map']==name];pairs=[i for i in ids if b[i]['population_20000'] is not None and v[i]['population_20000'] is not None]
 result['per_map'][name]={'games':len(ids),'before_wins':sum(b[i]['won'] for i in ids),'after_wins':sum(v[i]['won'] for i in ids),'population_20k_pairs':len(pairs),'population_20k_before':float(np.mean([b[i]['population_20000'] for i in pairs])) if pairs else None,'population_20k_after':float(np.mean([v[i]['population_20000'] for i in pairs])) if pairs else None}
(a.output/'paired-validation.json').write_text(json.dumps(result,indent=2)+'\n');(a.output/'paired-game-features.json').write_text(json.dumps({'before':list(b.values()),'after':list(v.values())},indent=2)+'\n')
fig,ax=plt.subplots(figsize=(10,4));ix=np.arange(len(maps));ax.bar(ix-.18,[result['per_map'][m]['before_wins']/result['per_map'][m]['games'] for m in maps],.36,label='Before',color='#7b8896');ax.bar(ix+.18,[result['per_map'][m]['after_wins']/result['per_map'][m]['games'] for m in maps],.36,label='Unified controller',color='#36927e');ax.set_xticks(ix,maps,rotation=25,ha='right');ax.set(ylabel='Win fraction',ylim=(0,1),title='Fresh seeds, every seat; 4 or 8 games per map per arm');ax.legend();ax.spines[['top','right']].set_visible(False);fig.tight_layout();fig.savefig(a.output/'paired-map-outcomes.png',dpi=160)
print(json.dumps(result,indent=2))
