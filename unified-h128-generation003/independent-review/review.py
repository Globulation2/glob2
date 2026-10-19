import pathlib,sys,json,hashlib,collections,os
D=pathlib.Path(sys.argv[1]); O=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(D/'analysis-tool-source'))
from tools import adaptive_gradient_spatial as s,adaptive_gradient_dataset as b,resource_gradient_model as m
import numpy as np
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
inputs=json.loads((D/'terminal-inputs.json').read_text())
for name,h in inputs['files'].items():
 assert sha(D/name)==h,(name,'changed sealed input')
stage=json.loads((D/'generic12-stage.json').read_text()); assert stage['returncode']==0 and stage['output_sha256']==sha(D/'generic-research.json')
assert stage['input_sha256']==sha(D/'terminal-inputs.json')
models={'gen002':json.loads((O/'gen002.json').read_text()),'refit':json.loads((D/'generic-research.json').read_text())}
assert sha(O/'gen002.json')=='0c87e6bd4b000ca763ebec1b7408133cbadc47bb51ed220d804bbddbb0b7344a'
registry=json.loads((D/'map-splits.json').read_text()); assignments=registry['assignments']
prior=json.loads((O/'prior-map-splits.json').read_text())['assignments']
assert all(assignments.get(g)==v for g,v in prior.items()),'Historical split reassigned'
allrows=s.load([D/p for p in inputs['manifests']]); rows=b.eligible(allrows)
held=set(models['refit']['provenance']['heldout_maps']); assert held=={r['group'] for r in rows if assignments[r['group']]==-1}
assert all(assignments[g]==-1 for g in models['gen002']['provenance']['heldout_maps'])
assert all(assignments[g]!=-1 for g in models['gen002']['provenance']['development_maps'])
for model in models.values():
 assert tuple(model['features'])==b.FEATURES and tuple(model['transforms'])==b.TRANSFORMS and model['threshold']==128000
 assert model.get('candidate_contract',model.get('contract'))==b.CONTRACT
 m.validate_heads(dict(model,schema=2,research_only=True),research=True,expected_threshold=128000)

def audit(pop):
 w=b.balanced_weights(pop); y=np.array([r['defer_extra']-r['refresh_extra'] for r in pop]); wm=lambda v:float(np.average(v,weights=w))
 result=dict(rows=len(pop),maps=len({r['group'] for r in pop}),actual_threshold_count=int((y>=128).sum()),actual_threshold_pct=wm(y>=128)*100,never_refresh_regret=wm(np.maximum(y-128,0)),models={})
 for name,model in models.items():
  pred=np.array([m.predict(model,[r[k] for k in model['features']]) for r in pop]); take=pred>=128000
  result['models'][name]=dict(metrics=m.metrics(pop,model,weighting=b.balanced_weights),raw_selected=int(take.sum()),prediction_min_milli=int(pred.min()),prediction_max_milli=int(pred.max()),near_threshold_96_160=int(((pred>=96000)&(pred<=160000)).sum()),calibration_bins={})
  for label,mask in [('below64',pred<64000),('64_96',(pred>=64000)&(pred<96000)),('96_128',(pred>=96000)&(pred<128000)),('128_160',(pred>=128000)&(pred<160000)),('160plus',pred>=160000)]:
   result['models'][name]['calibration_bins'][label]=dict(rows=int(mask.sum()),weighted_pct=wm(mask)*100,prediction_units=float(np.average(pred[mask]/1000,weights=w[mask])) if mask.any() else None,actual_units=float(np.average(y[mask],weights=w[mask])) if mask.any() else None)
 return result

def group(rows,key):
 groups=collections.defaultdict(list)
 for r in rows:
  value= ('0-31' if r['age']<32 else '32-127' if r['age']<128 else '128-511' if r['age']<512 else '512-4095' if r['age']<4096 else '4096+') if key=='age' else ('0-7' if r['q128']<8 else '8-31' if r['q128']<32 else '32+') if key=='demand' else str(r[key])
  groups[value].append(r)
 return {k:audit(v) for k,v in sorted(groups.items())}
test=[r for r in rows if r['group'] in held]; dev=[r for r in rows if r['group'] not in held]
bounds={}
for name,model in models.items():
 limit=[m.log_integer(m.LIMIT) if t=='log1p_q12' else m.LIMIT for t in model['transforms']]
 bounds[name]={head:abs(model[head]['bias'])+sum(abs(c)*bnd for c,bnd in zip(model[head]['coef'],limit)) for head in m.HEADS}
 assert max(bounds[name].values())<2**61
report=dict(status='READONLY-INFERENCE-REVIEW',fit_performed=False,automatic_promotion=False,inputs_sha256=sha(D/'terminal-inputs.json'),registry_sha256=sha(D/'map-splits.json'),model_sha256={name:sha(O/'gen002.json') if name=='gen002' else sha(D/'generic-research.json') for name in models},slot_counts=inputs['slot_counts'],expected_slots=inputs['expected_slots'],all_complete_rows=len(allrows),eligible_rows=len(rows),excluded_unknown_or_planning=len(allrows)-len(rows),units=dict(target='defer_extra-refresh_extra direction-cost units',prediction='milli direction-cost units',threshold_milli=128000,horizon_offsets_inclusive=[8,127]),same_rows_weights_and_holdout=True,historical_registry_assignments_verified=len(prior),integer_bounds=bounds,development=audit(dev),holdout=audit(test),holdout_slices={k:group(test,k) for k in ['family','format','cohort_role','cohort','trajectory_status','age','demand']},coverage=b.coverage(rows),cross_validation=models['refit']['provenance']['cross_validation'])
(O/'REVIEW.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['status','slot_counts','eligible_rows','development','holdout']},indent=2))
