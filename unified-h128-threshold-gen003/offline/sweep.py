import os,pathlib,json,hashlib,sys,collections,datetime,resource,time
B=pathlib.Path(__file__).resolve().parent;D=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-002/cumulative-512-research')
os.nice(19);os.sched_setaffinity(0,{0});resource.setrlimit(resource.RLIMIT_CPU,(300,310))
for k in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMEXPR_NUM_THREADS']:os.environ[k]='1'
sys.path.insert(0,str(D/'analysis-tool-source'))
from tools import adaptive_gradient_spatial as spatial,adaptive_gradient_dataset as base,resource_gradient_model as integer
import numpy as np
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def write(name,v):
 p=B/name;t=p.with_suffix('.tmp');t.write_text(json.dumps(v,indent=2)+'\n');t.replace(p)
start=time.process_time();plan=json.loads((B/'PLAN.json').read_text());assert not (B/'predictions.npz').exists(),'Refuse duplicate prediction generation'
inputs=json.loads((D/'terminal-inputs.json').read_text());assert sha(D/'terminal-inputs.json')==plan['source'].split()[-1]
for name,h in inputs['files'].items():assert sha(D/name)==h,name
model=json.loads((B/'gen003.json').read_text());assert sha(B/'gen003.json')==plan['model_sha256'];assert model['threshold']==128000 and model['contract']==base.CONTRACT
research=json.loads((D/'generic-research.json').read_text());assert all(model[h]==research[h] for h in integer.HEADS);assert model['features']==research['features'] and model['transforms']==research['transforms']
rows=base.eligible(spatial.load([D/p for p in inputs['manifests']]));assert len(rows)==122901
assign=json.loads((D/'map-splits.json').read_text())['assignments'];groups=np.array([r['group'] for r in rows]);dev=np.array([assign[r['group']]!=-1 for r in rows]);assert len(set(groups[dev]))==392 and len(set(groups[~dev]))==120
y=np.array([r['defer_extra']-r['refresh_extra'] for r in rows],dtype=np.float64)
pred=np.array([integer.predict(model,[r[k] for k in model['features']]) for r in rows],dtype=np.int64)
np.savez_compressed(B/'predictions.npz',prediction_milli=pred,actual_benefit=y,development=dev,groups=groups)
write('PREDICTIONS.json',dict(model_sha256=sha(B/'gen003.json'),input_sha256=sha(D/'terminal-inputs.json'),row_count=len(rows),predictions_sha256=sha(B/'predictions.npz'),generation_count=1,minimum_age=min(r['age'] for r in rows),score_units='milli direction-cost units',cpu_seconds=time.process_time()-start))
thresholds=[*plan['thresholds_direction_units'],None];takes={str(t) if t is not None else 'never':pred>=1000*t if t is not None else np.zeros(len(rows),dtype=bool) for t in thresholds}
outcome_keys=[k[6:] for k in rows[0] if k.startswith('defer_') and 'refresh_'+k[6:] in rows[0] and k!='defer_extra']
outcomes={k:(np.array([r['defer_'+k] for r in rows],dtype=np.float64),np.array([r['refresh_'+k] for r in rows],dtype=np.float64)) for k in outcome_keys}
queries=np.array([r['queries'] for r in rows],dtype=np.float64);planning=np.array([r['planning_queries'] for r in rows],dtype=np.float64)
def evaluate(indices):
 pop=[rows[i] for i in indices];w=base.balanced_weights(pop);yy=y[indices];wm=lambda a:float(np.average(a,weights=w));positive=np.maximum(yy,0);positive_total=wm(positive);result={}
 for name,t in takes.items():
  take=t[indices];r=dict(weighted_net_direction_benefit=wm(yy*take),weighted_harm_selected=wm(np.maximum(-yy,0)*take),positive_benefit_captured_fraction=wm(positive*take)/positive_total if positive_total else None,weighted_selection_pct=wm(take)*100,raw_selected=int(take.sum()),selected_actual_mean=float(np.average(yy[take],weights=w[take])) if take.any() else None,fixed128_cost_regret=wm(np.maximum(yy-128,0)*(~take)+np.maximum(128-yy,0)*take),outcomes={})
  for k,(defer,refresh) in outcomes.items():
   baseline=defer[indices];effect=(refresh[indices]-baseline)*take;den=planning[indices] if 'planning' in k else queries[indices];denmean=wm(den)
   r['outcomes'][k]=dict(weighted_event_delta_vs_defer=wm(effect),weighted_policy_events=wm(baseline+effect),weighted_query_denominator=denmean,policy_events_per_query=wm(baseline+effect)/denmean if denmean else None,event_delta_per_query=wm(effect)/denmean if denmean else None)
  result[name]=r
 return dict(rows=len(indices),maps=len(set(groups[indices])),thresholds=result)
