#!/usr/bin/env python3
"""Fit four coherent swarm coefficients; keep entire maps and seeds out of fitting.
Imitation targets resource-rich states only. Sparse-state imitation error is a
reported diagnostic, not an optimization objective or evidence of bad policy.
"""
import argparse,gzip,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
HELD_MAPS=['G2','Isles','SmallForTwo']
design={'held_out_maps':HELD_MAPS,'held_out_seed_rounds':[5,6],'training_rounds':[1,2,3,4],'horizon':[1000,30000],'sample_ticks':1000,'rich_state_definition':'reachable fertility-weighted food capacity / workers >= 0.5','loss':'mean per-map mean(((predicted workers-assigned workers)/6)^2 + (predicted count-built or building swarms)^2); rich training states only','sparse_policy':'resource monotonicity required; imitation loss reported but not optimized; validate starvation, growth, survival and outcomes separately','grid':{'scale_percent':list(range(200,601,25)),'food_per_worker_percent':[25,50,100,200,400],'pressure_sensitivity':[0,1,2,3,4,6,8,12],'workers_per_swarm':[3,4,5,6,7,8]}}
(a.output/'fit-design.json').write_text(json.dumps(design,indent=2)+'\n')
m=json.loads((a.reference/'manifest.json').read_text())
files=list((a.reference/'match-telemetry').glob('*.json.gz'))
if len(files)!=m['matches']:raise SystemExit(f'Reference incomplete: {len(files)}/{m["matches"]}; design saved, no fit performed')
rows=[];names=sorted(set(x['map'] for x in m['schedule']))
for file in sorted(files):
 game=json.load(gzip.open(file,'rt'));mi=names.index(game['map'])
 if game['status']!='completed':raise ValueError('Failed reference process: '+str(file))
 for e in game['observer']:
  tick=int(e['tick']);w=int(e['workers']);pop=int(e['population']);food=int(e.get('local_food_capacity',-1))
  if not int(e['alive']) or w<=0 or pop<=0 or not 1000<=tick<=30000 or tick%1000 or food<0:continue
  rows.append([mi,game['round'],tick,w,pop,max(int(e['critical_food']),int(e['unserved_food'])),food,int(e['swarm_assigned']),int(e['swarms'])])
x=np.array(rows,dtype=float);rich=x[:,6]/x[:,3]>=.5;heldmap=np.isin(x[:,0],[names.index(n) for n in HELD_MAPS]);heldseed=x[:,1]>=5;train=rich&~heldmap&~heldseed
if len(set(x[train,0]))<3:raise ValueError('Too few rich training maps')
def predict(z,params):
 scale,q,s,k=params
 baseline=np.minimum(z[:,3]*1000,np.floor(np.floor(np.sqrt(z[:,3]*1000000))*scale/100))
 supply=z[:,6]*100000;den=supply+q*baseline
 funded=np.floor(np.divide(baseline*supply,den,out=np.zeros(len(z)),where=den>0))
 funded=np.floor(funded*z[:,4]/(z[:,4]+s*np.minimum(z[:,4],z[:,5])))
 b=np.floor((funded+500)/1000);n=np.maximum(1,np.ceil(b/k));return b,n
z=x[train];weights=np.zeros(len(z))
for mi in set(z[:,0]):weights[z[:,0]==mi]=1/np.sum(z[:,0]==mi)
weights/=weights.sum()
best=None;grid=design['grid']
for scale in grid['scale_percent']:
 for q in grid['food_per_worker_percent']:
  for s in grid['pressure_sensitivity']:
   for k in grid['workers_per_swarm']:
    pars=(scale,q,s,k);b,n=predict(z,pars);loss=float(np.sum(weights*(((b-z[:,7])/6)**2+(n-z[:,8])**2)))
    if best is None or loss<best[0]:best=(loss,pars)
loss,pars=best
keys=['swarm_labor_scale_percent','swarm_food_per_worker_percent','swarm_pressure_sensitivity','swarm_workers_per_building']
result={'design':design,'maps':names,'reference_games':len(files),'parameters':dict(zip(keys,pars)),'training_loss':loss,'partitions':{},'per_map':{}}
def metrics(mask):
 z=x[mask]
 if not len(z):return {'samples':0}
 b,n=predict(z,pars)
 return {'samples':len(z),'staffing_mae':float(np.mean(abs(b-z[:,7]))),'count_mae':float(np.mean(abs(n-z[:,8]))),'predicted_workers':float(np.mean(b)),'nicowar_assigned':float(np.mean(z[:,7])),'predicted_swarms':float(np.mean(n)),'nicowar_swarms':float(np.mean(z[:,8]))}
for name,mask in [('train_rich',train),('held_seed_rich',rich&~heldmap&heldseed),('held_map_rich',rich&heldmap),('sparse_all',~rich),('held_map_sparse',heldmap&~rich)]:result['partitions'][name]=metrics(mask)
for i,name in enumerate(names):
 mask=x[:,0]==i;result['per_map'][name]={'rich':metrics(mask&rich),'sparse':metrics(mask&~rich),'food_per_worker_median':float(np.median(x[mask,6]/x[mask,3]))}
(a.output/'multimap-fit.json').write_text(json.dumps(result,indent=2)+'\n');np.savez_compressed(a.output/'multimap-fit-data.npz',rows=x,map_names=np.array(names));print(json.dumps(result,indent=2))
