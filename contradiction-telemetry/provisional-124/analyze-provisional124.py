#!/usr/bin/env python3
"""Summarize completed PROVISIONAL124 comparison; evaluate frozen incumbent, never fit."""
import collections,datetime,hashlib,json,os,statistics,sys
from pathlib import Path
BASE=Path('/home/bradley/glob2/artifacts/gradient-contradictions-20261018/postprocess-f65')
SOURCE=BASE/'source';OUT=BASE/'PROVISIONAL-fit-124-20261018';REPORT=OUT/'comparison.json'
sys.path.insert(0,str(SOURCE))
from tools import resource_gradient_model as m
from tools.resource_gradient_contradictions import prepare
from tools.resource_gradient_dataset import CONTRADICTION_COUNTERS,file_hash
def save(name,value):m.save(OUT/name,value)
def short(x):
 keys=['episodes','states','mae','decision_regret','decision_regret_p95','decision_regret_p99','selected_pct','selected_actual_benefit','positive_benefit_captured_fraction','missed_positive_benefit','negative_benefit_selected','false_refresh_pct','missed_threshold_decision_pct']
 return {k:x.get(k) for k in keys}
provenance=json.loads((OUT/'provenance.json').read_text())
assert file_hash(BASE/'map-split-registry.json')==provenance['registry_sha256']
assert file_hash(provenance['manifest'])==provenance['manifest_sha256']
registry_before=file_hash(BASE/'map-split-registry.json');source_before=file_hash(BASE/'source-identity.json');model_before=file_hash(SOURCE/'tools/models/resource-gradient.json')
report=json.loads(REPORT.read_text());assert report['research_only'] is True
all_rows=m.dataset([provenance['manifest']]);rows,excluded=prepare(all_rows)
row_hash=hashlib.sha256()
for row in rows:
 row_hash.update(json.dumps([row.get('collection'),row.get('tick'),row.get('key'),row['defer_extra'],row['refresh_extra'],*[row[k] for k in (*m.FEATURES,*CONTRADICTION_COUNTERS)]],separators=(',',':')).encode()+b'\n')
assert row_hash.hexdigest()==report['identical_rows_sha256']
held=set(report['heldout_states']);heldrows=[r for r in rows if r['group'] in held]
incumbent=json.loads((SOURCE/'tools/models/resource-gradient.json').read_text());inc=m.metrics(heldrows,incumbent)
save('incumbent-same-heldout.json',dict(no_fit=True,model_sha256=model_before,rows=len(heldrows),states=len(held),metrics=inc,
 identical_eligible_rows_sha256=report['identical_rows_sha256'],manifest_sha256=provenance['manifest_sha256'],
 note='Existing incumbent coefficients evaluated once on exactly the same eligible frozenheldout rows; no alternative fit or objective/split change.'))
base=report['variants']['baseline7'];main=['baseline7','counts','fractions','counts_fractions','fractions_log']
ranking=[]
for name,value in report['variants'].items():
 folds=value['cross_validation'];delta=value['cross_validation_delta_vs_baseline7']
 ranking.append(dict(name=name,features=len(value['features']),holdout=short(value['holdout']),
  mean_cv_regret=statistics.mean(f['decision_regret'] for f in folds),fold_cv_regret=[f['decision_regret'] for f in folds],
  fold_cv_regret_delta=[f['decision_regret'] for f in delta],cv_improved_folds=sum(f['decision_regret']<0 for f in delta),
  holdout_regret_delta=value['holdout']['decision_regret']-base['holdout']['decision_regret'],
  holdout_regret_change_pct=(value['holdout']['decision_regret']/base['holdout']['decision_regret']-1)*100 if base['holdout']['decision_regret'] else None))
