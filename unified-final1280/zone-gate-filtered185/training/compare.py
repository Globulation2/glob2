"""Paired scoring only; the single filtered fit is produced by derive-and-fit.py."""
from pathlib import Path
import json
import hashlib
import sys
import os
import collections

root = Path(__file__).resolve().parent
sys.path.insert(0,str(root/'analysis-source'))
from tools import adaptive_gradient_collection as collection, adaptive_gradient_dataset as data
from tools import adaptive_gradient_model as models, resource_gradient_model as integer
import numpy as np

sha = lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
derived=json.loads((root/'DERIVATION.json').read_text())
rows_all=collection.load([entry['path'] for entry in derived['manifests']])
rows=data.eligible(rows_all)
assignment=json.loads((root/'map-splits.json').read_text())['assignments']
held=[row for row in rows if assignment[row['group']]==-1]
old=json.loads((root/'unfiltered-deployment.json').read_text())
new=json.loads((root/'filtered-family-research.json').read_text())
integer.validate_heads(new,research=True,expected_threshold=24000)
models.validate_family_experts(new)
old_weight=data.balanced_weights(held)


def predictions(model,population):
    combined,experts=models.inference_views(model)
    return np.array([integer.predict(experts.get(row['family_id'],combined),[row[key] for key in model['features']])/1000 for row in population])


def calibration(population,model):
    prediction=predictions(model,population)
    benefit=np.array([row['defer_extra']-row['refresh_extra'] for row in population])
    weight=data.balanced_weights(population)
    order=np.argsort(prediction,kind='stable')
    bins=[]
    cumulative=np.cumsum(weight[order])/weight.sum()
    for index in range(10):
        selected=order[(cumulative>index/10)&(cumulative<=(index+1)/10)]
        if not len(selected):continue
        mass=weight[selected].sum()
        bins.append(dict(rows=len(selected),weight=float(mass),prediction=float(np.dot(weight[selected],prediction[selected])/mass),benefit=float(np.dot(weight[selected],benefit[selected])/mass)))
    return dict(deciles=bins,weighted_absolute_decile_error=sum(abs(item['prediction']-item['benefit'])*item['weight'] for item in bins)/sum(item['weight'] for item in bins))


def selected_outcomes(population,model):
    score=predictions(model,population)
    output={}
    for family in data.FAMILIES:
        indices=[i for i,row in enumerate(population) if row['family']==family]
        if not indices:
            output[family]=dict(rows=0,measured=False)
            continue
        weight=data.balanced_weights(population)
        result={}
        for selected in [False,True]:
            sub=[i for i in indices if bool(score[i]>=24)==selected]
            entry=dict(rows=len(sub),unweighted_queries=sum(population[i]['queries'] for i in sub))
            for denominator,fields in [('queries',['defer_stalls','refresh_stalls','defer_false_unreachable','refresh_false_unreachable','defer_missed_moves','refresh_missed_moves']),('planning_queries',['defer_planning_false_negative','refresh_planning_false_negative','defer_planning_false_positive','refresh_planning_false_positive'])]:
                den=sum(weight[i]*population[i].get(denominator,0) for i in sub)
                entry[denominator+'_weighted']=float(den)
                for field in fields:entry[field]=dict(events=sum(population[i].get(field,0) for i in sub),weighted_rate=float(sum(weight[i]*population[i].get(field,0) for i in sub)/den) if den else None)
            result['selected' if selected else 'deferred']=entry
        output[family]=result
    return output


old_cv=json.loads(Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/final1280-family-experts/family-experts-research.json').read_text())['cross_validation']
cross_validation=[]
for fold in range(3):
    validation=[row for row in rows if assignment[row['group']]==fold]
    source=old_cv[fold]['models']
    metric=integer.metrics(validation,old,weighting=data.balanced_weights,prediction=lambda row:integer.predict(source[row['family']],[row[key] for key in old['features']]))
    cross_validation.append(dict(fold=fold,rows=len(validation),unfiltered_fit_on_retained_rows=metric,filtered_fit_on_same_rows=new['provenance']['cross_validation'][fold]['overall']))
report=dict(status='PASS-PAIRED-FILTERED-POPULATION',threshold_milli=24000,scope='Offline threshold decisions apply neither minimum spacing nor demanded cap; production24/min32/cap1024 is separate.',same_heldout_rows_and_weights=True,rows=len(rows),heldout_rows=len(held),heldout_maps=len({row['group'] for row in held}),same_registry_sha256=sha(root/'map-splits.json'),original_eligible_rows=287045,excluded_rows=49925,retained_guard_rows=sum(row['family']=='guard' for row in rows),filter_id='eligible-guard-exact-nonpainting-ai-v1',unfiltered_model_sha256=sha(root/'unfiltered-deployment.json'),filtered_model_sha256=sha(root/'filtered-family-research.json'),comparison=dict(unfiltered=models.model_metrics(held,old),filtered=models.model_metrics(held,new)),calibration=dict(unfiltered=calibration(held,old),filtered=calibration(held,new)),slices=dict(unfiltered=models.slices(held,old),filtered=new['provenance']['holdout_slices']),cross_validation=cross_validation,separate_outcomes=dict(unfiltered_selection=selected_outcomes(held,old),filtered_selection=selected_outcomes(held,new),all_retained_heldout=models.outcome_metrics([row for row in rows_all if assignment[row['group']]==-1])),excluded_from_fitting=dict(unknown_capture=sum(not row['capture_known'] for row in rows_all),planning_only=sum(not row['movement_label_available'] for row in rows_all)),class_support={key:value['training_support'] for key,value in new['family_experts'].items()},limitations=derived['limitations']+['Selected future episode outcomes overlap and are not unique whole-game live rates.','No new-game quality evidence yet; a zone gate changes future demand and trajectories.','Old full-population metrics are not directly comparable to this retained-population paired analysis.'])
(root/'PAIRED-REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(status=report['status'],comparison=report['comparison'],cv=[dict(old=row['unfiltered_fit_on_retained_rows']['decision_regret'],new=row['filtered_fit_on_same_rows']['decision_regret']) for row in cross_validation],calibration={name:value['weighted_absolute_decile_error'] for name,value in report['calibration'].items()}),indent=2))
