#!/usr/bin/env python3
"""Read-only final-score sensitivities, coherent event changes and baseline7 permutation."""
import sys,json,collections,hashlib,statistics,os
from pathlib import Path
import numpy as np
BASE=Path('/home/bradley/glob2/artifacts/gradient-contradictions-20261018/postprocess-f65')
OUT=BASE/'PROVISIONAL-fit-124-20261018';sys.path.insert(0,str(BASE/'source'))
from tools import resource_gradient_model as m
from tools.resource_gradient_contradictions import prepare,FRACTIONS
from tools.resource_gradient_dataset import CONTRADICTION_COUNTERS,file_hash
prov=json.loads((OUT/'provenance.json').read_text());report=json.loads((OUT/'comparison.json').read_text())
rows,_=prepare(m.dataset([prov['manifest']]));held=set(report['heldout_states']);rows=[r for r in rows if r['group'] in held]
w=m.weights(rows);w=w/w.sum();y=np.array([r['defer_extra']-r['refresh_extra'] for r in rows])
registry_before=file_hash(BASE/'map-split-registry.json');model_before=file_hash(BASE/'source/tools/models/resource-gradient.json')
def transformed(model,raw):
 return np.column_stack([np.fromiter((m.log_integer(int(v)) for v in raw[:,i]),dtype=np.int64) if t=='log1p_q12' else np.clip(raw[:,i],0,m.LIMIT) for i,t in enumerate(model.get('transforms',['log1p_q12']*len(model['features'])))])
