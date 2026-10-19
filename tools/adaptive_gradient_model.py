#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit/export one all-family integer model; never pool the resource-only schema."""
import argparse,collections,copy,json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from tools import adaptive_gradient_dataset as d
from tools import resource_gradient_model as integer
from tools.resource_gradient_dataset import registered_split,balanced_weights as prior_weights
HEADS=integer.HEADS

def validate(model):
 if model.get('schema')!=4 or not d.compatible_contract(model.get('contract')) or tuple(model.get('transforms',()))!=d.selected_transforms(model.get('features',())) or type(model.get('trained'))is not bool:raise ValueError('Expected exact unified deployment contract')
 threshold=d.threshold_milli(model.get('threshold'))
 if 'deployment' in model and model['deployment'].get('threshold_milli')!=threshold:raise ValueError('Deployment threshold metadata disagrees with model')
 temporary=dict(model,schema=2,research_only=True);integer.validate_heads(temporary,research=True,expected_threshold=threshold)
 if not model['trained'] and any(model[name]['bias'] or any(model[name]['coef']) for name in HEADS):raise ValueError('Untrained bootstrap cannot contain fitted resource coefficients')

def predict(model,base,counts):
 validate(model)
 if not model['trained']:raise ValueError('No unified model has been trained; use explicit fixed-interval research bootstrap')
 values=d.prepare(base,counts)
 return integer.predict(model,values[:len(model['features'])])

def slices(rows,model):
 report={}
 for key in ('family','format','generator','dimensions','cohort','movement','age_bucket','demand_bucket','target_changed','route_changed','seed_cost_changed'):
  groups=collections.defaultdict(list)
  for r in rows:
   if key=='age_bucket':value='0-31' if r['age']<32 else '32-127' if r['age']<128 else '128-511' if r['age']<512 else '512-4095' if r['age']<4096 else '4096+'
   elif key=='demand_bucket':value='0-7' if r['q128']<8 else '8-31' if r['q128']<32 else '32+'
   elif key=='target_changed':value=bool(r['target_added'] or r['target_removed'])
   elif key=='route_changed':value=bool(r.get('route_changes',0))
   elif key=='seed_cost_changed':value=bool(r.get('seed_cost_changes',0))
   else:value=r.get(key,'unknown')
   groups[str(value)].append(r)
  report[key]={name:integer.metrics(group,model,weighting=d.balanced_weights) for name,group in sorted(groups.items())}
 return report

def age_demand_support(rows):
 return {family:dict(episodes=len(subset),maps=len({r['group'] for r in subset}),maximum_age=max((r['age'] for r in subset),default=None),high_demand_age128plus=sum(r['age']>=128 and r['q128']>=32 for r in subset),high_demand_age512plus=sum(r['age']>=512 and r['q128']>=32 for r in subset),high_demand_age4096plus=sum(r['age']>=4096 and r['q128']>=32 for r in subset)) for family in d.FAMILIES for subset in [[r for r in rows if r['family']==family]]}

def outcome_metrics(rows):
 """Separate future episode outcomes, never penalties folded into direction cost.

 Episodes overlap: these are counterfactual episode-query rates, not unique
 whole-game live stall rates. Live totals require their separate stream.
 """
 import numpy as np
 result={}
 for family in d.FAMILIES:
  subset=[r for r in rows if r['family']==family]
  if not subset:result[family]=dict(episodes=0,measured=False);continue
  w=d.balanced_weights(subset);entry=dict(episodes=len(subset),measured=True,scope='possibly-overlapping-counterfactual-episode-queries')
  for denominator,columns in [('queries',list(d.MOVEMENT_REACHABILITY)+['defer_stalls','refresh_stalls','defer_invalid_goals','refresh_invalid_goals','defer_missed_moves','refresh_missed_moves','defer_escape_failures','refresh_escape_failures']),('planning_queries',['defer_planning_false_negative','defer_planning_false_positive','refresh_planning_false_negative','refresh_planning_false_positive'])]:
   den=sum(weight*r.get(denominator,0) for weight,r in zip(w,subset))
   for column in columns:
    entry[column]=dict(events=sum(r.get(column,0) for r in subset),denominator=denominator,weighted_rate=sum(weight*r.get(column,0) for weight,r in zip(w,subset))/den if den else None)
  entry['guard_balance_queries']=sum(r.get('guard_balance_queries',0) for r in subset)
  result[family]=entry
 return result

def expected_family_scope(value):
 """Coverage requirements are declared before fitting; they never filter rows."""
 names=tuple(value.split(','))
 if not names or len(set(names))!=len(names) or set(names)-set(d.FAMILIES):raise ValueError('Expected-family scope must name unique known families')
 return names

def require_family_support(rows,minimum,expected):
 """Missing families outside the campaign scope remain explicitly unmeasured."""
 cov=d.coverage(rows)
 missing={family:cov[family] for family in expected if cov[family]['maps']<minimum}
 if missing:raise ValueError('Incomplete required family support; collect more compatible examples: '+str(missing))
 return cov

