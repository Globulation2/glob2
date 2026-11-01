#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Balanced all-family bootstrap plans using the ordinary tournament coordinator."""
import argparse,collections,copy,json,itertools,re
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from tools.resource_gradient_tournament import sample_plan,digest,map_identity
from tools import adaptive_gradient_collection as collector
from tools import adaptive_gradient_dataset as d
from tools.resource_gradient_experiments import exploration_seed_for_game
from tools.resource_gradient_distributed import make_experiment
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.common import store_artifact
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json

ADAPTIVE_POLICY=dict(minimum_interval=d.DEFAULT_MINIMUM_INTERVAL_TICKS,forced_age_cap=1024,publication_delay=8,cold_initialization=True,reactive_refresh=True,history_initialization=True,threshold_milli=d.DEFAULT_THRESHOLD_MILLI,horizon_ticks=128)

def policy_from_bundles(bundles):
 """Use the actual compiled model's qualified behavior, not a stale tool default.

 The collector contract describes H128 labels independently of the admission
 threshold. Fresh bundles must explicitly attest their compiled threshold, warm minimum interval and
 demanded-field age cap; historical missing/uncapped attestations are never
 silently upgraded by this launcher. Fixed exploration intervals above the
 attested cap are bounded by the same demanded-field safety admission.
 This leaves already-frozen campaigns and their original metadata untouched.
 """
 thresholds=[];caps=[];minimums=[]
 for bundle in bundles:
  value=bundle.get('build_options',{}).get('refresh_threshold_milli')
  if type(value)is not int:
   raise ValueError('Every supplied bundle must attest integer refresh_threshold_milli')
  thresholds.append(d.threshold_milli(value))
  minimum=bundle.get('build_options',{}).get('refresh_minimum_interval_ticks')
  if type(minimum)is not int or not 8<=minimum<=2**32-1:
   raise ValueError('Every supplied bundle must attest integer refresh_minimum_interval_ticks between8 and UINT32_MAX')
  minimums.append(minimum)
  cap=bundle.get('build_options',{}).get('refresh_age_cap_ticks')
  if type(cap)is not int or not minimum<=cap<=2**32-1:
   raise ValueError('Every supplied bundle must attest integer refresh_age_cap_ticks at least its minimum and at most UINT32_MAX')
  caps.append(cap)
 if not thresholds or len(set(thresholds))!=1:
  raise ValueError('Supplied bundle model refresh thresholds must agree')
 if len(set(minimums))!=1:raise ValueError('Supplied bundle warm minimum intervals must agree')
 if len(set(caps))!=1:raise ValueError('Supplied bundle demanded age caps must agree')
 return dict(ADAPTIVE_POLICY,threshold_milli=thresholds[0],forced_age_cap=caps[0],minimum_interval=minimums[0])

ROUTING = {
 'remote-only': dict(small=32,middle=224,large=0,description='Pharaoh small32; Rig/dev all remaining224; Mac paused'),
 'with-mac': dict(small=32,middle=192,large=32,description='Pharaoh small32; Rig/dev middle192; Mac largest32'),
}

def exploration_intervals(text,minimum):
 """Freeze only strata the attested executable can actually admit."""
 if minimum>65536:raise ValueError('Bundle minimum interval exceeds exploration maximum 65536')
 if text is None:
  return sorted({minimum,*[v for v in (8,16,32,64,128,256,512,1024,4096) if v>=minimum]})
 tokens=text.split(',')
 if any(not re.fullmatch(r'[0-9]+',v) for v in tokens):
  raise ValueError('Exploration intervals must be comma-separated integers')
 values=[int(v) for v in tokens]
 if any(not minimum<=v<=65536 for v in values):
  raise ValueError(f'Exploration intervals must be between the bundle minimum {minimum} and 65536')
 if len(set(values))!=len(values):raise ValueError('Exploration intervals must be unique')
 return values

def pool_for_rank(rank,routing):
 if routing not in ROUTING:raise ValueError('Unknown pre-outcome execution routing')
 return 'small-pharaoh' if rank<32 else 'large-mac' if routing=='with-mac' and rank>=224 else 'middle-linux'