def trunc(n,d):return np.where(n>=0,n//d,-((-n)//d))
def predict_x(model,x):
 heads={}
 for name in m.HEADS:
  h=model[name];heads[name]=np.clip(trunc(x@np.array(h['coef'],dtype=np.int64)+h['bias'],1<<h['shift']),-10**9,10**9)
 def sig(z):
  n=np.clip(z,-12000,12000)+12000;i=np.minimum(n//50,479);table=np.array(m.SIG,dtype=np.int64)
  return table[i]+((table[i+1]-table[i])*(n-i*50))//50
 value=trunc(sig(heads['positive_probability'])*np.maximum(heads['positive_mean'],0)-sig(heads['negative_probability'])*np.maximum(heads['negative_mean'],0),65536)
 return np.clip(value,-10**9,10**9)
def regret(pred):
 take=pred>=32000;return np.maximum(y-32,0)*(~take)+np.maximum(32-y,0)*take
def means(v,weights=w):
 weights=weights/weights.sum();return float(np.sum(v*weights))
results={};coefficient_tables={};validation={}
for name in ['baseline7','counts_fractions']:
 model=report['variants'][name]['model'];features=model['features'];raw=np.array([[r[f] for f in features] for r in rows],dtype=np.int64);x=transformed(model,raw)
 pred=predict_x(model,x)
 # Validate every original heldout prediction against permanent scalar integer implementation.
 scalar=np.fromiter((m.predict(model,v.tolist()) for v in raw),dtype=np.int64)
 assert np.array_equal(pred,scalar),name
 assert abs(means(regret(pred))-report['variants'][name]['holdout']['decision_regret'])<1e-9
 validation[name]=dict(rows=len(rows),all_scalar_predictions_equal=True,decision_regret=means(regret(pred)))
 table=[];coefficients=[]
 for i,f in enumerate(features):
  mean=means(x[:,i]);sd=float(np.sqrt(means((x[:,i]-mean)**2)));step=max(1,int(round(sd*.01)))
  hi=x.copy();lo=x.copy();upper=4096 if f in FRACTIONS else m.log_integer(m.LIMIT)
  hi[:,i]=np.minimum(upper,x[:,i]+step);lo[:,i]=np.maximum(0,x[:,i]-step)
  distance=hi[:,i]-lo[:,i];plus=predict_x(model,hi);minus=predict_x(model,lo)
  effect=np.divide((plus-minus)/1000*sd,distance,out=np.zeros(len(rows)),where=distance>0)
  diff=plus-minus
  table.append(dict(feature=f,transform=model.get('transforms',['log1p_q12']*len(features))[i],
   transformed_mean_q12=mean,transformed_sd_q12=sd,human_sd=sd/4096,step_q12=step,
   mean_signed_response_per_sd=means(effect),mean_absolute_response_per_sd=means(np.abs(effect)),
   weighted_positive_response_fraction=means(diff>0),weighted_negative_response_fraction=means(diff<0),
   weighted_zero_quantized_response_fraction=means(diff==0)))
  coefficients.append(dict(feature=f,human_input_unit='fraction0..1' if f in FRACTIONS else 'log2(1+raw)',
   **{h:float(model[h]['coef'][i])*4096/(1<<model[h]['shift'])/1000 for h in m.HEADS}))
 results[name]=sorted(table,key=lambda z:-z['mean_absolute_response_per_sd']);coefficient_tables[name]=dict(features=coefficients,
  biases={h:float(model[h]['bias'])/(1<<model[h]['shift'])/1000 for h in m.HEADS},
  note='Probabilityhead values are logits; meanhead values are movementbenefit units. Final score=sigmoid(poslogit)*max(posmean,0)-sigmoid(neglogit)*max(negmean,0), using permanent integer lookup/truncation. Coefficient sign is a head-specific partial sign, not overallscore polarity.')
 if name=='counts_fractions':
  coherent=[]
  for event,frac in zip(CONTRADICTION_COUNTERS[:-1],FRACTIONS):
   ei=features.index(event);qi=features.index(CONTRADICTION_COUNTERS[-1]);fi=features.index(frac)
   eligible=(raw[:,qi]>0)&(raw[:,ei]<raw[:,qi]);modified=raw.copy();modified[eligible,ei]+=1
   modified[:,fi]=modified[:,ei]*4096//np.maximum(1,modified[:,qi])
   change=(predict_x(model,transformed(model,modified))-pred)/1000
   ew=w*eligible
   coherent.append(dict(intervention=event+'+1,queriesfixed,matchingfractionrecomputed',eligible_rows=int(eligible.sum()),
    eligible_weight=float(ew.sum()),mean_change=means(change,ew),mean_absolute_change=means(np.abs(change),ew),
    positive_fraction=means(change>0,ew),negative_fraction=means(change<0,ew),zero_fraction=means(change==0,ew)))
  qi=features.index(CONTRADICTION_COUNTERS[-1]);modified=raw.copy();modified[:,qi]+=1
  for event,frac in zip(CONTRADICTION_COUNTERS[:-1],FRACTIONS):modified[:,features.index(frac)]=modified[:,features.index(event)]*4096//modified[:,qi]
  change=(predict_x(model,transformed(model,modified))-pred)/1000
  coherent.append(dict(intervention='onecleanquery:queries+1,eventcountsunchanged,allfractionsrecomputed',eligible_rows=len(rows),
   mean_change=means(change),mean_absolute_change=means(np.abs(change)),positive_fraction=means(change>0),negative_fraction=means(change<0),zero_fraction=means(change==0)))
  coherent_results=coherent
 if name=='baseline7':
  groups=collections.defaultdict(list)
  for index,row in enumerate(rows):groups[row['group']].append(index)
  baseline_regret=means(regret(pred));baseline_mae=means(np.abs(pred/1000-y));permutations=[]
  for i,f in enumerate(features):
   reps=[]
   for repeat in range(5):
    rng=np.random.default_rng(181042+repeat*100+i);modified=x.copy()
    for group,indices in sorted(groups.items()):
     indices=np.array(indices);modified[indices,i]=x[rng.permutation(indices),i]
    altered=predict_x(model,modified);reps.append(dict(regret_delta=means(regret(altered))-baseline_regret,mae_delta=means(np.abs(altered/1000-y))-baseline_mae))
   permutations.append(dict(feature=f,mean_regret_increase=statistics.mean(t['regret_delta'] for t in reps),regret_delta_range=[min(t['regret_delta'] for t in reps),max(t['regret_delta'] for t in reps)],mean_mae_increase=statistics.mean(t['mae_delta'] for t in reps),replicates=reps))
  permutation_results=sorted(permutations,key=lambda t:-t['mean_regret_increase'])
output=dict(provisional=True,no_refit=True,heldout_rows=len(rows),heldout_maps=len(held),comparison_sha256=file_hash(OUT/'comparison.json'),registry_sha256=registry_before,
 validation=validation,conditional_transformed_sensitivities=results,coherent_counter_sensitivities=coherent_results,
 baseline7_withinmap_permutation=permutation_results,head_coefficients_human_units=coefficient_tables,
 conditional_definition='Central transformedfeature +/-1% of weightedheldoutSD, minimum1Q12unit, clippedvalidtransformbounds. Effect=(scoreplus-scoreminus)/(actualtransformedstep)*SD. Other transformedinputs fixed; rawcount andfractionpartial changes can be inconsistent, so use coherentevent tests for pairedrepresentation.',
 coherent_definition='For each event,replace one recorded non-event query with event(+1count,fixedquerycount),recompute matchingfraction; onlyrowsQ>0,E<Q. Cleanquery raisesdenominator,recomputesallfractions. Othersevenfeatures fixed; these are conditionalcounterhistory changes, not simulations or causal interventions.',
 permutation_definition='Five deterministic withinmap independentfeature permutations on refittedbaseline7, fixedmodels/labels/weights/maps; preserves eachmap marginalfeaturedistribution but breakscorrelations/timing. Predictionimportance, not leave-feature-out retraining or causation.',
 warnings=['All analyses provisional124missing4largegames.','LocalSD response and permutationregret are differentimportance definitions; neither is a causal or uniqueimportance ranking undercorrelation.',
 'Quantizedintegeroutput yields manyexactzero responses; sign fractions refer to thisfinite step, not globalmonotonicity.',
 'Sensitivityranking reflects marginalchange in predictedscore, not improved heldoutdecisionaccuracy. Leave-event-out/retrained comparisonsremain separate.',
 'No modelselection,promotion,sourceedit,C++headeremit,splitchange,orenginecomputeclaim.'])
assert file_hash(BASE/'map-split-registry.json')==registry_before
assert file_hash(BASE/'source/tools/models/resource-gradient.json')==model_before
m.save(OUT/'coefficient-directions-and-importance.json',output)
print(json.dumps(dict(validation=validation,conditional=results['counts_fractions'],coherent=coherent_results,baseline7permutation=permutation_results),indent=2))
