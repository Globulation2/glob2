from pathlib import Path
import json,csv,numpy as np
root=Path(__file__).parent/'runs';rows=[];lifetimes=[]
for w in ['holiday','large-eleven']:
 for seed in range(42,74):
  for mode in ['baseline','proactive','heuristic']:
   d=root/w/f'seed-{seed}-{mode}-p4';r=json.loads((d/'game/result.json').read_text());a=r['gradient_attribution'];n=r['benchmark_measured_ticks'];fields=list(csv.DictReader(open(d/'fields.csv')))
   for f in fields:
    for k in f:f[k]=int(f[k])
   c=dict(workload=w,seed=seed,policy=mode,ticks=n,run_cpu_s=r['benchmark_run_cpu_ns']/1e9)
   for k,v in a.items():
    if isinstance(v,int):c[k+'_per_tick']=v/n
   for stage,v in a['search'].items():
    for k,value in v.items():c['search_'+stage+'_'+k+'_per_tick']=value/n
   c['private_seed_cpu_ns_per_tick']=sum(f['seed_cpu_ns'] for f in fields)/n
   c['gradient_cpu_ns_per_tick']=(sum(f['seed_cpu_ns'] for f in fields)+a['map_cpu_ns']+sum(a['search'][st]['cpu_ns'] for st in ['owner','ranking','primary']))/n
   c['all_seeds_per_tick']=(a['private_seeds']+a['map_seeds'])/n
   c['unit_nonsearch_cpu_ns_per_tick']=(a['unit_cpu_ns']-a['owner_unit_cpu_ns'])/n
   c['hiring_nonsearch_cpu_ns_per_tick']=(a['hiring_cpu_ns']-sum(f['seed_cpu_ns'] for f in fields)-a['search']['ranking']['cpu_ns']-a['search']['primary']['cpu_ns'])/n
   c['residual_cpu_ns_per_tick']=(r['benchmark_run_cpu_ns']-sum(a[k] for k in ['hiring_cpu_ns','observe_cpu_ns','map_cpu_ns','unit_cpu_ns','building_cpu_ns','ai_cpu_ns']))/n
   c['total_cpu_ns_per_tick']=r['benchmark_run_cpu_ns']/n
   maps=[{k:int(v) for k,v in rr.items()} for rr in csv.DictReader(open(d/'fields.csv.map.csv'))]
   c['map_cold_per_tick']=sum(f['cold'] for f in maps)/n
   c['map_warm_per_tick']=sum(not f['cold'] for f in maps)/n
   c['map_full_depth_per_tick']=sum(f['depth']>10000 for f in maps)/n
   c['map_replacing_proactive_per_tick']=sum(f['from_proactive'] for f in maps)/n
   c['map_warm_mean_age']=np.mean([f['captured']-f['previous_tick'] for f in maps if not f['cold']]) if any(not f['cold'] for f in maps) else 0
   c['map_entries_per_unitstep']=a['search']['map_seed']['entries']/a['unit_steps']
   c['gradient_entries_per_unitstep']=sum(v['entries'] for v in a['search'].values())/a['unit_steps']
   c['private_seeds_per_million_unitsteps']=a['private_seeds']/a['unit_steps']*1e6
   pro=[f for f in fields if f['proactive']]
   categories={'proactive_fields':pro,'speculative_fields':[f for f in pro if f['speculative']], 'proactive_discarded':[f for f in pro if f['retired']==2], 'proactive_unconsumed_retired':[f for f in pro if f['retired'] and not(f['flags']&(1|4))], 'proactive_never_owner_retired':[f for f in pro if f['retired'] and not(f['flags']&4)],'proactive_unused_active':[f for f in pro if not f['retired'] and not(f['flags']&(1|4))], 'proactive_owner_consumed':[f for f in pro if f['flags']&4],'proactive_rank_only':[f for f in pro if f['flags']&1 and not f['flags']&4],'proactive_own_reset_unused':[f for f in pro if f['retired']==12 and not(f['flags']&(1|4))], 'proactive_idle_unused':[f for f in pro if f['retired']==11 and not(f['flags']&(1|4))]}
   for cat,fs in categories.items():
    c[cat+'_count']=len(fs);c[cat+'_per_tick']=len(fs)/n;c[cat+'_seed_cpu_ns_per_tick']=sum(f['seed_cpu_ns'] for f in fs)/n
   for f in pro:lifetimes.append(dict(workload=w,seed=seed,policy=mode,**f))
   rows.append(c)
with (root/'per-game.csv').open('w',newline='') as f:
 wr=csv.DictWriter(f,fieldnames=list(rows[0]));wr.writeheader();wr.writerows(rows)
with (root/'field-lifetimes.csv').open('w',newline='') as f:
 wr=csv.DictWriter(f,fieldnames=list(lifetimes[0]));wr.writeheader();wr.writerows(lifetimes)
rng=np.random.default_rng(20261018);idx=rng.integers(0,32,(10000,32));groups=[]
for w in ['holiday','large-eleven']:
 modes={m:[r for r in rows if r['workload']==w and r['policy']==m] for m in ['baseline','proactive','heuristic']}
 metrics={}
 for key in rows[0]:
  if key in ['workload','seed','policy']:continue
  arrays={m:np.array([r[key] for r in rr],float) for m,rr in modes.items()};b=arrays['baseline'];p=arrays['proactive'];h=arrays['heuristic'];d=p[idx].mean(1)-b[idx].mean(1)
  metrics[key]={'baseline':float(b.mean()),'proactive':float(p.mean()),'heuristic':float(h.mean()),'delta':float(p.mean()-b.mean()),'delta_ci':np.quantile(d,[.025,.975]).tolist(),'change_pct':float(100*(p.mean()/b.mean()-1)) if b.mean() else None,'heuristic_minus_proactive':float(h.mean()-p.mean()),'heuristic_minus_proactive_ci':np.quantile(h[idx].mean(1)-p[idx].mean(1),[.025,.975]).tolist()}
 groups.append(dict(workload=w,metrics=metrics))
(root/'summary.json').write_text(json.dumps(groups,indent=2));print(json.dumps(groups,indent=2))
