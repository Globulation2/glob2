#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare and export resource-gradient collections using the shared coordinator.

No SSH scheduler lives here. Jobs pin one supplied architecture-specific bundle;
weighted, format-stratified allocation chooses that bundle before any outcomes.
The coordinator provides leases, retries, host eligibility and artifact receipts.
"""
from __future__ import annotations
import argparse
import collections
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from tools.resource_gradient_dataset import FEATURES, canonical_hash, file_hash, seal
from tools import adaptive_gradient_collection as collector
from tools.resource_gradient_tournament import digest, map_identity
from tools.resource_gradient_experiments import exploration_seed_for_game
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.common import atomic_json, store_artifact, hash_id
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job, validate_experiment
from tools.tournaments.results import Results


def _records(value):return value['games'] if isinstance(value,dict) else value


def make_experiment(plan, maps, bundles, cohort, build_weights=None, ticks=90000, timeout_seconds=7200):
    """maps: [{group: original planned hash[:16], spec: accepted spec, path, sha256}]."""
    specs=_records(plan);maps=maps.get('maps',maps) if isinstance(maps,dict) else maps
    if not specs or len({digest(s) for s in specs})!=len(specs):raise ValueError('Nonempty unique planned games required')
    if type(ticks) is not int or ticks<1:raise ValueError('Positive tick cap required')
    if type(timeout_seconds) is not int or timeout_seconds<1:raise ValueError('Positive game timeout required')
    if not bundles:raise ValueError('At least one supplied build required')
    versions={canonical_hash(b['capabilities'].get('sim_version')) for b in bundles}
    if len(versions)!=1 or any(not b['capabilities'].get('sim_version') for b in bundles):raise ValueError('Supplied bundles must have identical explicit simulation versions')
    if len({(b['source_revision'],b.get('dirty_identity')) for b in bundles})!=1:raise ValueError('Supplied bundles must describe identical source')
    by_build={b['id']:b for b in bundles}
    if len(by_build)!=len(bundles):raise ValueError('Duplicate supplied build')
    weights=build_weights or {key:1 for key in by_build}
    if set(weights)!=set(by_build) or any(type(v) is not int or v<1 for v in weights.values()):raise ValueError('Every build needs a positive integer weight')
    # Smooth weighted round robin resets per format, giving exact quotas whenever
    # format counts are multiples of the total weight (e.g. 28 Linux / 4 Mac).
    credits=collections.defaultdict(lambda:{key:0 for key in sorted(weights)})
    index={r['group']:r for r in maps}
    if len(index)!=len(maps):raise ValueError('Duplicate accepted-map group')
    expected={digest(s)[:16] for s in specs}
    if set(index)!=expected:raise ValueError('Accepted map index must match every planned slot exactly')
    jobs=[]
    for ordinal,planned in enumerate(specs):
        group=digest(planned)[:16];accepted=index[group];actual=accepted['spec'];path=Path(accepted['path'])
        if accepted.get('planned_spec_sha256',digest(planned))!=digest(planned):raise ValueError('Planned map provenance mismatch')
        # Map fallback may change only generation inputs, never game setup.
        for key in ('format','players','alliances','ais','ai','game_seed'):
            if actual.get(key)!=planned.get(key):raise ValueError(f'Accepted map changed gameplay identity: {key}')
        if file_hash(path)!=accepted['sha256']:raise ValueError('Accepted map bytes changed')
        metadata=accepted.get('cohort',cohort)
        if not metadata.get('id') or metadata.get('role') not in ('exploration','candidate'):raise ValueError('Explicit cohort id and role required')
        contract=metadata.get('contract',{})
        from tools.adaptive_gradient_dataset import collection_threshold
        unified=collector.compatible_contract(contract)
        if unified and collector.contract_id(contract)!=collector.CONTRACT_ID:raise ValueError('Historical collector contracts are read-only; new jobs require current v4')
        if unified and any(b.get('build_options',{}).get('adaptive_gradient_contract_sha256')!=canonical_hash(collector.CONTRACT) for b in bundles):raise ValueError('Supplied bundle does not pin unified collector contract')
        if not unified and (tuple(contract.get('features',()))!=FEATURES or contract.get('label')!='movement-direction-extra-cost'
            or contract.get('future_offsets')!=[8,31] or contract.get('benefit_unit')!='direction-cost'
            or contract.get('request_type')!='movement'):
            raise ValueError('Collection requires the current eight-tick movement label contract')
        if 'contradictions' in contract:
            from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT
            if contract['contradictions'] != CONTRADICTION_CONTRACT:
                raise ValueError('Unknown contradiction measurement contract')
            # Explicit frozen build metadata prevents an old collector bundle
            # being accidentally relabelled as the augmented collector.
            for supplied in bundles:
                if supplied.get('build_options',{}).get('contradiction_contract_sha256') != canonical_hash(CONTRADICTION_CONTRACT):
                    raise ValueError('Supplied bundle does not pin the contradiction contract')
        from tools.adaptive_gradient_dataset import declared_threshold_milli
        actual_threshold=collection_threshold(metadata)
        if unified and actual_threshold is None:raise ValueError('Explicit unified collection threshold required')
        age_cap=metadata.get('policy',{}).get('forced_age_cap')
        if age_cap is not None:
            if type(age_cap)is not int or not 8<=age_cap<=2**32-1:
                raise ValueError('Explicit collection age cap must be an integer between8 and UINT32_MAX')
            for supplied in bundles:
                attested=supplied.get('build_options',{}).get('refresh_age_cap_ticks')
                if type(attested)is not int or not 8<=attested<=2**32-1:
                    raise ValueError('Capped collection requires explicit bundle refresh_age_cap_ticks')
                if attested!=age_cap:raise ValueError('Bundle refresh age cap differs from collection behavior')
        for supplied in bundles:
            options=supplied.get('build_options',{})
            # Historical bundles predate this attestation. Keep them usable for
            # comparisons, but never allow explicit model metadata to contradict
            # the cohort's claimed behavior, even through the low-level API.
            if 'refresh_threshold_milli' not in options:continue
            attested=declared_threshold_milli(options['refresh_threshold_milli'])
            if actual_threshold is None:raise ValueError('Explicit collection threshold required for attested bundle')
            if attested!=actual_threshold:raise ValueError('Bundle refresh threshold differs from collection behavior')
        policy=metadata['policy'];intervals=policy.get('intervals',[])
        if 'model_sha256' not in policy or 'header_sha256' not in metadata:
            raise ValueError('Cohort must pin explicit model_sha256 and header_sha256')
        model_hash=hash_id(policy['model_sha256']);header_hash=hash_id(metadata['header_sha256'])
        for supplied in bundles:
            options=supplied.get('build_options',{})
            if options.get('model_sha256')!=model_hash or options.get('header_sha256')!=header_hash:
                raise ValueError('Supplied bundle model/header identity differs from cohort')
        collection={'contract':collector.CONTRACT_ID} if unified else {}
        if any(intervals):
            collection.update(exploration=','.join(map(str,intervals)),
                        seed=accepted.get('collection_seed',exploration_seed_for_game(policy.get('seed',0),group)))
        if 'build' in accepted:
            # Optional predeclared execution pool: identical binaries may have
            # separate bundle identities/host allowlists for large-map work.
            # This never drops a slot or chooses a pool from game outcomes.
            build=accepted['build']
            if build not in by_build:raise ValueError('Accepted map pins an unknown supplied build')
        else:
            fmt=actual['format'];current=credits[fmt]
            for key in weights:current[key]+=weights[key]
            build=max(sorted(weights),key=lambda key:current[key]);current[build]-=sum(weights.values())
        if by_build[build]['platform']['os']=='darwin':collection['threads']=1
        from tools.tournaments.jobs import resource_gradient_collection_outputs
        outputs=resource_gradient_collection_outputs(collection)
        if 'contradictions' in contract:
            # Keep non-overlapping whole-game rates as well as the overlapping
            # since-capture histories used by episode features.
            outputs['gradient-contradictions.csv']={'epoch','key','empty_goal',
                'persistent_progress_blocked','unit_progress_blocked','sidestep_used',
                'reachable_no_move','movement_queries'}
        roster=actual.get('ais') or [actual.get('ai','Nicowar')]*actual['players']
        config=dict(players=roster,alliances=actual['alliances'],ticks=ticks,resource_gradient_collection=collection)
        labels=dict(resource_gradient=dict(logical_group=group,ordinal=ordinal,planned_spec=planned,
                    actual_spec=actual,cohort=metadata,map_sha256=accepted['sha256'],
                    collection_seed=collection.get('seed'),generation=accepted.get('generation_attempts'),
                    model_sha256=model_hash,header_sha256=header_hash))
        jobs.append(job('game',build,inputs={'map':{'sha256':accepted['sha256'],'encoding':'identity','bytes':path.stat().st_size,
                            'raw_sha256':accepted['sha256'],'raw_bytes':path.stat().st_size,'path':'map.map.gz' if path.name.endswith('.gz') else 'map.map'}},
                        seeds={'game':actual['game_seed']},config=config,
                        outputs={'result':'full','required':list(outputs)},labels=labels,
                        limits={'timeout_seconds':timeout_seconds,'estimated_seconds':900},job_id=group))
    manifest=dict(schema_version=1,id='gradient-'+digest(dict(jobs=jobs))[:24],jobs=jobs,
                  settings={'prefetch':0,'measured':False},
                  resource_gradient=dict(schema=1,plan_sha256=digest(plan),build_weights=weights,
                    partition='explicit accepted-map build when present; otherwise smooth weighted round robin within format before outcomes',
                    sim_version=bundles[0]['capabilities']['sim_version']))
    return validate_experiment(manifest)


def prepare(plan, maps, bundle_directories, cohort, directory, build_weights=None, ticks=90000, timeout_seconds=7200):
    bundles=[inspect_bundle(p) for p in bundle_directories]
    experiment=make_experiment(plan,maps,bundles,cohort,build_weights,ticks,timeout_seconds)
    directory=Path(directory)
    for record in (maps.get('maps',maps) if isinstance(maps,dict) else maps):
        artifact=store_artifact(record['path'],directory/'artifacts',compress=False)
        if artifact['sha256']!=record['sha256']:raise ValueError('Imported map identity differs')
    coordinator=Coordinator.submit(directory,experiment,bundle_directories)
    try:return coordinator.status()
    finally:coordinator.close()


def export(directory, output, allow_partial=False):
    """Export accepted successes only, with explicit accounting for every slot."""
    reader=Results(directory);output=Path(output);expected={j['id']:j for j in reader.manifest['jobs']}
    if len(expected)!=len(reader.manifest['jobs']):raise ValueError('Duplicate logical jobs in experiment')
    accepted={};failed=[]
    for record in reader:
        identity=record['job']['id']
        if identity not in expected or record['job']!=expected[identity]:raise ValueError('Result does not match frozen job')
        if identity in accepted:raise ValueError('Duplicate logical result')
        if record.get('accepted') is not True:raise ValueError('Unaccepted result export')
        externally_censored=expected[identity]['labels']['resource_gradient']['cohort']['contract'].get('id') in collector.SUPPORTED_CONTRACT_IDS and any(a['path']=='runtime-cap-override.json' for a in record.get('artifacts',[]))
        if externally_censored or record.get('category')!='success' or record.get('result',{}).get('status')!='completed':
            failed.append(identity);continue
        accepted[identity]=record
    missing=sorted(set(expected)-accepted.keys())
    status=dict(expected=len(expected),completed=len(accepted),missing=missing,failed=failed,
                failed_attempts=sum(r.get('category')!='success' for r in reader.attempts()))
    if missing and not allow_partial:raise ValueError(f'Collection incomplete: {len(accepted)}/{len(expected)} successes; {len(failed)} terminal failures')
    manifests=collections.defaultdict(list);metadata_by_id={}
    for identity,record in accepted.items():
        labels=record['job']['labels']['resource_gradient'];metadata=labels['cohort'];cohort_id=metadata['id']
        if cohort_id in metadata_by_id and metadata_by_id[cohort_id]!=metadata:raise ValueError('Conflicting cohort declarations')
        metadata_by_id[cohort_id]=metadata
        build=inspect_bundle(reader.root/'builds'/record['job']['build'])
        binary=next(f['sha256'] for f in build['files'] if f['path']==build['executable'])
        destination=output/'episodes'/identity;destination.mkdir(parents=True,exist_ok=True)
        artifacts={}
        for name in record['job']['outputs']['required']:
            found=[a for a in record['artifacts'] if a['path']==name]
            if len(found)!=1:raise ValueError(f'Missing/duplicate required artifact: {name}')
            info=found[0];target=destination/(name+'.'+info['sha256']+'.gz')
            # Verify transported bytes even when already gzip-compressed. Keep
            # their exact identity rather than decoding and losing the receipt.
            with reader.open_artifact(record,name,'rb') as stream:
                decoded=hashlib.sha256();decoded_bytes=0
                for block in iter(lambda:stream.read(1024*1024),b''):
                    decoded.update(block);decoded_bytes+=len(block)
                    if type(info.get('raw_bytes')) is not int or decoded_bytes>info['raw_bytes']:
                        raise ValueError('Decoded artifact exceeds receipt raw_bytes')
            if decoded_bytes!=info.get('raw_bytes') or decoded.hexdigest()!=info.get('raw_sha256'):
                raise ValueError('Decoded artifact differs from raw receipt identity')
            if info['encoding']=='gzip':
                if target.exists() and file_hash(target)!=info['sha256']:raise ValueError('Existing immutable export changed')
                if not target.exists():shutil.copyfile(reader.root/'artifacts'/info['sha256'],target)
                if file_hash(target)!=info['sha256']:raise ValueError('Copied artifact hash mismatch')
            elif info['encoding']=='identity':
                if target.exists():
                    with gzip.open(target,'rb') as existing:
                        existing_hash=hashlib.sha256()
                        for block in iter(lambda:existing.read(1024*1024),b''):existing_hash.update(block)
                    if existing_hash.hexdigest()!=decoded.hexdigest():raise ValueError('Existing immutable decoded artifact changed')
                else:
                    with reader.open_artifact(record,name,'rb') as source,target.open('xb') as file:
                        with gzip.GzipFile(filename='',fileobj=file,mode='wb',mtime=0) as compressed:shutil.copyfileobj(source,compressed)
            else:raise ValueError('Unknown encoding')
            artifacts[name]=dict(path=str(target.resolve()),sha256=file_hash(target),decoded_sha256=decoded.hexdigest(),decoded_bytes=decoded_bytes,transport=info)
        episode=artifacts['gradient-episodes.csv']
        if collector.compatible_contract(metadata['contract']):
            policy_result=record.get('result',{}).get('adaptive_gradient_policy',{})
            if policy_result.get('collector_contract')!=collector.contract_id(metadata['contract']):raise ValueError('Runtime did not attest the declared collector contract')
            from tools.adaptive_gradient_dataset import collection_threshold,declared_threshold_milli
            if 'threshold_milli_direction_cost' in policy_result and declared_threshold_milli(policy_result['threshold_milli_direction_cost'])!=collection_threshold(metadata):raise ValueError('Runtime threshold differs from sealed collection behavior')
            from tools.tournaments.jobs import resource_gradient_collection_outputs
            for name,required in resource_gradient_collection_outputs(record['job']['config']['resource_gradient_collection']).items():
                if name not in artifacts:raise ValueError('Missing unified output: '+name)
                with gzip.open(artifacts[name]['path'],'rt') as stream:columns=set(next(csv.reader(stream),[]))
                if not required<=columns:raise ValueError('Unified output schema mismatch: '+name)
        if 'contradictions' in metadata['contract']:
            from tools.resource_gradient_dataset import CONTRADICTION_CONTRACT
            if metadata['contract']['contradictions'] != CONTRADICTION_CONTRACT:
                raise ValueError('Unknown exported contradiction measurement contract')
            with gzip.open(episode['path'],'rt') as stream:
                columns=set(next(csv.reader(stream),[]))
            required=set(CONTRADICTION_CONTRACT['counters'])|{CONTRADICTION_CONTRACT['capture_known']}|set(CONTRADICTION_CONTRACT['future_stall_columns'])
            if not required <= columns:
                raise ValueError('Augmented collector is missing contradiction episode columns')
            totals=artifacts.get('gradient-contradictions.csv')
            if not totals:
                raise ValueError('Augmented collector is missing contradiction totals')
            with gzip.open(totals['path'],'rt') as stream:
                columns=set(next(csv.reader(stream),[]))
            if not {'epoch','key','empty_goal','persistent_progress_blocked','unit_progress_blocked',
                    'sidestep_used','reachable_no_move','movement_queries'}<=columns:
                raise ValueError('Augmented collector is missing contradiction totals columns')
        receipt=destination/(record['id']+'.accepted-attempt.json')
        if receipt.exists() and json.loads(receipt.read_text())!=record:raise ValueError('Accepted attempt receipt changed')
        if not receipt.exists():atomic_json(receipt,record)
        spec=labels['actual_spec']
        manifests[cohort_id,binary].append(dict(group=identity,map_group=map_identity(spec),format=spec['format'],spec=spec,
            episodes=episode['path'],episode_sha256=episode['sha256'],binary_sha256=binary,map_sha256=labels['map_sha256'],
            ticks=record['result']['ticks'],termination=record['result'].get('termination'),cohort=metadata,
            provenance=dict(host=record['host'],attempt=record['id'],bundle=build['id'],source_revision=build['source_revision'],
                sim_version=build['capabilities']['sim_version'],package_id=record['package_id'],
                model_sha256=labels['model_sha256'],header_sha256=labels['header_sha256'],collection_seed=labels['collection_seed'],
                accepted_receipt=str(receipt.resolve()),accepted_receipt_sha256=file_hash(receipt),artifacts=artifacts)))
    sealed=[]
    for (cohort_id,binary),records in sorted(manifests.items()):
        records.sort(key=lambda r:r['group'])
        name=canonical_hash(dict(cohort=cohort_id,binary=binary,records=records))[:24]
        temporary=output/f'{name}.records.json';atomic_json(temporary,records)
        target=output/f'{name}.cohort.json';seal(temporary,target,metadata_by_id[cohort_id]);temporary.unlink()
        if collector.compatible_contract(metadata_by_id[cohort_id]['contract']):collector.load([target])
        sealed.append(str(target.resolve()))
    status.update(manifests=sealed,by_format=dict(collections.Counter(r['job']['labels']['resource_gradient']['actual_spec']['format'] for r in accepted.values())),
                  hosts=dict(collections.Counter(r['host'] for r in accepted.values())))
    atomic_json(output/'export-status.json',status)
    return status


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('prepare');p.add_argument('--plan',required=True);p.add_argument('--maps',required=True)
    p.add_argument('--bundle',action='append',required=True);p.add_argument('--cohort',required=True);p.add_argument('--directory',required=True)
    p.add_argument('--weights',help='JSON file mapping bundle IDs to positive capacity weights');p.add_argument('--ticks',type=int,default=90000)
    p.add_argument('--timeout-seconds',type=int,default=7200,help='Per-game wall timeout, frozen before dispatch; increase for large diagnostic games')
    p=sub.add_parser('collect');p.add_argument('directory');p.add_argument('--hosts',required=True);p.add_argument('--once',action='store_true')
    p=sub.add_parser('export');p.add_argument('directory');p.add_argument('--output',required=True);p.add_argument('--allow-partial',action='store_true')
    args=parser.parse_args();read=lambda p:json.loads(Path(p).read_text())
    if args.command=='prepare':result=prepare(read(args.plan),read(args.maps),args.bundle,read(args.cohort),args.directory,read(args.weights) if args.weights else None,args.ticks,args.timeout_seconds)
    elif args.command=='export':result=export(args.directory,args.output,args.allow_partial)
    else:
        from tools.tournaments.cli import configure_hosts
        coordinator=Coordinator(args.directory)
        try:result=coordinator.run(configure_hosts(read(args.hosts)),once=args.once)
        finally:coordinator.close()
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