cv_rank=sorted(ranking,key=lambda r:r['mean_cv_regret']);held_rank=sorted(ranking,key=lambda r:r['holdout']['decision_regret'])
main_cv=sorted([r for r in ranking if r['name'] in main],key=lambda r:r['mean_cv_regret']);main_held=sorted([r for r in ranking if r['name'] in main],key=lambda r:r['holdout']['decision_regret'])
subgroups={}
for name in main[1:]:
 value=report['variants'][name];subgroups[name]={}
 for category in ['format','generator','dimensions','age_band','demand_band']:
  slices=value['holdout_slices'].get(category,{});baseline=base['holdout_slices'].get(category,{})
  subgroups[name][category]=[]
  for label,metrics in slices.items():
   b=baseline[label]
   subgroups[name][category].append(dict(label=label,states=metrics['states'],episodes=metrics['episodes'],
    baseline_regret=b['decision_regret'],regret=metrics['decision_regret'],regret_delta=metrics['decision_regret']-b['decision_regret'],
    baseline_mae=b['mae'],mae=metrics['mae'],baseline_selected_pct=b['selected_pct'],selected_pct=metrics['selected_pct']))
def support(selected):
 return dict(episodes=len(selected),maps=len({r['group'] for r in selected}),
  age128_q32=sum(r['age']>=128 and r['q128']>=32 for r in selected),
  age512_q32=sum(r['age']>=512 and r['q128']>=32 for r in selected),
  age1024_q128=sum(r['age']>=1024 and r['q128']>=128 for r in selected),
  age4096_q32=sum(r['age']>=4096 and r['q128']>=32 for r in selected))
experiment=json.loads((BASE.parent/'round-001-readonly/experiment.json').read_text())
missing=[]
for job in experiment['jobs']:
 if job['id'] in provenance['missing_games']:
  spec=job['labels']['resource_gradient']['actual_spec'];missing.append(dict(group=job['id'],format=spec['format'],generator=spec['generator'],width=spec['width'],height=spec['height']))
summary=dict(provisional=True,accepted_games=124,full128_refit_required=True,research_only=True,missing_games=missing,
 comparison_sha256=file_hash(REPORT),registry_sha256=registry_before,model_sha256=model_before,
 identical_rows_sha256=report['identical_rows_sha256'],eligible_rows=report['rows'],excluded=report['excluded'],
 eligibility_by_format=report['eligibility_by_format'],split_coverage=report['split_coverage'],
 incumbent_same_heldout=short(inc),main_variants={r['name']:r for r in ranking if r['name'] in main},
 cv_ranking=cv_rank,holdout_ranking=held_rank,main_cv_ranking=[r['name'] for r in main_cv],main_holdout_ranking=[r['name'] for r in main_held],
 incremental_features=report['incremental_features'],leave_event_out=report['leave_event_out'],
 subgroup_deltas=subgroups,separate_outcomes=report['separate_outcomes'],separate_outcomes_holdout=report['separate_outcomes_holdout'],
 separate_outcomes_slices=report['separate_outcomes_slices'],live_event_totals=report['live_event_totals'],
 joint_support=dict(all_eligible=support(rows),heldout=support(heldrows),
  heldout_by_format={f:support([r for r in heldrows if r['format']==f]) for f in sorted({r['format'] for r in heldrows})}),
 warnings=['Four slow512x512games absent:3x16ffa+1x8ffa; provisional completion subset biases late/large trajectories.',
 'Refittedbaseline7 is not incumbentproduction coefficients. Integer threshold32 offline decisionregret is not CPU/quality engine outcome.',
 'Sameknowncapture movement rows, map splits and role/cohort/map weights in every variant. Historical126holdouts remain excluded.',
 'CV rankings are development evidence; inspecting heldout across17variants is exploratory and not an independent confirmation of a chosenwinner.',
 'Counter correlations/ablations are incremental prediction evidence, not proof congestion causes stale-field harm.',
 'Futurestall/planning labels are separate from directioncost target; aggregate labels are shared byallmodels and are not measured policy improvements.',
 'Knowncapture filtering loses some maps; highdemand veryold states remain scarce. No coefficient promotion or prospective benefit claim.'],
 completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
assert file_hash(BASE/'map-split-registry.json')==registry_before
assert file_hash(SOURCE/'tools/models/resource-gradient.json')==model_before
assert file_hash(BASE/'source-identity.json')==source_before
save('provisional-summary.json',summary)
print(json.dumps(dict(incumbent=short(inc),main=summary['main_variants'],cv_best=main_cv[0]['name'],heldout_best=main_held[0]['name'],joint_support=summary['joint_support']),indent=2))
