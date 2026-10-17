from pathlib import Path
import csv,json,numpy as np
root=Path(__file__).parent/'runs';rows=list(csv.DictReader(open(root/'per-game.csv')));rng=np.random.default_rng(20261018);idx=rng.integers(0,32,(10000,32));out=[]
for w in ['holiday','large-eleven']:
 modeRows={}
 for m in ['baseline','proactive','heuristic']:
  arr=[]
  for seed in range(42,74):
   d=root/w/f'seed-{seed}-{m}-p4';rr=next(r for r in rows if r['workload']==w and int(r['seed'])==seed and r['policy']==m);n=int(rr['ticks']);fields=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(open(d/'fields.csv'))];pro=[f for f in fields if f['proactive']];unused=[f for f in fields if f['retired'] and not f['flags']&5];specunused=[f for f in unused if f['proactive'] and f['speculative']]
   arr.append(dict(unused_count=len(unused),unused_seed_ns_tick=sum(f['seed_cpu_ns'] for f in unused)/n,spec_unused_count=len(specunused),spec_unused_seed_ns_tick=sum(f['seed_cpu_ns'] for f in specunused)/n,pro_unused_fraction=sum(f['retired'] and not f['flags']&5 for f in pro)/len(pro) if pro else 0,warm_per_million_buildingticks=float(rr['map_warm_per_tick'])/float(rr['building_ticks_per_tick'])*1e6,warm_per_million_unitsteps=float(rr['map_warm_per_tick'])/float(rr['unit_steps_per_tick'])*1e6,all_seeds_per_million_unitsteps=float(rr['all_seeds_per_tick'])/float(rr['unit_steps_per_tick'])*1e6))
  modeRows[m]=arr
 metrics={}
 for k in modeRows['baseline'][0]:
  arrays={m:np.array([r[k] for r in values]) for m,values in modeRows.items()};b=arrays['baseline'];p=arrays['proactive'];metrics[k]={'baseline':float(b.mean()),'proactive':float(p.mean()),'heuristic':float(arrays['heuristic'].mean()),'delta_ci':np.quantile(p[idx].mean(1)-b[idx].mean(1),[.025,.975]).tolist()}
 out.append(dict(workload=w,metrics=metrics))
(root/'waste-summary.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
