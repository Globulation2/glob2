# SPDX-License-Identifier: GPL-3.0-or-later
"""Paired offline contradiction-feature comparisons; never promotes coefficients.

All variants see the same known-capture movement episodes, role/cohort weights,
map holdout and validation folds. Fractions are integer Q12 event/query ratios;
count and fraction transforms use exactly the portable integer predictor path.
Planning errors and future stalls are separate outcomes, not movement labels.
"""
from __future__ import annotations
import collections
import csv
import gzip
import hashlib
import json
from pathlib import Path
from tools.resource_gradient_dataset import (CONTRADICTION_CONTRACT, CONTRADICTION_COUNTERS,
    CONTRADICTION_KNOWN, canonical_hash, registered_split, support_audit, manifest_records, file_hash)

FRACTIONS=tuple(name+'_frac_q12' for name in CONTRADICTION_COUNTERS[:-1])


def prepare(rows):
    """Require a measured compatible contract; unknown history is never zero."""
    expected=canonical_hash(CONTRADICTION_CONTRACT)
    if any(r.get('contradiction_contract')!=expected for r in rows):
        raise ValueError('Augmented comparison requires the explicit contradiction contract on every row; legacy telemetry cannot be filled with zeros')
    kept=[];excluded=collections.Counter()
    for original in rows:
        if not original.get('movement_label_available',1):excluded['planning_only']+=1;continue
        if original.get(CONTRADICTION_KNOWN)!=1:excluded['unknown_capture']+=1;continue
        row=dict(original);denominator=row[CONTRADICTION_COUNTERS[-1]]
        if denominator<0 or any(not 0<=row[name]<=denominator for name in CONTRADICTION_COUNTERS[:-1]):
            raise ValueError('Invalid since-capture event/query counts')
        for event,fraction in zip(CONTRADICTION_COUNTERS[:-1],FRACTIONS):
            row[fraction]=row[event]*4096//max(1,denominator)
        from tools.resource_gradient_dataset import bucket
        row['age_band']=bucket(row['age'],(32,64,128,256,512,1024,4096))
        row['demand_band']=bucket(row['q128'],(1,8,32,128))
        kept.append(row)
    if not kept:raise ValueError('No known-capture movement episodes for paired comparison')
    return kept,dict(excluded)


def variants(ablations=True):
    from tools.resource_gradient_model import FEATURES
    baseline=list(FEATURES);counts=list(CONTRADICTION_COUNTERS);fractions=list(FRACTIONS)
    def spec(features):return dict(features=features,transforms=['identity_q12' if f in FRACTIONS else 'log1p_q12' for f in features])
    result={'baseline7':spec(baseline),'counts':spec(baseline+counts),
            'fractions':spec(baseline+fractions),'counts_fractions':spec(baseline+counts+fractions)}
    result['fractions_log']=dict(features=baseline+fractions,transforms=['log1p_q12']*(len(baseline)+len(fractions)))
    if ablations:
        for feature in counts:
            result['add:'+feature]=spec(baseline+[feature])
            # Remove both representations of an event from the full variant.
            remove={feature,feature+'_frac_q12'}
            result['without:'+feature]=spec([f for f in baseline+counts+fractions if f not in remove])
    return result


def separate_outcomes(rows):
    """These observational summaries are not counterfactual movement benefits."""
    fields=('planning_queries','defer_planning_false_negative','defer_planning_false_positive',
            'refresh_planning_false_negative','refresh_planning_false_positive',
            'defer_stalls','refresh_stalls')
    result={name:sum(r[name] for r in rows if name in r) for name in fields}
    result['episodes']=len(rows)
    queries=sum(r.get('queries',0) for r in rows)
    result['movement_label_queries']=queries
    result['defer_stall_rate']=result['defer_stalls']/queries if queries and any('defer_stalls' in r for r in rows) else None
    result['refresh_stall_rate']=result['refresh_stalls']/queries if queries and any('refresh_stalls' in r for r in rows) else None
    result['overlapping_history_reachable_no_move_sum']=sum(r[CONTRADICTION_COUNTERS[-2]] for r in rows)
    result['note']='Planning/stall labels describe disjoint episode query horizons; cumulative since-capture feature histories overlap. Their sums are diagnostics, never actual game event rates.'
    result['available_columns']=[name for name in fields if any(name in r for r in rows)]
    return result


