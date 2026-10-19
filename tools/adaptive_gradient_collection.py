# SPDX-License-Identifier: GPL-3.0-or-later
"""Current H128 collection contract and explicit read-only historical decoding.

The optional spatial experiment is not part of current collection or inference.
Its exact v3 schema remains readable so sealed datasets are reproducible; absent
historical measurements are never synthesized as zeros in current v4 rows.
"""
import collections,copy
from tools import adaptive_gradient_dataset as base
from tools.resource_gradient_dataset import manifest_records,canonical_hash

CONTRACT_ID='unified-gradient-h128-v4'
LEGACY_CONTRACT_ID='unified-gradient-spatial-h128-v3'
SUPPORTED_CONTRACT_IDS=(CONTRACT_ID,LEGACY_CONTRACT_ID)
CONTRACT=copy.deepcopy(base.CONTRACT)
CONTRACT.update(id=CONTRACT_ID,model_feature_contract=base.CONTRACT_ID,
 movement_counterfactual_reachability={'columns': ['defer_false_unreachable',
             'refresh_false_unreachable',
             'defer_false_reachable',
             'refresh_false_reachable'],
 'denominator': 'queries',
 'definition': 'raw-field-value-at-query-cell>1;each-defer/refreshed-vs-current-fresh',
 'classification': 'independent-of-selector-arrival-or-intentional-stay;separate-from-planning-and-direction-cost'})
LEGACY_CONTRACT=copy.deepcopy(CONTRACT)
LEGACY_CONTRACT.update(id=LEGACY_CONTRACT_ID,spatial={'schema': 1,
 'region_tiles': 4,
 'scope': 'validated-materials-and-clear-ground;other-families-explicitly-unsupported',
 'movement_classes': [0, 1, 2, 3, 4, 5, 6, 7],
 'history': 'publication-generation;reset-on-actual-publication;not-capture-baseline',
 'metadata': ['overlap_supported',
              'overlap_history_known',
              'overlap_generation',
              'overlap_publication_tick',
              'overlap_support_reason'],
 'counts': ['queries_near_goal_change',
            'queries_near_passability_change',
            'overlap_movement_queries'],
 'directional': ['queries_near_goal_added',
                 'queries_near_goal_removed',
                 'queries_near_blocked',
                 'queries_near_opened'],
 'seed_classes': 'published0=forbidden;65535=goal;other=open;positive-cost-fields-with-validated-seed-recovery',
 'passability': '(oldOpen^currentOpen)&~(oldGoals|currentGoals)',
 'current_inputs': 'authoritative-world-family-seed-predicate;exclude-transient-unit-occupancy',
 'disabled': 'explicit-isolation-mode;supported-family-knownfalse;zero-counters-are-unmeasured',
 'sampling': 'every-eligible-query;once-per-category;categories-may-overlap'})
LEGACY_COUNTS=('queries_near_goal_change','queries_near_passability_change','overlap_movement_queries')
LEGACY_DIRECTIONAL=('queries_near_goal_added','queries_near_goal_removed','queries_near_blocked','queries_near_opened')
LEGACY_HISTORY=('overlap_supported','overlap_history_known','overlap_generation','overlap_publication_tick')
LEGACY_COLUMNS=(*LEGACY_COUNTS,*LEGACY_DIRECTIONAL,*LEGACY_HISTORY,'overlap_support_reason')

def contract_id(contract):
 """Recognize only these two complete schemas, normalizing behavior metadata."""
 for identifier,expected in [(CONTRACT_ID,CONTRACT),(LEGACY_CONTRACT_ID,LEGACY_CONTRACT)]:
  if base.compatible_contract(contract,expected):return identifier
 raise ValueError('Unsupported unified collection contract; H128 labels and feature semantics must match exactly')

def compatible_contract(contract):
 try:contract_id(contract);return True
 except ValueError:return False

def compatible_contract_hash(value):
 # A hash-only historical receipt cannot justify arbitrary omitted semantics.
 return value==canonical_hash(CONTRACT) or base.compatible_contract_hash(value,LEGACY_CONTRACT)

