#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic publication-generation spatial overlap; no inference promotion."""
import argparse,collections,copy,json,sys,tempfile,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from tools import adaptive_gradient_dataset as base
from tools import resource_gradient_model as integer
from tools.resource_gradient_dataset import registered_split

CONTRACT_ID='unified-gradient-spatial-h128-v3'
COUNTS=('queries_near_goal_change','queries_near_passability_change','overlap_movement_queries')
DIRECTIONAL=('queries_near_goal_added','queries_near_goal_removed','queries_near_blocked','queries_near_opened')
HISTORY=('overlap_supported','overlap_history_known','overlap_generation','overlap_publication_tick')
RATES=tuple(k+'_frac_q12' for k in COUNTS[:2])
DIRECTIONAL_RATES=tuple(k+'_frac_q12' for k in DIRECTIONAL)
CONTRACT=copy.deepcopy(base.CONTRACT)
CONTRACT.update(id=CONTRACT_ID,model_feature_contract=base.CONTRACT_ID,movement_counterfactual_reachability=dict(columns=list(base.MOVEMENT_REACHABILITY),denominator='queries',definition='raw-field-value-at-query-cell>1;each-defer/refreshed-vs-current-fresh',classification='independent-of-selector-arrival-or-intentional-stay;separate-from-planning-and-direction-cost'),spatial=dict(
 schema=1,region_tiles=4,scope='validated-materials-and-clear-ground;other-families-explicitly-unsupported',movement_classes=list(range(8)),
 history='publication-generation;reset-on-actual-publication;not-capture-baseline',
 metadata=[*HISTORY,'overlap_support_reason'],counts=list(COUNTS),directional=list(DIRECTIONAL),
 seed_classes='published0=forbidden;65535=goal;other=open;positive-cost-fields-with-validated-seed-recovery',
 passability='(oldOpen^currentOpen)&~(oldGoals|currentGoals)',
 current_inputs='authoritative-world-family-seed-predicate;exclude-transient-unit-occupancy',
 disabled='explicit-isolation-mode;supported-family-knownfalse;zero-counters-are-unmeasured',sampling='every-eligible-query;once-per-category;categories-may-overlap'))

def load(manifests):
 rows=base.load(manifests,expected_contract=CONTRACT)
 for row in rows:
  if not set((*COUNTS,*DIRECTIONAL,*HISTORY,'overlap_support_reason'))<=row.keys():raise ValueError('Missing explicit spatial history/counter columns; older rows are not zero')
  if row['overlap_history_known'] not in (0,1) or row['overlap_supported'] not in (0,1):raise ValueError('Invalid spatial history-known flag')
  if any(not 0<=row[k]<=base.UINT64_MAX for k in (*COUNTS,*DIRECTIONAL,'overlap_generation','overlap_publication_tick')):raise ValueError('Invalid spatial integer counter')
  expected_reason='air_unvalidated' if row['movement']>=8 else {0:'natural_ground',1:'market_supplier_seeds',2:'guard_weighted_seeds',3:'clear_ground',4:'forbidden_border_seeds'}[row['family_id']]
  disabled=row['overlap_support_reason']=='disabled'
  if disabled:
   if expected_reason not in ('natural_ground','clear_ground') or not row['overlap_supported'] or row['overlap_history_known'] or any(row[k] for k in (*COUNTS,*DIRECTIONAL)):raise ValueError('Disabled overlap must be an explicitly unknown eligible adapter')
  elif row['overlap_support_reason']!=expected_reason or row['overlap_supported']!=int(expected_reason in ('natural_ground','clear_ground')):raise ValueError('Spatial support reason disagrees with family adapter')
  if not row['overlap_supported'] and any(row[k] for k in (*COUNTS,*DIRECTIONAL)):raise ValueError('Unsupported spatial adapter emitted measured counters')
  q=row[COUNTS[2]]
  if any(row[k]>q for k in (*COUNTS[:2],*DIRECTIONAL)):raise ValueError('Overlap event exceeds publication-window denominator')
  if any(row[k]>row[COUNTS[0]] for k in DIRECTIONAL[:2]) or any(row[k]>row[COUNTS[1]] for k in DIRECTIONAL[2:]):raise ValueError('Directional event exceeds parent category')
  if row['overlap_history_known']:
   if (not row['overlap_supported'] or row['family_id'] not in (0,3)) or row['movement'] not in CONTRACT['spatial']['movement_classes']:raise ValueError('Spatial history declared known for ineligible field')
   if not row['overlap_generation'] or not ((row['tick']-row['overlap_publication_tick'])&0xffffffff)<2**31:raise ValueError('Invalid published-generation identity/window')
  for k,rate in zip((*COUNTS[:2],*DIRECTIONAL),(*RATES,*DIRECTIONAL_RATES)):row[rate]=row[k]*4096//max(1,q)
 return rows

def eligible(rows):
 return [r for r in base.eligible(rows) if r['family_id'] in (0,3) and r['overlap_supported'] and r['movement'] in CONTRACT['spatial']['movement_classes'] and r['overlap_history_known']]

def variants():
 return {
 'baseline12':(base.FEATURES,base.TRANSFORMS),
 'overlap_counts':((*base.FEATURES,*COUNTS),(*base.TRANSFORMS,*(['log1p_q12']*3))),
 'overlap_fractions':((*base.FEATURES,*RATES),(*base.TRANSFORMS,'identity_q12','identity_q12')),
 'overlap_counts_fractions':((*base.FEATURES,*COUNTS,*RATES),(*base.TRANSFORMS,*(['log1p_q12']*3),'identity_q12','identity_q12')),
 'directional_counts_fractions':((*base.FEATURES,*COUNTS,*DIRECTIONAL,*RATES,*DIRECTIONAL_RATES),(*base.TRANSFORMS,*(['log1p_q12']*7),*(['identity_q12']*6))),
 }