def fit(a):
 from tools import adaptive_gradient_collection as collection
 threshold=d.threshold_milli(getattr(a,'threshold_milli',d.DEFAULT_THRESHOLD_MILLI))
 expected=expected_family_scope(a.expected_families)
 features=d.SELECTED_FEATURES;transforms=d.SELECTED_TRANSFORMS
 allrows=collection.load(a.manifest) if a.collector_contract in collection.SUPPORTED_CONTRACT_IDS else d.load(a.manifest);rows=d.eligible(allrows)
 registry=Path(a.split_registry)
 if not registry.exists():raise ValueError('Persistent map splits must be frozen from map metadata before collection')
 assignments=json.loads(registry.read_text())['assignments']
 if any(r['group'] not in assignments for r in rows):raise ValueError('Dataset contains maps absent from the predeclared split registry')
 import tempfile,shutil
 with tempfile.TemporaryDirectory() as temp:
  validation_copy=Path(temp)/registry.name;shutil.copy2(registry,validation_copy)
  held,folds,split=registered_split(rows,validation_copy,integer.split_maps)
 split['registry'].update(name=registry.name,sha256=d.file_hash(registry),read_only_during_fit=True)
 dev=[r for r in rows if r['group'] not in held];test=[r for r in rows if r['group'] in held]
 def require(population,minimum):
  require_family_support(population,minimum,expected)
 require(dev,3);require(test,1);cv=[]
 for fold in range(3):
  training=[r for r in dev if folds[r['group']]!=fold];validation=[r for r in dev if folds[r['group']]==fold];require(training,2);require(validation,1)
  model=integer.fit_heads(training,features,transforms,weighting=d.balanced_weights,threshold=threshold);prior=integer.fit_heads(training,features,transforms,weighting=prior_weights,threshold=threshold);cv.append(dict(overall=integer.metrics(validation,model,weighting=d.balanced_weights),slices=slices(validation,model),prior_weight_fit_same_features=integer.metrics(validation,prior,weighting=d.balanced_weights)))
 model=integer.fit_heads(dev,features,transforms,weighting=d.balanced_weights,threshold=threshold);prior=integer.fit_heads(dev,features,transforms,weighting=prior_weights,threshold=threshold);model['candidate_contract']=d.CONTRACT;model['provenance']=dict(expected_families=list(expected),not_required_families=[family for family in d.FAMILIES if family not in expected],unmeasured_families=[family for family,value in d.coverage(rows).items() if not value['episodes']],campaign_scope='Declared family coverage requirements; all observed eligible rows retained. Missing families remain unmeasured, never imputed.',collector_contract=a.collector_contract,source_collector_contracts=sorted({r.get('collector_contract_id',a.collector_contract) for r in allrows}),source_contract_sha256=sorted({r['source_contract_sha256'] for r in allrows}),collection_thresholds_milli=sorted({r['collection_threshold_milli'] for r in allrows if r['collection_threshold_milli'] is not None}),manifests=[dict(path=str(p),sha256=d.file_hash(p)) for p in a.manifest],split=split,coverage=d.coverage(rows),age_demand_support=age_demand_support(rows),adaptive_policy=dict(minimum_interval=8,forced_age_cap=None,publication_delay=8,threshold=threshold),unknown_or_planning_rows_excluded=len(allrows)-len(rows),development_maps=sorted({r['group'] for r in dev}),heldout_maps=sorted(held),cross_validation=cv,holdout=integer.metrics(test,model,weighting=d.balanced_weights),holdout_slices=slices(test,model),holdout_separate_outcomes=outcome_metrics(test),weighting_comparison=dict(same_features_and_rows_and_splits=True,new_weight_fit_new_score=integer.metrics(test,model,weighting=d.balanced_weights),new_weight_fit_prior_score=integer.metrics(test,model,weighting=prior_weights),prior_weight_fit_new_score=integer.metrics(test,prior,weighting=d.balanced_weights),prior_weight_fit_prior_score=integer.metrics(test,prior,weighting=prior_weights),prior_weight_model=prior),weighting='equal format/role/cohort/map/family/collection/episode',threshold=threshold,note='Research only. Explicit emission and prospective per-family/total-compute validation required.')
 integer.save(a.output,model)