def live_totals(manifests):
    """Verified unsampled, nonoverlapping per-field/world-epoch event receipts."""
    names=tuple(name.removeprefix('since_capture_') for name in CONTRADICTION_COUNTERS)
    seen=set();totals=collections.Counter();games=0;missing=[];subgroups=collections.defaultdict(collections.Counter)
    for manifest in manifests:
        records,enclosing=manifest_records(manifest)
        for record in records:
            identity=(record['group'],record['binary_sha256'],record['episode_sha256'])
            if identity in seen:continue
            seen.add(identity)
            artifact=record.get('provenance',{}).get('artifacts',{}).get('gradient-contradictions.csv')
            if not artifact:missing.append(record['group']);continue
            path=Path(artifact['path'])
            if not path.is_absolute():path=Path(manifest).resolve().parent/path
            if file_hash(path)!=artifact['sha256']:raise ValueError('Live contradiction totals hash mismatch')
            per_game=collections.Counter();epochs=set()
            with (gzip.open(path,'rt') if path.suffix=='.gz' else path.open()) as stream:
                reader=csv.DictReader(stream)
                if not {'epoch','key',*names}<=set(reader.fieldnames or ()):raise ValueError('Live contradiction totals columns missing')
                for row in reader:
                    key=(int(row['epoch']),int(row['key']))
                    if key in epochs:raise ValueError('Duplicate live contradiction epoch/field totals')
                    epochs.add(key);values={name:int(row[name]) for name in names}
                    if any(v<0 for v in values.values()) or any(values[name]>values['movement_queries'] for name in names[:-1]):
                        raise ValueError('Invalid live contradiction event/query counts')
                    per_game.update(values)
            totals.update(per_game);games+=1
            spec=record.get('spec',{});metadata=enclosing or record.get('cohort',{})
            for key,value in (('format',record['format']),('generator',spec.get('generator','unknown')),('role',metadata.get('role','unspecified'))):
                subgroups[key,str(value)].update(per_game)
    def summary(counts):
        denominator=counts['movement_queries']
        return dict(counts=dict(counts),rates={name:counts[name]/denominator if denominator else None for name in names[:-1]})
    return dict(measured=games>0,complete=not missing,games=games,missing_games=missing,overall=summary(totals),
                subgroups={f'{key}:{value}':summary(counts) for (key,value),counts in sorted(subgroups.items())},
                note='Actual unsampled movement-query event rates; one cumulative record per field/world epoch. Categories can overlap on a query.')