def extra_metrics(rows,model,comparable_rates):
 import numpy as np
 w=base.balanced_weights(rows);pred=np.array([integer.predict(model,[r[k] for k in model['features']])/1000 for r in rows]);y=np.array([r['defer_extra']-r['refresh_extra'] for r in rows]);total=float(np.sum(w*np.maximum(y,0)));calibration=[]
 for low,high in ((0,64),(64,96),(96,112),(112,128),(128,144),(144,160),(160,192),(192,256),(256,float('inf'))):
  mask=(pred>=low)&(pred<high);calibration.append(dict(predicted_range=[low,high if high!=float('inf') else None],episodes=int(mask.sum()),weight=float(w[mask].sum()),predicted_mean=float(np.average(pred[mask],weights=w[mask])) if mask.any() else None,observed_mean=float(np.average(y[mask],weights=w[mask])) if mask.any() else None))
 # Weighted top-score ranks; exact tied scores are fractionally selected together,
 # yielding the requested refresh share without outcome-based tie breaking.
 order=np.argsort(-pred,kind='stable');curves=[]
 for rate in comparable_rates:
  remaining=float(w.sum())*rate;selected=np.zeros(len(rows));i=0
  while i<len(order) and remaining>0:
   j=i+1
   while j<len(order) and pred[order[j]]==pred[order[i]]:j+=1
   group=order[i:j];mass=float(w[group].sum());fraction=min(1.,remaining/mass);selected[group]=fraction;remaining-=mass*fraction;i=j
  curves.append(dict(refresh_fraction=rate,positive_benefit_captured_fraction=float(np.sum(w*np.maximum(y,0)*selected))/total if total else None,net_benefit=float(np.sum(w*y*selected)/w.sum()),scope='offline-ranked-episode-decisions;not-simulation-refresh-rate'))
 return dict(near_threshold_calibration=calibration,comparable_selection_rates=curves)

def compare(a):
 rows=load(a.manifest);chosen=eligible(rows)
 if not chosen:raise ValueError('No validated known resource/clear ground spatial episodes')
 registry=Path(a.split_registry)
 if not registry.exists():raise ValueError('Persistent pre-outcome map registry required')
 assignments=json.loads(registry.read_text())['assignments']
 if any(r['group'] not in assignments for r in chosen):raise ValueError('Missing predeclared map group')
 with tempfile.TemporaryDirectory() as temp:
  target=Path(temp)/registry.name;shutil.copy2(registry,target);held,folds,split=registered_split(chosen,target,integer.split_maps)
 dev=[r for r in chosen if r['group'] not in held];test=[r for r in chosen if r['group'] in held]
 expected_formats={r['format'] for r in rows if r['family_id'] in (0,3) and r['movement']<8}
 def require(population):
  if {r['format'] for r in population}!=expected_formats:raise ValueError('Known spatial filter loses format coverage in development/heldout fold')
  if {r['family_id'] for r in population}!={0,3}:raise ValueError('Known eligible resource AND clear support required in each development/heldout fold; unsupported families are not zeros')
 require(dev);require(test)
 reports={}
 for name,(features,transforms) in variants().items():
  cv=[]
  for fold in range(3):
   train=[r for r in dev if folds[r['group']]!=fold];validation=[r for r in dev if folds[r['group']]==fold]
   require(train);require(validation)
   model=integer.fit_heads(train,features,transforms,weighting=base.balanced_weights,threshold=128000);cv.append(integer.metrics(validation,model,weighting=base.balanced_weights))
  model=integer.fit_heads(dev,features,transforms,weighting=base.balanced_weights,threshold=128000);slices={}
  for key in ('family','format','generator','dimensions','movement'):
   groups=collections.defaultdict(list)
   for r in test:groups[str(r.get(key,'unknown'))].append(r)
   slices[key]={k:integer.metrics(v,model,weighting=base.balanced_weights) for k,v in groups.items()}
  reports[name]=dict(model=model,cross_validation=cv,holdout=integer.metrics(test,model,weighting=base.balanced_weights),holdout_slices=slices,**extra_metrics(test,model,[.01,.05,.1,.2,.3,.5]))
 from tools.adaptive_gradient_model import outcome_metrics
 selected=min(reports,key=lambda name:sum(f['decision_regret'] for f in reports[name]['cross_validation'])/3)
 report=dict(manifests=[dict(path=str(p),sha256=base.file_hash(p)) for p in a.manifest],common_row_identity_sha256=base.canonical_hash(sorted((r['collection'],r['tick'],r['key']) for r in chosen)),development_cv_selected_variant=selected,schema=1,research_only=True,contract=CONTRACT,rows=len(chosen),excluded_unknown_or_ineligible=len(rows)-len(chosen),identical_rows_weights_splits=True,support={family:dict(rows=sum(r['family']==family for r in rows),supported=sum(r['family']==family and bool(r['overlap_supported']) for r in rows),support_reasons=dict(collections.Counter(r['overlap_support_reason'] for r in rows if r['family']==family)),known=sum(r['family']==family and bool(r['overlap_history_known']) for r in rows),eligible=sum(r['family']==family for r in chosen)) for family in base.FAMILIES},split=split,registry_sha256=base.file_hash(registry),variants=reports,separate_counterfactual_outcomes=outcome_metrics(test),note='Select using development folds. Held-out variants exploratory; no automatic production model emission. Ranked episode selection rates are not actual simulation compute. No causal or measured CPU benefit claim.')
 integer.save(a.output,report)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--manifest',action='append',required=True);p.add_argument('--split-registry',required=True);p.add_argument('--output',required=True);compare(p.parse_args())
