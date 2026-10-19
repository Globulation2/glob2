import pathlib,json,sys,os,time,collections,hashlib,resource,bisect
R=pathlib.Path(__file__).resolve().parent;D=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-002/cumulative-512-research');S=D.parent.parent.parent/'threshold-sweep-gen003'
# Artifact source/data are fixed cumulative512; yield to higher-priority cumulative768 fitting.
os.nice(19);os.sched_setaffinity(0,{0});resource.setrlimit(resource.RLIMIT_CPU,(90,100))
for k in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMEXPR_NUM_THREADS']:os.environ[k]='1'
sys.path.insert(0,str(D/'analysis-tool-source'))
from tools import adaptive_gradient_spatial as spatial,adaptive_gradient_dataset as base,resource_gradient_model as integer
import numpy as np
start=time.process_time();inputs=json.loads((D/'terminal-inputs.json').read_text());assert hashlib.sha256((D/'terminal-inputs.json').read_bytes()).hexdigest()=='f78be1b7fdabd8eecce00fb74b853afe6c909a390086edff82d7dce4d0d3fa67'
# Correct frozen score cache path without rerunning inference.
S=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/threshold-sweep-gen003');assert hashlib.sha256((S/'predictions.npz').read_bytes()).hexdigest()=='41f833dd01a2fc042066d6fb0f1d92b051559fcba2a994c69ce26c9b42655569'
cache=np.load(S/'predictions.npz');rows=base.eligible(spatial.load([D/p for p in inputs['manifests']]));assert len(rows)==len(cache['prediction_milli'])==122901
assert all(r['group']==g and r['defer_extra']-r['refresh_extra']==y for r,g,y in zip(rows,cache['groups'],cache['actual_benefit']))
training=[r for r,v in zip(rows,cache['development']) if v];test=[r for r,v in zip(rows,cache['development']) if not v];scores=cache['prediction_milli'][~cache['development']];w=base.balanced_weights(test);w=w/w.sum()
features=list(base.FEATURES);ranges={f:sorted(r[f] for r in training) for f in features};family_ranges={name:{f:sorted(r[f] for r in training if r['family']==name) for f in features} for name in ['clear','materials']}
def signature(r):return tuple(integer.log_integer(r[f]) if t=='log1p_q12' else min(integer.LIMIT,max(0,r[f])) for f,t in zip(features,base.TRANSFORMS))
counts=collections.Counter(signature(r) for r in training);fc=collections.Counter((r['family'],signature(r)) for r in training)
selected=[]
for index,(row,score,weight) in enumerate(zip(test,scores,w)):
 if score<128000:continue
 y=row['defer_extra']-row['refresh_extra'];entry={k:row.get(k) for k in ['group','format','generator','dimensions','cohort_role','cohort','collection','family','movement','team','material','key','tick','serving_capture_tick','queries','defer_extra','refresh_extra','route_changes','seed_cost_changes','captured_goal_count']};entry.update(prediction_milli=int(score),actual_benefit=y,actual_benefit_per_future_query=y/row['queries'] if row['queries'] else None,global_population_weight=float(weight),global_net_benefit_contribution=float(weight*y),global_negative_harm_contribution=float(weight*max(-y,0)),features={f:row[f] for f in features},development_support={f:dict(min=ranges[f][0],max=ranges[f][-1],percentile=100*bisect.bisect_right(ranges[f],row[f])/len(training),outside_observed_range=not ranges[f][0]<=row[f]<=ranges[f][-1],same_family_percentile=100*bisect.bisect_right(family_ranges[row['family']][f],row[f])/len(family_ranges[row['family']][f])) for f in features},exact_transformed_development_matches=counts[signature(row)],same_family_transformed_development_matches=fc[(row['family'],signature(row))]);selected.append(entry)
def aggregate(p):
 mass=sum(x['global_population_weight'] for x in p);return dict(rows=len(p),maps=len({x['group'] for x in p}),population_mass_pct=100*mass,actual_raw_sum=sum(x['actual_benefit'] for x in p),global_net_contribution=sum(x['global_net_benefit_contribution'] for x in p),global_harm_contribution=sum(x['global_negative_harm_contribution'] for x in p),selected_mean_using_global_weights=sum(x['global_net_benefit_contribution'] for x in p)/mass if mass else None,prediction_min=min((x['prediction_milli'] for x in p),default=None),prediction_max=max((x['prediction_milli'] for x in p),default=None))
byfamily={f:dict(summary=aggregate([x for x in selected if x['family']==f]),groups={g:aggregate([x for x in selected if x['family']==f and x['group']==g]) for g in sorted({x['group'] for x in selected if x['family']==f})}) for f in ['clear','materials']}
contributions={}
for threshold in [1,4,8,16,32,128]:
 contributions[str(threshold)]={}
 for f in base.FAMILIES:
  ii=[i for i,r in enumerate(test) if r['family']==f and scores[i]>=threshold*1000];contributions[str(threshold)][f]=dict(rows=len(ii),global_selection_pct=float(100*sum(w[i] for i in ii)),global_net_contribution=float(sum(w[i]*(test[i]['defer_extra']-test[i]['refresh_extra']) for i in ii)),global_harm_contribution=float(sum(w[i]*max(test[i]['refresh_extra']-test[i]['defer_extra'],0) for i in ii)))
out=dict(status='READONLY-CACHE-CONTEXT-NO-INFERENCE-NO-FIT',input_sha256=hashlib.sha256((D/'terminal-inputs.json').read_bytes()).hexdigest(),cache_sha256=hashlib.sha256((S/'predictions.npz').read_bytes()).hexdigest(),rows=selected,tail_by_family=byfamily,additive_family_contributions=contributions,cpu_seconds=time.process_time()-start,weights='Original pooled heldout hierarchy weights retained, additive contributions unlike conditional subgroup reweighting',support_note='No exact transformed-vector matches is not proof of extrapolation or insufficient predictability. Percentiles are descriptive, global and within-family; no fit or heldout selection.')
(R/'TAIL-CONTEXT.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(byfamily,indent=2));print('CPU',out['cpu_seconds'])