dev_indices=np.flatnonzero(dev);development=evaluate(dev_indices);ranked=sorted(plan['thresholds_direction_units'],key=lambda t:(development['thresholds'][str(t)]['weighted_net_direction_benefit'],t),reverse=True);best=ranked[0];bestvalue=development['thresholds'][str(best)]['weighted_net_direction_benefit'];within=[t for t in ranked if bestvalue>0 and development['thresholds'][str(t)]['weighted_net_direction_benefit']>=.9*bestvalue];second=max(within) if within else ranked[1]
if second==best:second=ranked[1]
shortlist=list(dict.fromkeys([best,second,128]));write('DEV-SHORTLIST.json',dict(plan_sha256=sha(B/'PLAN.json'),rule=plan['dev_shortlist_rule'],shortlist_units=shortlist,best_units=best,development=development,holdout_opened=False,limitation=plan['selection_limit']))
# Freeze development-only choices before inspecting any heldout metrics.
held_indices=np.flatnonzero(~dev);holdout=evaluate(held_indices)
def group(key):
 buckets=collections.defaultdict(list)
 for i in held_indices:
  r=rows[i];value=('0-31' if r['age']<32 else '32-127' if r['age']<128 else '128-511' if r['age']<512 else '512-4095' if r['age']<4096 else '4096+') if key=='age' else ('0-7' if r['q128']<8 else '8-31' if r['q128']<32 else '32+') if key=='demand' else str(r[key]);buckets[value].append(i)
 return {k:evaluate(np.asarray(v)) for k,v in sorted(buckets.items())}
# Paired resampling of complete map clusters with fixed population hierarchy weights.
w=base.balanced_weights([rows[i] for i in held_indices]);ug,gi=np.unique(groups[held_indices],return_inverse=True);den=np.bincount(gi,weights=w);rng=np.random.default_rng(plan['uncertainty']['seed']);multiplicities=rng.multinomial(len(ug),np.full(len(ug),1/len(ug)),size=2000);bootden=multiplicities@den
columns={}
for t in [*shortlist,None]:
 name=str(t) if t is not None else 'never';take=takes[name][held_indices];columns[name]=np.bincount(gi,weights=w*y[held_indices]*take,minlength=len(ug))
uncertainty={}
for name,values in columns.items():
 for reference in ['never','128']:
  delta=multiplicities@(values-columns[reference])/bootden;point=float((values-columns[reference]).sum()/den.sum());uncertainty[name+'_minus_'+reference]=dict(net_direction_benefit_delta=point,percentile95=[float(v) for v in np.quantile(delta,[.025,.975])])
write('RESULTS.json',dict(plan_sha256=sha(B/'PLAN.json'),prediction_sha256=sha(B/'predictions.npz'),shortlist_sha256=sha(B/'DEV-SHORTLIST.json'),development=development,holdout=holdout,heldout_slices={k:group(k) for k in plan['subgroups']},paired_map_uncertainty=uncertainty,selection_note='DEV only; all heldout thresholds descriptive, no selection on heldout',cpu_seconds=time.process_time()-start,slot_counts=inputs['slot_counts'],outcome_keys=outcome_keys))
write('STATUS.json',dict(status='COMPLETE',pid=os.getpid(),cpu_seconds=time.process_time()-start,results_sha256=sha(B/'RESULTS.json'),fit_performed=False))
