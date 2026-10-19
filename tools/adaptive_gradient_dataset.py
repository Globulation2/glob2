# SPDX-License-Identifier: GPL-3.0-or-later
"""The all-family dataset contract. Resource-only cohorts are never inferred compatible."""
import collections,csv,gzip,json
from pathlib import Path
from tools.resource_gradient_dataset import canonical_hash,file_hash,manifest_records
FAMILIES=('materials','markets','guard','clear','forbidden')
BASE_FEATURES=('age','q32','q128','target_added','target_removed','target_added_frac_q12','target_removed_frac_q12')
COUNTERS=('since_capture_invalid_goal','since_capture_reachable_no_move','since_capture_movement_queries')
FRACTIONS=tuple(k+'_frac_q12' for k in COUNTERS[:2])
FEATURES=(*BASE_FEATURES,*COUNTERS,*FRACTIONS)
TRANSFORMS=('log1p_q12',)*10+('identity_q12',)*2
CAPTURE_COLUMNS=('serving_capture_tick','captured_goal_count','capture_target_added_epoch','capture_target_removed_epoch','capture_route_epoch','capture_seed_cost_epoch','current_target_added_epoch','current_target_removed_epoch','current_route_epoch','current_seed_cost_epoch')
MOVEMENT_REACHABILITY=('defer_false_unreachable','refresh_false_unreachable','defer_false_reachable','refresh_false_reachable')
CONTRACT_ID='unified-gradient-h128-v2'
CONTRACT=dict(id=CONTRACT_ID,features=list(BASE_FEATURES),counters=list(COUNTERS),families=list(FAMILIES),
 capture_metadata=list(CAPTURE_COLUMNS),publication_delay=8,key='family:u8,team:u8,movement:u8,material:u8;255=none',target_semantics='effective-seed-membership-v1',
 label='movement-direction-extra-cost',benefit_unit='direction-cost',request_type='movement',future_offsets=[8,127],horizon_ticks=128,
 invalid_goal='cached-arrival-rejected-by-real-family-destination-predicate',stall='reachable-required-travel-selector-no-move',
 selector='real-family-selector;pre-decision-RNG-copy;forbidden-lexicographic-base-option',
 separate_outcomes=['stalls','invalid_goals','false_unreachable','false_reachable','missed_moves','escape_failures','planning'])
COUNTER_IDS=(0,4,5)
UINT64_MAX=(1<<64)-1
DEFAULT_THRESHOLD_MILLI=16000
MAX_THRESHOLD_MILLI=(1<<63)-1

def threshold_milli(value):
 """Positive signed-64-bit deployment behavior, independent of label semantics."""
 if isinstance(value,str):
  if not value or not value.isascii() or not value.isdecimal():raise ValueError('Threshold must be a positive integer milli direction-cost value')
  value=int(value)
 if type(value)is not int or not 0<value<=MAX_THRESHOLD_MILLI:raise ValueError('Threshold must be a positive signed-64-bit integer')
 return value

def declared_threshold_milli(value):
 if type(value)is not int:raise ValueError('Threshold declaration must be an integer')
 return threshold_milli(value)

def label_contract(contract):
 """Normalize only the historical misplaced behavior field; change nothing else.

 Old sealed cohorts keep their exact contract/hash. Their threshold describes
 collection behavior, not the unchanged capture-anchored H128 outcome label.
 """
 if not isinstance(contract,dict):raise ValueError('Explicit unified contract required')
 normalized=dict(contract)
 if 'refresh_threshold_milli' in normalized:declared_threshold_milli(normalized.pop('refresh_threshold_milli'))
 return normalized

def compatible_contract(actual,expected=CONTRACT):
 try:return label_contract(actual)==label_contract(expected)
 except ValueError:return False

def compatible_contract_hash(value,expected=CONTRACT):
 """Hash-only receipts permit exactly canonical or the frozen historical128 declaration."""
 canonical=label_contract(expected)
 return value in {canonical_hash(canonical),canonical_hash(dict(canonical,refresh_threshold_milli=128000))}

def collection_threshold(metadata):
 policy=metadata.get('policy',{})
 direct=policy.get('threshold_milli')
 nested=policy.get('adaptive',{}).get('threshold_milli')
 declared=[declared_threshold_milli(v) for v in (direct,nested) if v is not None]
 if len(set(declared))>1:raise ValueError('Conflicting collection behavior thresholds')
 if declared:return declared[0]
 legacy=metadata['contract'].get('refresh_threshold_milli')
 return declared_threshold_milli(legacy) if legacy is not None else None

