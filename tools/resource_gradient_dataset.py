#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Immutable collection cohorts and policy-independent support diagnostics.

A cohort is a sealed manifest wrapper: schema=1, cohort={id, policy, contract},
records=[...], content_sha256=<canonical cohort-and-records hash>. Legacy list manifests
remain readable, but their unknown label contract cannot silently mix with a
specified contract. A planning oracle is a separate target, never a movement label.
"""
from __future__ import annotations
import collections
import csv
import gzip
import hashlib
import json
import os
from pathlib import Path

FEATURES = ('age', 'q32', 'q128', 'source_added', 'source_removed',
            'source_added_frac_q12', 'source_removed_frac_q12')


def canonical_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def file_hash(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()


def manifest_records(path):
    document = json.loads(Path(path).read_text())
    if isinstance(document, list): return document, None
    if document.get('schema') != 1 or not isinstance(document.get('records'), list):
        raise ValueError(f'Unsupported cohort manifest: {path}')
    if canonical_hash({'cohort': document.get('cohort'), 'records': document['records']}) != document.get('content_sha256'):
        raise ValueError(f'Cohort records hash mismatch: {path}')
    metadata = document.get('cohort', {})
    if not metadata.get('id') or not metadata.get('policy') or not metadata.get('contract'):
        raise ValueError('Sealed cohort requires id, policy and contract')
    return document['records'], metadata


def seal(manifest, output, cohort):
    """Seal without overwriting evidence, preserving relative episode paths."""
    source = Path(manifest).resolve(); destination = Path(output).resolve()
    records, existing = manifest_records(source)
    if existing: raise ValueError('Cohort is already sealed')
    if any(r.get('cohort',cohort)!=cohort for r in records):
        raise ValueError('Record cohort conflicts with sealed cohort')
    records = [dict(r, episodes=os.path.relpath((source.parent / r['episodes']).resolve(), destination.parent)) for r in records]
    value = dict(schema=1, cohort=cohort, records=records, content_sha256=canonical_hash({'cohort':cohort, 'records':records}))
    if destination.exists():
        if json.loads(destination.read_text()) != value: raise ValueError('Refusing to replace immutable cohort')
        return value
    # Exclusive creation protects two collectors sealing the same destination.
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('x') as stream: json.dump(value, stream, indent=2); stream.write('\n')
    manifest_records(destination)
    return value


def load(manifests):
    from tools.resource_gradient_tournament import map_identity
    rows = []; seen = {}; cohorts = {}; contracts = set()
    for path in manifests:
        records, enclosing = manifest_records(path)
        for record in records:
            p = Path(record['episodes'])
            if not p.is_absolute(): p = Path(path).resolve().parent / p
            digest = file_hash(p)
            if digest != record['episode_sha256']: raise ValueError(f'Dataset hash mismatch: {p}')
            if enclosing and record.get('cohort',enclosing)!=enclosing:
                raise ValueError('Record cohort conflicts with sealed cohort')
            metadata = enclosing or record.get('cohort')
            if metadata:
                cohort = metadata['id']; policy = metadata['policy']; contract = metadata['contract']
                role=metadata.get('role','unspecified')
                if role not in ('exploration','candidate','unspecified'):raise ValueError('Unknown cohort role')
                if tuple(contract.get('features', ())) != FEATURES:
                    raise ValueError('Dataset feature contract mismatch')
                if contract.get('benefit_unit') != 'direction-cost' or not isinstance(contract.get('future_offsets'), list) or len(contract['future_offsets']) != 2 or any(type(v) is not int for v in contract['future_offsets']) or not 0 <= contract['future_offsets'][0] <= contract['future_offsets'][1]:
                    raise ValueError('Incomplete movement benefit/horizon contract')
                if contract.get('request_type') != 'movement' or contract.get('label') != 'movement-direction-extra-cost':
                    raise ValueError('Only movement labels can fit the movement severity model')
            else:
                cohort = 'legacy:' + record['binary_sha256']; policy = {'name': 'legacy-unspecified'}; role='unspecified'
                contract = {'label': 'legacy-unspecified', 'request_type': 'movement'}
            declaration = dict(policy=policy, contract=contract,role=role)
            if cohort in cohorts and cohorts[cohort] != declaration: raise ValueError('Cohort identity reused with different policy/contract')
            cohorts[cohort] = declaration; contracts.add(canonical_hash(contract))
            identity = (record['group'], record['binary_sha256'], digest)
            if identity in seen:
                if seen[identity] != cohort: raise ValueError('Same collection assigned to multiple cohorts')
                continue
            seen[identity] = cohort
            spec = record.get('spec', {}); group = record.get('map_group')
            if not group and all(k in spec for k in ['generator', 'players', 'map_seed']) and ('size' in spec or {'width', 'height'} <= spec.keys()):
                group = map_identity(spec)
            with (gzip.open(p, 'rt') if p.suffix == '.gz' else p.open()) as stream:
                reader = csv.DictReader(stream)
                if not {'complete', 'defer_extra', 'refresh_extra', *FEATURES} <= set(reader.fieldnames or ()):
                    raise ValueError(f'Missing movement episode columns: {p}')
                for line in reader:
                    if int(line['complete']) != 1: continue
                    row = {k: int(v) for k, v in line.items()}
                    # Future zero queries are a valid zero benefit for movement-origin
                    # fields. Only explicit planning-only origin lacks this target.
                    row['movement_label_available'] = int(row.get('movement_observed', 1) > 0)
                    if any(row[k] < 0 for k in FEATURES): raise ValueError('Negative feature value')
                    row.update(group=group or record['group'], format=record['format'], collection='|'.join(identity),
                               cohort=cohort, cohort_role=role, policy=json.dumps(policy, sort_keys=True), request_type='movement',
                               label_contract=canonical_hash(contract), generator=spec.get('generator', 'unknown'),
                               dimensions=f"{spec.get('width', spec.get('size', '?'))}x{spec.get('height', spec.get('size', '?'))}")
                    rows.append(row)
    if not rows: raise ValueError('No complete episodes')
    if len(contracts) != 1: raise ValueError('Incompatible label contracts; do not pool unknown legacy and specified cohorts')
    return rows


def balanced_weights(rows):
    """Equal format, role, cohort, map, collection, then episode.

    The permanent exploration role keeps its share even after many candidate
    generations. Map identity remains independent of role/cohort for splits.
    Missing historical roles remain explicitly unspecified rather than inferred.
    """
    import numpy as np
    def key(row):return (row['format'],row.get('cohort_role','unspecified'),row.get('cohort','legacy'),row['group'],row['collection'])
    count=collections.Counter(key(r) for r in rows)
    roles=collections.defaultdict(set);cohorts=collections.defaultdict(set)
    maps=collections.defaultdict(set);games=collections.defaultdict(set)
    declarations={}
    for fmt,role,cohort,group,collection in count:
        if cohort in declarations and declarations[cohort]!=role:raise ValueError('Cohort role changed')
        declarations[cohort]=role
        roles[fmt].add(role);cohorts[fmt,role].add(cohort)
        maps[fmt,role,cohort].add(group);games[fmt,role,cohort,group].add(collection)
    w=np.array([1/(len(roles[fmt])*len(cohorts[fmt,role])*len(maps[fmt,role,cohort])*
                   len(games[fmt,role,cohort,group])*count[fmt,role,cohort,group,collection])
                for fmt,role,cohort,group,collection in map(key,rows)])
    return w*len(w)/w.sum()


def bucket(value, boundaries):
    lower=0
    for boundary in boundaries:
        if value < boundary: return f'{lower}..{boundary-1}'
        lower=boundary
    return f'>={boundaries[-1]}'


def support_audit(rows):
    age_edges=(32,64,128,256,512,1024,4096);demand_edges=(1,8,32,128)
    def counts(subset):
        joint = collections.Counter((bucket(r['age'], age_edges), bucket(r['q128'], demand_edges)) for r in subset)
        for age in [bucket(v,age_edges) for v in (0,*age_edges)]:
            for demand in [bucket(v,demand_edges) for v in (0,*demand_edges)]:
                joint.setdefault((age,demand),0)
        return dict(episodes=len(subset), maps=len({r['group'] for r in subset}),
                    age_demand=[dict(age=a, q128=q, episodes=n) for (a,q),n in sorted(joint.items())],
                    high_demand_old=sum(r['age'] > 256 and r['q128'] >= 8 for r in subset),
                    maximum_age=max((r['age'] for r in subset),default=None),
                    movement_label_episodes=sum(r.get('movement_label_available',1) for r in subset),
                    behavior_intervals=dict(sorted(collections.Counter(str(r.get('behavior_interval','unknown')) for r in subset).items())))
    cohorts = collections.defaultdict(list)
    roles=collections.defaultdict(list)
    for row in rows:roles[row.get('cohort_role','unspecified')].append(row)
    for row in rows: cohorts[row.get('cohort', 'legacy')].append(row)
    planning=[r for r in rows if 'planning_queries' in r]
    planning_report=dict(measured=bool(planning),episodes_with_instrumentation=len(planning),
        episodes_with_planning_queries=sum(r.get('planning_queries',0)>0 for r in planning),
        planning_queries=sum(r.get('planning_queries',0) for r in planning),
        planning_only_episodes=sum(r.get('planning_queries',0)>0 and not r.get('movement_label_available',1) for r in planning),
        defer_false_negative=sum(r.get('defer_planning_false_negative',0) for r in planning),
        defer_false_positive=sum(r.get('defer_planning_false_positive',0) for r in planning),
        refresh_false_negative=sum(r.get('refresh_planning_false_negative',0) for r in planning),
        refresh_false_positive=sum(r.get('refresh_planning_false_positive',0) for r in planning),
        note='Separate reachability labels; not converted to movement benefit. Legacy episodes lack this instrumentation.')
    return dict(schema=1, overall=counts(rows),
                movement_population=counts([r for r in rows if r.get('movement_label_available',1)]),
                planning_population=counts([r for r in rows if r.get('planning_queries',0)>0]),
                cohorts={k:counts(v) for k,v in sorted(cohorts.items())},
                roles={k:counts(v) for k,v in sorted(roles.items())},
                policies=sorted({r.get('policy', 'legacy-unspecified') for r in rows}),
                label_contracts=sorted({r.get('label_contract', 'legacy-unspecified') for r in rows}),
                planning_coverage=planning_report,
                warning='Observed support is not proof of safety beyond these bins; offline accuracy does not establish closed-loop improvement.')


def audit_cli(args):
    report = support_audit(load(args.manifest))
    text = json.dumps(report, indent=2) + '\n'
    if getattr(args, 'output', None): Path(args.output).write_text(text)
    print(text, end='')


def registered_split(rows, path, initial_split):
    """Freeze map assignments across aggregation rounds, before inspecting labels.

    Existing maps never migrate from holdout into fitting. New maps use a fixed
    hash assignment; reject insufficient coverage rather than searching seeds
    against outcomes. Unused assignments remain to protect later reintroduced maps.
    """
    import os
    import tempfile
    path=Path(path)
    if path.exists():
        registry=json.loads(path.read_text())
        if registry.get('schema')!=1 or registry.get('strategy')!='map-hash-713-v1' or not isinstance(registry.get('assignments'),dict):
            raise ValueError('Inconsistent split registry schema/strategy')
        assignments=dict(registry['assignments'])
        if any(not isinstance(group,str) or type(value) is not int or value not in (-1,0,1,2) for group,value in assignments.items()):
            raise ValueError('Inconsistent split registry assignments')
        for group in sorted({r['group'] for r in rows}-assignments.keys()):
            code=int(hashlib.sha256(('resource-gradient-split-713:'+group).encode()).hexdigest(),16)
            assignments[group]=-1 if code%4==0 else (code//4)%3
    else:
        held,folds,_=initial_split(rows)
        assignments={group:-1 if group in held else folds[group] for group in sorted({r['group'] for r in rows})}
    formats=sorted({r['format'] for r in rows});current={r['group'] for r in rows}
    held={g for g in current if assignments[g]==-1};folds={g:assignments[g] for g in current-held}
    def counts(groups):return {fmt:len({r['group'] for r in rows if r['format']==fmt and r['group'] in groups}) for fmt in formats}
    dev_counts=counts(current-held);held_counts=counts(held);coverage=[]
    if not formats or min(dev_counts.values())<3 or min(held_counts.values())<1:
        raise ValueError('Frozen split registry lacks development/holdout format coverage; collect more maps')
    for fold in range(3):
        valid={g for g in current-held if folds[g]==fold};train=current-held-valid
        tc,vc=counts(train),counts(valid)
        if min(tc.values())<2 or min(vc.values())<1:
            raise ValueError('Frozen split registry lacks cross-validation format coverage; collect more maps')
        coverage.append(dict(training_maps=tc,validation_maps=vc))
    value=dict(schema=1,strategy='map-hash-713-v1',assignments=assignments)
    path.parent.mkdir(parents=True,exist_ok=True)
    # Do not touch the registry when coverage validation fails. Replace only a
    # complete JSON document, so interruption cannot leave partial assignments.
    with tempfile.NamedTemporaryFile(mode='w',dir=path.parent,prefix=path.name+'.',delete=False) as stream:
        temporary=Path(stream.name);json.dump(value,stream,indent=2,sort_keys=True);stream.write('\n');stream.flush();os.fsync(stream.fileno())
    try:temporary.replace(path)
    finally:temporary.unlink(missing_ok=True)
    return held,folds,dict(development_maps=dev_counts,holdout_maps=held_counts,folds=coverage,
        registry=dict(name=path.name,sha256=file_hash(path),strategy=value['strategy']))
