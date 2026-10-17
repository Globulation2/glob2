from pathlib import Path
import json,csv,numpy as np
root=Path(__file__).parent/'runs'
rows=[]
for w in ['holiday','large-eleven']:
 for seed in range(42,74):
  for p in [1,4]:
   for mode in ['baseline','proactive']:
    d=root/w/f'seed-{seed}-{mode}-p{p}'
    r=json.loads((d/'game/result.json').read_text());a=np.fromfile(d/'tick-ns.u64',dtype=np.uint64).astype(float)/1000
    assert len(a)==r['benchmark_measured_ticks']
    windows=a[:len(a)//8*8].reshape(-1,8).sum(axis=1)/8
    rows.append(dict(workload=w,seed=seed,participants=p,policy=mode,ticks=len(a),cpu_s=r['benchmark_run_cpu_ns']/1e9,cpu_us_tick=r['benchmark_run_cpu_ns']/len(a)/1000,mean_us=a.mean(),p99_us=np.quantile(a,.99),p999_us=np.quantile(a,.999),max_us=a.max(),adjacent_us=np.abs(np.diff(a)).mean(),window8_p99_us=np.quantile(windows,.99),over1ms_pct=100*np.mean(a>1000),over4ms_pct=100*np.mean(a>4000),queries_per_tick=r['route_queries']/len(a),pending_per_1k_ticks=1000*r['route_pending']/len(a),pending_pct=100*r['route_pending']/max(1,r['route_queries']),pending_ticks_pct=100*r['route_pending_ticks']/len(a),pending_queries=r['route_pending'],scheduled_jobs=r['building_gradient_jobs'],scheduled_discarded=r['building_gradient_discarded'],synchronous=r['building_gradient_synchronous'],finalChecksum=r['finalChecksum']))
for w in ['holiday','large-eleven']:
 for seed in range(42,74):
  values=[json.loads((root/w/f'seed-{seed}-{m}-p{p}/game/result.json').read_text()) for m in ['baseline','proactive'] for p in [1,4]]
  assert len({v['initialChecksum'] for v in values})==1,(w,seed,'initial state')
  for i in [0,2]:assert values[i]['finalChecksum']==values[i+1]['finalChecksum'],(w,seed,'participants')
with (root/'per-game.csv').open('w',newline='') as f:
 wr=csv.DictWriter(f,fieldnames=list(rows[0]));wr.writeheader();wr.writerows(rows)
rng=np.random.default_rng(20261017);indices=rng.integers(0,32,size=(10000,32));groups=[]
for w in ['holiday','large-eleven']:
 for p in [1,4]:
  stats={}
  for key in ['ticks','cpu_s','queries_per_tick','pending_per_1k_ticks','pending_pct','pending_ticks_pct','pending_queries','cpu_us_tick','mean_us','p99_us','p999_us','adjacent_us','window8_p99_us','over1ms_pct','over4ms_pct','scheduled_jobs']:
   x,y=[np.array([r[key] for r in rows if r['workload']==w and r['participants']==p and r['policy']==m]) for m in ['baseline','proactive']]
   den=x[indices].mean(axis=1);num=y[indices].mean(axis=1)
   boot=100*(num[den>0]/den[den>0]-1) if x.mean() else np.zeros(10000)
   stats[key]={'baseline':x.mean(),'proactive':y.mean(),'change_pct':100*(y.mean()/x.mean()-1) if x.mean() else None,'paired_bootstrap_95_pct':np.quantile(boot,[.025,.975]).tolist(),'paired_bootstrap_95_absolute':np.quantile(num-den,[.025,.975]).tolist()}
  groups.append(dict(workload=w,participants=p,seeds=32,metrics=stats))
(root/'summary.json').write_text(json.dumps(groups,indent=2));print(json.dumps(groups,indent=2))