def prepare(base,counts):
 if len(base)!=7 or len(counts)!=3 or any(type(v)is not int or v<0 or v>UINT64_MAX for v in [*base,*counts]):raise ValueError('Invalid generic integer inputs')
 if any(v>counts[2] for v in counts[:2]):raise ValueError('Event exceeds movement-query denominator')
 return [*(min(v,1000000) for v in base),*(min(v,1000000) for v in counts),*(v*4096//max(1,counts[2]) for v in counts[:2])]

def load(manifests,expected_contract=CONTRACT):
 rows=[];seen={};cohorts={}
 for path in manifests:
  records,metadata=manifest_records(path)
  if not metadata or not compatible_contract(metadata.get('contract'),expected_contract):raise ValueError('Unified data requires the exact sealed '+expected_contract['id']+' contract; resource-only pooling is forbidden')
  if metadata.get('role') not in ('exploration','candidate'):raise ValueError('Explicit collection role required')
  behavior_threshold=collection_threshold(metadata)
  declared=canonical_hash(metadata)
  if metadata['id'] in cohorts and cohorts[metadata['id']]!=declared:raise ValueError('Cohort identity changed')
  cohorts[metadata['id']]=declared
  for record in records:
   p=Path(record['episodes']);p=p if p.is_absolute() else Path(path).resolve().parent/p
   if file_hash(p)!=record['episode_sha256']:raise ValueError('Episode bytes changed')
   identity=(record['group'],record['binary_sha256'],record['episode_sha256'])
   if identity in seen:
    if seen[identity]!=metadata['id']:raise ValueError('Collection assigned multiple cohorts')
    continue
   seen[identity]=metadata['id'];spec=record.get('spec',{});group=record.get('map_group') or record['group']
   with (gzip.open(p,'rt') if p.suffix=='.gz' else p.open()) as stream:
    reader=csv.DictReader(stream);required={'family','team','movement','material','key','tick','complete','publication_delay','queries','defer_extra','refresh_extra','capture_known','movement_observed',*BASE_FEATURES,*COUNTERS,*CAPTURE_COLUMNS,*MOVEMENT_REACHABILITY}
    if not required<=set(reader.fieldnames or ()):raise ValueError('Missing generic episode columns')
    for line in reader:
     if None in line or None in line.values():raise ValueError('Truncated/malformed episode record; derive a validated newline-complete artifact first')
     row={k:(v if k=='overlap_support_reason' else int(v)) for k,v in line.items()}
     if row['complete'] not in (0,1) or row['capture_known'] not in (0,1):raise ValueError('Invalid known/complete flag')
     if not row['complete']:continue
     if row['publication_delay']!=8:raise ValueError('Episode publication delay differs from the frozen8..127 label horizon')
     family=row['family'];team=row['team'];move=row['movement'];material=row['material']
     if not 0<=family<5 or not 0<=team<32 or not 0<=move<256 or not 0<=material<256 or (family>=2 and material!=255) or (family<2 and material==255):raise ValueError('Invalid stable field identity')
     if row['key']!=(family<<24|team<<16|move<<8|material):raise ValueError('Field key disagrees with identity columns')
     if any(row[k]<0 for k in CAPTURE_COLUMNS):raise ValueError('Negative capture metadata')
     added=row['current_target_added_epoch']-row['capture_target_added_epoch'];removed=row['current_target_removed_epoch']-row['capture_target_removed_epoch']
     if added<0 or removed<0 or row['current_route_epoch']<row['capture_route_epoch'] or row['current_seed_cost_epoch']<row['capture_seed_cost_epoch']:raise ValueError('Capture epochs run backwards')
     expected=[(row['tick']-row['serving_capture_tick'])&0xffffffff,row['q32'],row['q128'],min(added,1000000),min(removed,1000000),min(1000000,min(added,1000000)*4096//max(1,row['captured_goal_count'])),min(1000000,min(removed,1000000)*4096//max(1,row['captured_goal_count']))]
     if [row[k] for k in BASE_FEATURES]!=expected:raise ValueError('Features disagree with serving capture metadata')
     values=prepare([row[k] for k in BASE_FEATURES],[row[k] for k in COUNTERS]);row.update(zip(FRACTIONS,values[-2:]))
     if any(row[k]<0 for k in ('queries','defer_extra','refresh_extra')):raise ValueError('Negative direction label')
     for k in (*MOVEMENT_REACHABILITY,'defer_stalls','refresh_stalls','defer_invalid_goals','refresh_invalid_goals','defer_missed_moves','refresh_missed_moves','defer_escape_failures','refresh_escape_failures'):
      if k in row and not 0<=row[k]<=row['queries']:raise ValueError('Outcome exceeds movement-query denominator')
     for arm in ('defer','refresh'):
      if row[arm+'_false_unreachable']+row[arm+'_false_reachable']>row['queries']:raise ValueError('Mutually exclusive reachability outcomes exceed movement queries')
     row.update(family=FAMILIES[family],family_id=family,group=group,format=record['format'],collection='|'.join(identity),cohort=metadata['id'],cohort_role=metadata['role'],generator=spec.get('generator','unknown'),dimensions=f"{spec.get('width','?')}x{spec.get('height','?')}",movement_label_available=int(row['movement_observed']>0),label_contract=canonical_hash(label_contract(expected_contract)),source_contract_sha256=canonical_hash(metadata['contract']),collection_threshold_milli=behavior_threshold,trajectory_status=record.get('trajectory_status','completed'))
     rows.append(row)
 if not rows:raise ValueError('No complete generic episodes')
 return rows

def eligible(rows):return [r for r in rows if r['capture_known'] and r['movement_label_available']]

def balanced_weights(rows):
 """Equal format, role, cohort, map, family, collection, then episode.

 Family balance is conditional on measured families; absence is a support gap,
 never invented zero error. OS/ELF does not receive a separate cohort weight.
 """
 import numpy as np
 hierarchy=[lambda r:r['format'],lambda r:r['cohort_role'],lambda r:r['cohort'],lambda r:r['group'],lambda r:r['family'],lambda r:r['collection']]
 children=collections.defaultdict(set);counts=collections.Counter();paths=[]
 for row in rows:
  path=()
  for key in hierarchy:
   child=key(row);children[path].add(child);path=(*path,child)
  counts[path]+=1;paths.append(path)
 w=[]
 for path in paths:
  den=counts[path]
  for i in range(len(path)):den*=len(children[path[:i]])
  w.append(1/den)
 result=np.asarray(w);return result*len(result)/result.sum()

def coverage(rows):
 return {family:dict(maps=len({r['group'] for r in rows if r['family']==family}),episodes=sum(r['family']==family for r in rows)) for family in FAMILIES}
