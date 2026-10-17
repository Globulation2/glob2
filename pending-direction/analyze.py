from pathlib import Path
import csv,gzip,json,collections
import numpy as np
BASE=Path(__file__).parent
ROWS=[];SEEDS=[]
for p in sorted((BASE/'runs').glob('*/seed-*/directions.csv.gz')):
 meta=json.loads((p.parent/'run.json').read_text());w=p.parent.parent.name;seed=int(p.parent.name.split('-')[1])
 SEEDS.append(dict(workload=w,seed=seed,**{k:v for k,v in meta.items() if k not in ['command','environment']}))
 for r in csv.DictReader(gzip.open(p,'rt')):
  q={k:int(v) for k,v in r.items()};q.update(workload=w,seed=seed);ROWS.append(q)
subsets={
 'movement':lambda r:r['kind']==0,
 'movement-origin-open':lambda r:r['kind']==0 and r['here']>1,
 'movement-origin-blocked':lambda r:r['kind']==0 and r['here']==0,
 'movement-footprint':lambda r:r['kind']==0 and r['route']==0,
 'movement-combat':lambda r:r['kind']==0 and r['route']==2,
 'movement-clearing':lambda r:r['kind']==0 and r['route']==1,
 'service-selection':lambda r:r['kind']==2 and r['medical'] in [1,2],
 'all-selection':lambda r:r['kind'] in [1,2],
}
per=[]
for w in ['holiday','large-eleven']:
 for seed in sorted({r['seed'] for r in ROWS if r['workload']==w}):
  src=[r for r in ROWS if r['workload']==w and r['seed']==seed]
  for v in [0,1]:
   for name,subset in subsets.items():
    for dedup in [False,True]:
     data=[r for r in src if r['variant']==v and subset(r) and not r['at_goal']]
     if dedup:
      seen=set();unique=[]
      for r in data:
       key=tuple(r[k] for k in ['unit_identity','unit','building','building_identity','route','swim','x','y'])
       if key in seen:continue
       seen.add(key);unique.append(r)
      data=unique
     c=collections.Counter(n=len(data))
     for r in data:
      origin=r['here'] if r['here']>1 else r['best_legal_score']
      available=r['best_legal_score']>=0
      legal=bool(r['legal']);move=bool(r['dx'] or r['dy']);reach=r['next']>1
      c['origin_blocked']+=r['here']==0
      c['legal']+=legal;c['blocked']+=move and not legal;c['stopped']+=not move
      c['route_from_origin_or_neighbor']+=r['here']>1 or available
      c['progress']+=legal and reach and origin>=0 and r['next']>origin
      c['flat']+=legal and reach and origin>=0 and r['next']==origin
      c['worse']+=legal and reach and origin>=0 and r['next']<origin
      c['off_route']+=legal and not reach
      c['best_available']+=legal and available and r['chosen_score']==r['best_legal_score']
      c['has_gradient_progress_step']+=available
      if legal and available and reach:
       excess=max(0,r['best_legal_score']-r['chosen_score'])
       c['scorable']+=1;c['excess_cost_sum']+=excess;c['excess_positive']+=excess>0
       c['within_one_step']+=excess<=10
       c['worse_available_penalty_gt_2_steps']+=excess>20
     per.append(dict(workload=w,seed=seed,variant=v,subset=name,dedup=dedup,**dict(c)))
metrics=['origin_blocked','legal','blocked','stopped','progress','flat','worse','off_route','best_available','has_gradient_progress_step','route_from_origin_or_neighbor','within_one_step','worse_available_penalty_gt_2_steps']
rng=np.random.default_rng(1197);summaries=[]
for w in ['holiday','large-eleven']:
 for v in [0,1]:
  for name in subsets:
   for dedup in [False,True]:
    group=[r for r in per if (r['workload'],r['variant'],r['subset'],r['dedup'])==(w,v,name,dedup)]
    if not group:continue
    counts=np.array([r['n'] for r in group],dtype=float);total=counts.sum()
    if not total:continue
    idx=rng.integers(0,len(group),size=(10000,len(group)));den=counts[idx].sum(axis=1)
    result=dict(workload=w,variant=v,subset=name,dedup=dedup,seeds=len(group),n=int(total),metrics={})
    for k in metrics:
     nums=np.array([r.get(k,0) for r in group]);samples=np.divide(nums[idx].sum(axis=1),den,out=np.zeros_like(den),where=den>0)
     result['metrics'][k]={'count':int(nums.sum()),'pct':100*float(nums.sum()/total),'ci95':list(100*np.percentile(samples,[2.5,97.5])),'mean_game_pct':float(np.mean([100*r.get(k,0)/r['n'] for r in group if r['n']]))}
    summaries.append(result)
(BASE/'summary.json').write_text(json.dumps({'runs':len(SEEDS),'ticks':sum(r['ticks'] for r in SEEDS),'pending_queries':sum(r['samples'] for r in SEEDS),'all_match_reference':all(r.get('matches_uninstrumented_simulation') for r in SEEDS),'results':summaries},indent=2))
with (BASE/'per-game.csv').open('w') as f:
 keys=sorted({k for r in per for k in r});writer=csv.DictWriter(f,fieldnames=keys);writer.writeheader();writer.writerows(per)
lines=['| Map | Variant | Requests | Legal | Makes progress | Longer route | Best available step |','|---|---|---:|---:|---:|---:|---:|']
for r in summaries:
 if r['subset']!='movement' or r['dedup']:continue
 m=r['metrics'];lines.append(f"| {r['workload']} | {r['variant']} | {r['n']} | {m['legal']['pct']:.2f}% | {m['progress']['pct']:.2f}% | {m['worse']['pct']:.2f}% | {m['best_available']['pct']:.2f}% |")
(BASE/'table.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines));print('runs',len(SEEDS),'queries',sum(r['samples'] for r in SEEDS))

worst=[];error_groups=collections.defaultdict(list)
for r in ROWS:
 if r['kind']!=0 or r['variant']!=1 or not r['legal'] or r['next']<=1 or r['at_goal']:continue
 origin=r['here'] if r['here']>1 else r['best_legal_score']
 if origin<0:continue
 increase=(origin-r['next'])/10
 if increase>0:
  q=dict(r,remaining_distance_increase=increase);worst.append(q)
  error_groups[(r['workload'],'blocked' if r['here']==0 else 'open')].append(increase)
(BASE/'worst-examples.json').write_text(json.dumps(sorted(worst,key=lambda r:r['remaining_distance_increase'],reverse=True)[:30],indent=2))
(BASE/'wrong-step-distribution.json').write_text(json.dumps({w+':'+kind:{'n':len(vals),'median':float(np.median(vals)),'p95':float(np.percentile(vals,95)),'max':max(vals),'gt_two_tiles':sum(v>2 for v in vals)} for (w,kind),vals in error_groups.items()},indent=2))