def compare(args):
    from tools import resource_gradient_model as m
    all_rows=m.dataset(args.manifest);rows,excluded=prepare(all_rows)
    expected=collections.defaultdict(set);manifest_games=collections.defaultdict(set)
    for manifest in args.manifest:
        records,_=manifest_records(manifest)
        for record in records:
            spec=record.get('spec',{});group=record.get('map_group')
            if not group and all(k in spec for k in ('generator','players','map_seed')) and ('size' in spec or {'width','height'}<=spec.keys()):
                group=m.map_identity(spec)
            expected[record['format']].add(group or record.get('group','unknown'))
            manifest_games[record['format']].add((record.get('group'),record.get('binary_sha256'),record.get('episode_sha256')))
    for row in all_rows:expected[row['format']]  # preserve formats even with legacy starting-state grouping
    eligibility={}
    for fmt,planned_maps in sorted(expected.items()):
        original=[r for r in all_rows if r['format']==fmt];eligible=[r for r in rows if r['format']==fmt]
        eligibility[fmt]=dict(manifest_maps=len(planned_maps),manifest_games=len(manifest_games[fmt]),observed_maps=len({r['group'] for r in original}),
            eligible_maps=len({r['group'] for r in eligible}),episodes=len(original),eligible_episodes=len(eligible),
            unknown_capture_episodes=sum(r.get(CONTRADICTION_KNOWN)!=1 for r in original),
            planning_only_episodes=sum(not r.get('movement_label_available',1) for r in original))
    missing={fmt:stats for fmt,stats in eligibility.items() if stats['eligible_maps']<4}
    if missing:raise ValueError('Need at least four known-capture movement maps per expected format: '+json.dumps(missing,sort_keys=True))
    held,folds,coverage=registered_split(rows,args.split_registry,m.split_maps)
    dev=[r for r in rows if r['group'] not in held];test=[r for r in rows if r['group'] in held]
    row_hash=hashlib.sha256()
    for row in rows:
        row_hash.update(json.dumps([row.get('collection'),row.get('tick'),row.get('key'),
            row['defer_extra'],row['refresh_extra'],*[row[k] for k in (*m.FEATURES,*CONTRADICTION_COUNTERS)]],separators=(',',':')).encode()+b'\n')
    report=dict(schema=1,research_only=True,contract=CONTRADICTION_CONTRACT,
                rows=len(rows),excluded=excluded,eligibility_by_format=eligibility,identical_rows_sha256=row_hash.hexdigest(),
                manifests=[dict(name=Path(p).name,sha256=m.sha(p)) for p in args.manifest],
                split_coverage=coverage,development_states=sorted({r['group'] for r in dev}),heldout_states=sorted(held),
                support=support_audit(all_rows),separate_outcomes=separate_outcomes(all_rows),
                live_event_totals=live_totals(args.manifest),variants={},
                note='All variants share eligible rows, weights and map splits. Offline comparisons do not establish closed-loop CPU/quality gains. No production model/header is written.')
    for name,spec in variants(not getattr(args,'no_ablations',False)).items():
        cv=[]
        for fold in range(3):
            training=[r for r in dev if folds[r['group']]!=fold];validation=[r for r in dev if folds[r['group']]==fold]
            model=m.fit_heads(training,**spec);cv.append(m.metrics(validation,model))
        model=m.fit_heads(dev,**spec)
        slices=m.metric_slices(test,model)
        for key in ('age_band','demand_band'):
            groups=collections.defaultdict(list)
            for row in test:groups[row[key]].append(row)
            slices[key]={label:m.metrics(group,model) for label,group in sorted(groups.items())}
        report['variants'][name]=dict(features=spec['features'],transforms=spec['transforms'],model=model,
            cross_validation=cv,holdout=m.metrics(test,model),holdout_slices=slices)
    report['separate_outcomes_holdout']=separate_outcomes([r for r in all_rows if r['group'] in held])
    report['separate_outcomes_slices']={}
    for key in ('format','generator','dimensions'):
        groups=collections.defaultdict(list)
        for row in all_rows:groups[row.get(key,'unknown')].append(row)
        report['separate_outcomes_slices'][key]={label:separate_outcomes(group) for label,group in sorted(groups.items())}
    base=report['variants']['baseline7']['holdout']
    for result in report['variants'].values():
        result['holdout_delta_vs_baseline7']={key:result['holdout'][key]-value for key,value in base.items()
            if isinstance(value,(int,float)) and isinstance(result['holdout'].get(key),(int,float))}
    base_cv=report['variants']['baseline7']['cross_validation']
    for result in report['variants'].values():
        result['cross_validation_delta_vs_baseline7']=[{key:fold[key]-value for key,value in reference.items()
            if isinstance(value,(int,float)) and isinstance(fold.get(key),(int,float))}
            for fold,reference in zip(result['cross_validation'],base_cv)]
    full=report['variants']['counts_fractions']['holdout']
    report['leave_event_out']=sorted([dict(feature=name.split(':',1)[1],
        decision_regret_increase=value['holdout']['decision_regret']-full['decision_regret'],
        mae_increase=value['holdout']['mae']-full['mae'])
        for name,value in report['variants'].items() if name.startswith('without:')],key=lambda r:-r['decision_regret_increase'])
    report['incremental_features']=sorted([
        dict(feature=name.split(':',1)[1],decision_regret_improvement=-value['holdout_delta_vs_baseline7']['decision_regret'],
             mae_improvement=-value['holdout_delta_vs_baseline7']['mae'])
        for name,value in report['variants'].items() if name.startswith('add:')],key=lambda r:-r['decision_regret_improvement'])
    m.save(args.output,report)
    print(json.dumps({key:dict(holdout=value['holdout'],delta=value['holdout_delta_vs_baseline7'])
                      for key,value in report['variants'].items()},indent=2))
    return report


def add_command(sub):
    p=sub.add_parser('compare-contradictions',help='Research-only paired baseline/contradiction fits; never emits production coefficients')
    p.add_argument('--manifest',action='append',required=True);p.add_argument('--split-registry',required=True)
    p.add_argument('--output',required=True);p.add_argument('--no-ablations',action='store_true')
    p.set_defaults(action=compare)
