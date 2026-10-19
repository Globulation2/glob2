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
 if model.get('schema')!=4 or model.get('contract')!=d.CONTRACT or tuple(model.get('features',()))!=d.FEATURES or tuple(model.get('transforms',()))!=d.TRANSFORMS or type(model.get('trained'))is not bool or model.get('threshold')!=128000:raise ValueError('Expected exact unified deployment contract')
 temporary=dict(model,schema=2,research_only=True);integer.validate_heads(temporary,research=True,expected_threshold=128000)
 if not model['trained'] and any(model[name]['bias'] or any(model[name]['coef']) for name in HEADS):raise ValueError('Untrained bootstrap cannot contain fitted resource coefficients')

def predict(model,base,counts):
 validate(model)
 if not model['trained']:raise ValueError('No unified model has been trained; use explicit fixed-interval research bootstrap')
 return integer.predict(model,d.prepare(base,counts))

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
 from tools import adaptive_gradient_spatial as spatial
 expected=expected_family_scope(a.expected_families)
 allrows=spatial.load(a.manifest) if a.collector_contract==spatial.CONTRACT_ID else d.load(a.manifest);rows=d.eligible(allrows)
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
  model=integer.fit_heads(training,d.FEATURES,d.TRANSFORMS,weighting=d.balanced_weights,threshold=128000);prior=integer.fit_heads(training,d.FEATURES,d.TRANSFORMS,weighting=prior_weights,threshold=128000);cv.append(dict(overall=integer.metrics(validation,model,weighting=d.balanced_weights),slices=slices(validation,model),prior_weight_fit_same_features=integer.metrics(validation,prior,weighting=d.balanced_weights)))
 model=integer.fit_heads(dev,d.FEATURES,d.TRANSFORMS,weighting=d.balanced_weights,threshold=128000);prior=integer.fit_heads(dev,d.FEATURES,d.TRANSFORMS,weighting=prior_weights,threshold=128000);model['candidate_contract']=d.CONTRACT;model['provenance']=dict(expected_families=list(expected),not_required_families=[family for family in d.FAMILIES if family not in expected],unmeasured_families=[family for family,value in d.coverage(rows).items() if not value['episodes']],campaign_scope='Declared family coverage requirements; all observed eligible rows retained. Missing families remain unmeasured, never imputed.',collector_contract=a.collector_contract,manifests=[dict(path=str(p),sha256=d.file_hash(p)) for p in a.manifest],split=split,coverage=d.coverage(rows),age_demand_support=age_demand_support(rows),adaptive_policy=dict(minimum_interval=8,forced_age_cap=None,publication_delay=8,threshold=128000),unknown_or_planning_rows_excluded=len(allrows)-len(rows),development_maps=sorted({r['group'] for r in dev}),heldout_maps=sorted(held),cross_validation=cv,holdout=integer.metrics(test,model,weighting=d.balanced_weights),holdout_slices=slices(test,model),holdout_separate_outcomes=outcome_metrics(test),weighting_comparison=dict(same_features_and_rows_and_splits=True,new_weight_fit_new_score=integer.metrics(test,model,weighting=d.balanced_weights),new_weight_fit_prior_score=integer.metrics(test,model,weighting=prior_weights),prior_weight_fit_new_score=integer.metrics(test,prior,weighting=d.balanced_weights),prior_weight_fit_prior_score=integer.metrics(test,prior,weighting=prior_weights),prior_weight_model=prior),weighting='equal format/role/cohort/map/family/collection/episode',threshold=128000,note='Research only. Explicit emission and prospective per-family/total-compute validation required.')
 integer.save(a.output,model)

def emit(a):
 model=json.loads(Path(a.model).read_text())
 if a.contract!=d.CONTRACT_ID or model.get('candidate_contract')!=d.CONTRACT or model.get('schema')!=2 or not model.get('research_only'):raise ValueError('Explicit new unified research contract required')
 model=copy.deepcopy(model);model.update(schema=4,contract=d.CONTRACT,trained=True,prospective_validation_required=True);model.pop('research_only',None);validate(model)
 from tools.adaptive_gradient_codegen import render
 header=render(model);integer.save(a.output,model);Path(a.header).write_text(header)

def main():
 p=argparse.ArgumentParser();s=p.add_subparsers(dest='action',required=True);f=s.add_parser('fit');f.add_argument('--manifest',action='append',required=True);f.add_argument('--split-registry',required=True);f.add_argument('--output',required=True);f.add_argument('--expected-families',default=','.join(d.FAMILIES),help='Comma-separated required campaign families (default: all); does not filter observed rows');f.add_argument('--collector-contract',choices=['unified-gradient-h128-v2','unified-gradient-spatial-h128-v3'],default='unified-gradient-spatial-h128-v3');f.set_defaults(run=fit)
 e=s.add_parser('emit');e.add_argument('--model',required=True);e.add_argument('--contract',choices=[d.CONTRACT_ID],required=True);e.add_argument('--output',required=True);e.add_argument('--header',required=True);e.set_defaults(run=emit);a=p.parse_args();a.run(a)
if __name__=='__main__':main()