def compact_deployment(model,training_inputs=None,evidence_url=None):
 """Keep the portable deployment and hash-links to immutable training evidence.

 Full fit metrics remain in the original research JSON. Metadata compaction is
 not a refit: code generation must produce exactly the same C++ header.
 """
 validate(model)
 keep=('schema','contract','features','transforms','threshold','trained',*HEADS,'deployment','prospective_validation_required')
 result={key:copy.deepcopy(model[key]) for key in keep if key in model}
 source=model.get('provenance',{});registry=source.get('split',{}).get('registry',{})
 provenance=dict(source_ablation_sha256=source.get('source_ablation_sha256'),source_generic12_fit_sha256=source.get('source_generic12_fit_sha256'),source_fit_sha256=model.get('deployment',{}).get('source_model_sha256'),
                 split_registry_sha256=registry.get('sha256'),
                 eligible_episode_count=sum(v['episodes'] for v in source.get('coverage',{}).values()),
                 development_map_count=len(source.get('development_maps',[])),
                 heldout_map_count=len(source.get('heldout_maps',[])),
                 expected_families=source.get('expected_families',[]),
                 unmeasured_families=source.get('unmeasured_families',[]),
                 collector_contracts=source.get('source_collector_contracts',[source['collector_contract']] if source.get('collector_contract') else []),
                 collection_thresholds_milli=source.get('collection_thresholds_milli',[]))
 if training_inputs:
  path=Path(training_inputs);inputs=json.loads(path.read_text());slots=inputs.get('expected_slots')
  if type(slots)is not int or slots<=0 or sum(inputs.get('slot_counts',{}).get(k,0) for k in ('completed','user-censored'))!=slots:raise ValueError('Training input must account for every declared game slot')
  hashes=sorted(inputs['files'][name] for name in inputs['manifests'])
  if hashes!=sorted(m['sha256'] for m in source.get('manifests',[])):raise ValueError('Training input manifests differ from source fit')
  if inputs['files'].get('map-splits.json')!=registry.get('sha256'):raise ValueError('Training input grouped splits differ from source fit')
  provenance.update(training_input_sha256=d.file_hash(path),training_game_slots=slots,
                    terminal_counts=inputs['slot_counts'],
                    analysis_source_identity_sha256=inputs['files'].get('ANALYSIS-SOURCE-A16-FROZEN.json'))
 if evidence_url:
  if not evidence_url.startswith('https://'):raise ValueError('Evidence URL must be an HTTPS link')
  provenance['evidence_url']=evidence_url
 result['provenance']={key:value for key,value in provenance.items() if value is not None}
 validate(result)
 return result

def emit(a):
 model=json.loads(Path(a.model).read_text())
 if a.contract!=d.CONTRACT_ID or not d.compatible_contract(model.get('candidate_contract')) or model.get('schema')!=2 or not model.get('research_only'):raise ValueError('Explicit new unified research contract required')
 source_threshold=d.threshold_milli(model.get('threshold'))
 integer.validate_heads(model,research=True,expected_threshold=source_threshold)
 model=copy.deepcopy(model)
 # Threshold changes affect decisions/metrics, never the already fitted heads.
 # Preserve original fit metrics and provenance under their original threshold.
 threshold=d.threshold_milli(getattr(a,'threshold_milli',d.DEFAULT_THRESHOLD_MILLI))
 model.update(schema=4,contract=d.CONTRACT,threshold=threshold,trained=True,prospective_validation_required=True,
              deployment=dict(threshold_milli=threshold,source_fit_threshold_milli=source_threshold,
                              source_model_sha256=d.file_hash(a.model),source_candidate_contract_sha256=d.canonical_hash(model['candidate_contract']),
                              source_fit_metrics_threshold_milli=source_threshold,
                              fit_metrics_recomputed=False,coefficients_refit=False))
 model.pop('research_only',None);validate(model)
 from tools.adaptive_gradient_codegen import render
 header=render(model)
 if getattr(a,'compact',False):
  model=compact_deployment(model,getattr(a,'training_inputs',None),getattr(a,'evidence_url',None))
  if render(model)!=header:raise ValueError('Metadata compaction changed generated inference')
 integer.save(a.output,model);Path(a.header).write_text(header)

def main():
 p=argparse.ArgumentParser();s=p.add_subparsers(dest='action',required=True);f=s.add_parser('fit');f.add_argument('--manifest',action='append',required=True);f.add_argument('--split-registry',required=True);f.add_argument('--output',required=True);f.add_argument('--expected-families',default=','.join(d.FAMILIES),help='Comma-separated required campaign families (default: all); does not filter observed rows');f.add_argument('--collector-contract',choices=['unified-gradient-h128-v2','unified-gradient-h128-v4','unified-gradient-spatial-h128-v3'],default='unified-gradient-h128-v4');f.add_argument('--threshold-milli',type=d.threshold_milli,default=d.DEFAULT_THRESHOLD_MILLI);f.set_defaults(run=fit)
 e=s.add_parser('emit');e.add_argument('--model',required=True);e.add_argument('--contract',choices=[d.CONTRACT_ID],required=True);e.add_argument('--output',required=True);e.add_argument('--header',required=True);e.add_argument('--threshold-milli',type=d.threshold_milli,default=d.DEFAULT_THRESHOLD_MILLI);e.add_argument('--compact',action='store_true',help='Retain deployment and small hash-linked provenance; full fit JSON stays external');e.add_argument('--training-inputs',help='Immutable terminal input receipt for compact provenance');e.add_argument('--evidence-url',help='Published full training and validation evidence');e.set_defaults(run=emit);a=p.parse_args();a.run(a)
if __name__=='__main__':main()