def _validate_legacy(row):
 """Validate genuine v3 observations without preparing experimental features."""
 if not set(LEGACY_COLUMNS)<=row.keys():raise ValueError('Missing historical spatial observations; absence is not zero')
 if row['overlap_history_known'] not in (0,1) or row['overlap_supported'] not in (0,1):raise ValueError('Invalid historical spatial history-known flag')
 if any(not 0<=row[k]<=base.UINT64_MAX for k in (*LEGACY_COUNTS,*LEGACY_DIRECTIONAL,'overlap_generation','overlap_publication_tick')):raise ValueError('Invalid historical spatial integer counter')
 expected='air_unvalidated' if row['movement']>=8 else {0:'natural_ground',1:'market_supplier_seeds',2:'guard_weighted_seeds',3:'clear_ground',4:'forbidden_border_seeds'}[row['family_id']]
 if row['overlap_support_reason']=='disabled':
  if expected not in ('natural_ground','clear_ground') or not row['overlap_supported'] or row['overlap_history_known'] or any(row[k] for k in (*LEGACY_COUNTS,*LEGACY_DIRECTIONAL)):raise ValueError('Disabled historical observations must be explicitly unknown')
 elif row['overlap_support_reason']!=expected or row['overlap_supported']!=int(expected in ('natural_ground','clear_ground')):raise ValueError('Historical adapter support reason disagrees with family')
 if not row['overlap_supported'] and any(row[k] for k in (*LEGACY_COUNTS,*LEGACY_DIRECTIONAL)):raise ValueError('Unsupported historical adapter emitted measurements')
 q=row[LEGACY_COUNTS[2]]
 if any(row[k]>q for k in (*LEGACY_COUNTS[:2],*LEGACY_DIRECTIONAL)):raise ValueError('Historical event exceeds publication-window denominator')
 if any(row[k]>row[LEGACY_COUNTS[0]] for k in LEGACY_DIRECTIONAL[:2]) or any(row[k]>row[LEGACY_COUNTS[1]] for k in LEGACY_DIRECTIONAL[2:]):raise ValueError('Historical directional event exceeds parent category')
 if row['overlap_history_known']:
  if not row['overlap_supported'] or row['family_id'] not in (0,3) or row['movement'] not in LEGACY_CONTRACT['spatial']['movement_classes']:raise ValueError('Historical history declared known for unsupported field')
  if not row['overlap_generation'] or not ((row['tick']-row['overlap_publication_tick'])&0xffffffff)<2**31:raise ValueError('Invalid historical published-generation window')

def load(manifests):
 """Pool unchanged H128 direction labels, preserving each original declaration.

 Cohort identities cannot change across collector versions. Each schema is
 validated by its original requirements, then the generic twelve features use
 only their common authoritative inputs. No v3 analysis model is retained.
 """
 groups=collections.defaultdict(list);cohorts={}
 for path in manifests:
  _,metadata=manifest_records(path)
  if not metadata:raise ValueError('Explicit sealed cohort metadata required')
  identifier=contract_id(metadata.get('contract'));declared=canonical_hash(metadata)
  if metadata['id'] in cohorts and cohorts[metadata['id']]!=declared:raise ValueError('Cohort identity changed across collector declarations')
  cohorts[metadata['id']]=declared;groups[identifier].append(path)
 rows=[];seen=set()
 for identifier,paths in groups.items():
  expected=CONTRACT if identifier==CONTRACT_ID else LEGACY_CONTRACT
  chunk=base.load(paths,expected_contract=expected);collections_in_chunk={r['collection'] for r in chunk}
  if seen & collections_in_chunk:raise ValueError('Collection assigned to multiple collector schemas')
  seen.update(collections_in_chunk)
  for row in chunk:
   if identifier==LEGACY_CONTRACT_ID:_validate_legacy(row)
   elif any(k in row for k in LEGACY_COLUMNS):raise ValueError('Current v4 collection cannot claim removed spatial observations')
   row['collector_contract_id']=identifier
  rows.extend(chunk)
 if not rows:raise ValueError('No complete unified episodes')
 return rows