def balanced_roles(specs,routing,seed):
 """Exact format/size-pool halves, chosen before map-generation outcomes.

 Each format has32 slots, so it has zero or two odd pool strata. Choosing which
 odd stratum gets the extra exploration slot has at most2**8 possibilities.
 """
 ranked=sorted(specs,key=lambda s:(s['width']*s['height']*s['players'],digest(s)))
 pools={digest(s)[:16]:pool_for_rank(i,routing) for i,s in enumerate(ranked)}
 formats=sorted({s['format'] for s in specs});strata={}
 for fmt in formats:
  for pool in sorted(set(pools.values())):
   strata[fmt,pool]=sorted([s for s in specs if s['format']==fmt and pools[digest(s)[:16]]==pool],key=lambda s:(s['width']*s['height']*s['players'],digest(s)))
 targets={pool:sum(value==pool for value in pools.values())//2 for pool in set(pools.values())}
 base={pool:sum(len(strata[fmt,pool])//2 for fmt in formats) for pool in targets}
 choices=[]
 for fmt in formats:
  if sum(len(strata[fmt,pool]) for pool in targets)!=32:raise ValueError('Each format must contain32 slots')
  odd=[pool for pool in sorted(targets) if len(strata[fmt,pool])%2]
  options=list(itertools.combinations(odd,len(odd)//2))
  choices.append(sorted(options,key=lambda extra:digest(dict(seed=seed,format=fmt,extra=extra))))
 selected=None
 for options in itertools.product(*choices):
  count=collections.Counter(pool for extra in options for pool in extra)
  if all(base[pool]+count[pool]==targets[pool] for pool in targets):selected=dict(zip(formats,options));break
 if selected is None:raise ValueError('Unable to balance format and execution-pool roles')
 roles={}
 for (fmt,pool),group in strata.items():
  offset=(0 if pool in selected[fmt] else 1) if len(group)%2 else int(digest(dict(seed=seed,format=fmt,pool=pool))[:2],16)%2
  for i,spec in enumerate(group):roles[digest(spec)[:16]]='exploration' if (i+offset)%2==0 else 'candidate'
 return roles,pools

def validate_plan(document):
 unified=document.get('unified',{});routing=unified.get('routing',{})
 mode=routing.get('mode')
 if mode not in ROUTING or routing!={'mode':mode,**ROUTING[mode],'order':'descending area*players,then planned group within each pool'}:raise ValueError('Unknown or changed frozen routing provenance')
 slots=unified.get('slots',[])
 if len(slots)!=256 or len({r['group'] for r in slots})!=256:raise ValueError('Frozen256 unique slots required')
 if unified.get('role_assignment')!='format-pool-balanced-v2':raise ValueError('Exact pre-outcome format/pool role rule required')
 roles,pools=balanced_roles([r['spec'] for r in slots],mode,document['design']['seed'])
 for row in slots:
  group=digest(row['spec'])[:16]
  if row['group']!=group or row['pool']!=pools[group]:raise ValueError('Slot differs from pre-outcome routing')
  if row['role']!=roles[group]:raise ValueError('Pre-outcome role assignment changed')
 if unified.get('contract')!=collector.CONTRACT:raise ValueError('Exact unified contract required')
 if (unified.get('ticks'),unified.get('timeout_seconds'),unified.get('process_attempts'))!=(90000,1800,1):raise ValueError('Frozen collection limits changed')
 for role in ('exploration','candidate'):
  chosen=[r for r in slots if r['role']==role]
  if len(chosen)!=128 or len(collections.Counter(r['spec']['format'] for r in chosen))!=8 or set(collections.Counter(r['spec']['format'] for r in chosen).values())!={16}:raise ValueError('128 slots and16 performat required for each role')
 return mode

def plan(catalog,seed,routing='remote-only'):
 if routing not in ROUTING:raise ValueError('Unknown pre-outcome execution routing')
 value=sample_plan(catalog,256,seed);roles,pools=balanced_roles(value['games'],routing,seed)
 ranked=sorted(value['games'],key=lambda s:(s['width']*s['height']*s['players'],digest(s)))
 slots=[dict(group=digest(s)[:16],role=roles[digest(s)[:16]],pool=pools[digest(s)[:16]],spec=s) for s in ranked]
 value['unified']=dict(contract=collector.CONTRACT,role_assignment='format-pool-balanced-v2',assignment='Exact half of each format and each size pool; bounded seeded odd-stratum allocation, size/hash alternation within strata; before outcomes',routing=dict(mode=routing,**ROUTING[routing],order='descending area*players,then planned group within each pool'),ticks=90000,timeout_seconds=1800,process_attempts=1,slots=slots)
 validate_plan(value)
 return value

def freeze_splits(maps,registry):
 """Append map-only assignments before admission, preserving every old exclusion."""
 import hashlib
 path=Path(registry)
 if not path.exists():raise ValueError('Start from the persistent registry containing prior held-out exclusions')
 value=json.loads(path.read_text())
 if value.get('schema')!=1 or value.get('strategy')!='map-hash-713-v1':raise ValueError('Unknown persistent split registry')
 assignments=value['assignments']
 if not isinstance(assignments,dict) or any(not isinstance(k,str) or type(v)is not int or v not in (-1,0,1,2) for k,v in assignments.items()):raise ValueError('Invalid persistent map assignments')
 records=maps.get('maps',maps) if isinstance(maps,dict) else maps
 for record in records:
  group=map_identity(record['spec'])
  if group not in assignments:
   code=int(hashlib.sha256(('resource-gradient-split-713:'+group).encode()).hexdigest(),16)
   assignments[group]=-1 if code%4==0 else (code//4)%3
 atomic_json(path,value)
 return dict(registry=str(path),sha256=d.file_hash(path),maps=len(assignments),prior_assignments_preserved=True)

def accepted_routing(document,maps):
 """Mechanical post-generation setup amendment; never changes roles or maps."""
 mode=validate_plan(document);records=maps.get('maps',maps) if isinstance(maps,dict) else maps
 slots={r['group']:r for r in document['unified']['slots']};index={r['group']:r for r in records}
 if len(index)!=256 or len(records)!=256 or set(index)!=set(slots):raise ValueError('Actual routing requires every unique frozen planned slot')
 ranked=sorted(records,key=lambda r:(r['spec']['width']*r['spec']['height']*r['spec']['players'],r['group']));assignment=[]
 for i,record in enumerate(ranked):
  slot=slots[record['group']];actual=record['spec'];planned=slot['spec']
  base_spec={k:v for k,v in planned.items() if k!='map_candidates'}
  if actual not in [base_spec,*[dict(base_spec,**v) for v in planned.get('map_candidates',[])]]:raise ValueError('Actual map was not a frozen planned candidate')
  if record.get('planned_spec_sha256',digest(planned))!=digest(planned):raise ValueError('Accepted planned-slot provenance changed')
  for key in ('format','players','alliances','ais','ai','game_seed'):
   if actual.get(key)!=planned.get(key):raise ValueError('Accepted routing cannot change gameplay identity')
  attempts=record.get('generation_attempts',[])
  assignment.append(dict(group=record['group'],role=slot['role'],original_pool=slot['pool'],actual_pool=pool_for_rank(i,mode),planned_dimensions=[planned['width'],planned['height']],actual_dimensions=[actual['width'],actual['height']],actual_size=actual['width']*actual['height']*actual['players'],map_sha256=record['sha256'],actual_spec_sha256=digest(actual),generation_attempts_sha256=digest(attempts),fallback_rejections=sum(r['returncode']!=0 for r in attempts),fallback_reason='predeclared-generation-rejection' if len(attempts)>1 else 'none'))
 return dict(schema=1,rule='actual-area*players-then-frozen-planned-group-v1',phase='post-generation-before-any-simulation-outcomes',routing_mode=mode,planned_plan_sha256=digest(document),accepted_maps_sha256=digest(records),roles_maps_seeds_unchanged=True,role_pools={role:dict(collections.Counter(r['actual_pool'] for r in assignment if r['role']==role)) for role in ('exploration','candidate')},assignments=assignment)

def prepare(a):
 if not 0<=a.seed<=2**32-1:raise ValueError('Bootstrap assignment seed must be uint32')
 document=json.loads(Path(a.plan).read_text());validate_plan(document);slots=[r for r in document['unified']['slots'] if r['role']==a.role]
 if document['unified']['contract']!=collector.CONTRACT or len(slots)!=128 or set(collections.Counter(r['spec']['format'] for r in slots).values())!={16}:raise ValueError('Frozen exact unified256plan required')
 if not Path(a.split_registry).exists():raise ValueError('Freeze the persistent split registry before preparing collection')
 registry=json.loads(Path(a.split_registry).read_text())['assignments']
 maps=json.loads(Path(a.maps).read_text());amendment=json.loads(Path(a.routing_amendment).read_text()) if a.routing_amendment else None
 if amendment is not None and amendment!=accepted_routing(document,maps):raise ValueError('Actual-size routing amendment differs from frozen content/maps/roles')
 effective={r['group']:r for r in amendment['assignments']} if amendment else {}
 maps=maps.get('maps',maps) if isinstance(maps,dict) else maps;index={r['group']:r for r in maps};supplied=[inspect_bundle(p) for p in a.bundle];modelhash={b['build_options'].get('model_sha256') for b in supplied};headerhash={b['build_options'].get('header_sha256') for b in supplied}
 if len(modelhash)!=1 or len(headerhash)!=1 or None in modelhash|headerhash:raise ValueError('Pin common actual model/header in all bundles')
 if a.role=='candidate' and a.intervals:raise ValueError('Candidate role cannot silently use a fixed exploration override')
 compiled_policy=policy_from_bundles(supplied)
 intervals=exploration_intervals(a.intervals,compiled_policy['minimum_interval']) if a.role=='exploration' else []
 policy=dict(**compiled_policy,name='all-family-fixed-interval-bootstrap' if a.role=='exploration' else 'unified-adaptive',model_sha256=next(iter(modelhash)),seed=a.seed,intervals=intervals)
 if a.role=='exploration' and (not policy['intervals'] or 0 in policy['intervals']):raise ValueError('Bootstrap requires explicit nonzero all-family intervals; no legacy adaptive0 fallback')
 if a.role=='candidate':
  for b in supplied:
   if not b['build_options'].get('unified_model_trained'):raise ValueError('An untrained placeholder cannot launch a candidate cohort')
 cohort=dict(id=a.cohort,role=a.role,policy=policy,header_sha256=next(iter(headerhash)),contract=collector.CONTRACT)
 selected=[]
 for row in slots:
  item=copy.deepcopy(index[row['group']]);item['cohort']=cohort
  if map_identity(item['spec']) not in registry:raise ValueError('Map split was not frozen before collection')
  pool=effective[row['group']]['actual_pool'] if effective else row['pool']
  b=next((b for b in supplied if b['build_options'].get('execution_pool')==pool),None)
  if not b:raise ValueError('Missing predeclared pool bundle')
  item['build']=b['id'];selected.append(item)
 subplan=dict(games=[r['spec'] for r in sorted(slots,key=lambda r:(-index[r['group']]['spec']['width']*index[r['group']]['spec']['height']*index[r['group']]['spec']['players'],r['group']))])
 manifest=make_experiment(subplan,selected,supplied,cohort,ticks=90000,timeout_seconds=1800);manifest['settings']['process_attempts']=1
 manifest['unified_routing']=copy.deepcopy(document['unified']['routing'])
 if amendment is not None:manifest['unified_actual_routing']=copy.deepcopy(amendment)
 for job in manifest['jobs']:
  job['labels']['resource_gradient']['persistent_split_registry_sha256']=d.file_hash(a.split_registry)
  job['labels']['resource_gradient']['routing']=copy.deepcopy(document['unified']['routing'])
  if amendment is not None:
   job['labels']['resource_gradient']['actual_routing']=copy.deepcopy(effective[job['id']])
   job['labels']['resource_gradient']['actual_routing_sha256']=digest(amendment)
 for record in selected:
  if store_artifact(record['path'],Path(a.output)/'artifacts',compress=False)['sha256']!=record['sha256']:raise ValueError('Frozen map changed')
 c=Coordinator.submit(a.output,manifest,a.bundle)
 try:return c.status()
 finally:c.close()


def main():
 p=argparse.ArgumentParser();sub=p.add_subparsers(dest='command',required=True);q=sub.add_parser('plan');q.add_argument('--catalog',required=True);q.add_argument('--seed',type=int,required=True);q.add_argument('--routing',choices=sorted(ROUTING),default='remote-only');q.add_argument('--output',required=True)
 q=sub.add_parser('prepare');q.add_argument('--plan',required=True);q.add_argument('--split-registry',required=True);q.add_argument('--maps',required=True);q.add_argument('--bundle',action='append',required=True);q.add_argument('--routing-amendment');q.add_argument('--role',choices=['exploration','candidate'],required=True);q.add_argument('--cohort',required=True);q.add_argument('--seed',type=int,default=181101);q.add_argument('--intervals');q.add_argument('--output',required=True)
 q=sub.add_parser('route-accepted');q.add_argument('--plan',required=True);q.add_argument('--maps',required=True);q.add_argument('--output',required=True)
 q=sub.add_parser('freeze-splits');q.add_argument('--maps',required=True);q.add_argument('--split-registry',required=True)
 a=p.parse_args()
 if a.command=='route-accepted':
  value=accepted_routing(json.loads(Path(a.plan).read_text()),json.loads(Path(a.maps).read_text()));target=Path(a.output)
  if target.exists() and json.loads(target.read_text())!=value:raise ValueError('Refusing to replace frozen routing amendment')
  atomic_json(target,value)
 elif a.command=='freeze-splits':print(json.dumps(freeze_splits(json.loads(Path(a.maps).read_text()),a.split_registry),indent=2))
 elif a.command=='plan':atomic_json(a.output,plan(json.loads(Path(a.catalog).read_text()),a.seed,a.routing))
 else:print(json.dumps(prepare(a),indent=2))
if __name__=='__main__':main()
