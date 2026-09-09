#!/usr/bin/env python3
"""Reviewable execution manifests, outcome reports, and measured budget checkpoints."""
from __future__ import annotations
from collections import defaultdict, Counter
import json
from pathlib import Path
import statistics
import maxima_win_experiment as exp
import maxima_win_statistics as stats


def execution(scenario,settings,repeat=False):
    # Arm/switch names are comparison metadata, not execution identities.
    # This allows exact baseline reuse even when its label differs by switch.
    payload={'scenario':scenario,'settings':settings,'repeat':repeat}
    payload['execution_id']=exp.identity(payload)
    return payload


def comparisons(protocol,stage,counts,validated,control_settings=None):
    if stage not in ('controls','pilot','confirmation'): raise ValueError('invalid stage')
    if stage!='controls' and set(counts)-set(validated): raise ValueError('unvalidated switches cannot be dispatched')
    if stage=='pilot' and any(n!=100 for n in counts.values()): raise ValueError('pilot requires 100 fresh pairs per switch')
    jobs={}; rows=[]
    for switch,n in counts.items():
        if n<=0: raise ValueError('positive fixed sample required')
        for i in range(n):
            s=exp.scenario(protocol,stage,i,balanced=stage=='controls')
            if stage=='controls':
                if n!=200 or not control_settings: raise ValueError('200 controls and verified disabled configuration required')
                if protocol.get('controls',{}).get('kind')=='no_orders' and control_settings!={exp.NO_ORDERS:True}:
                    raise ValueError('no-orders protocol requires the complete AI-off treatment')
                on=exp.arm_settings(protocol,s); off=json.loads(json.dumps(on))
                for p in s['players']:
                    if p['focal']: off[str(p['player'])].update(control_settings)
            else:
                on=exp.arm_settings(protocol,s,switch,True)
                off=exp.arm_settings(protocol,s,switch,False)
            arms={'on':on,'off':off}; row={'switch':switch,'scenario_id':s['scenario_id'],
                 'format':s['format'],'opponent':s['opponent'],'stage':stage,'index':i,'repeats':{}}
            for arm in s['order']:
                job=execution(s,arms[arm]); jobs.setdefault(job['execution_id'],job)
                row[arm]=job['execution_id']
                if s['repeat']:
                    repeat=execution(s,arms[arm],True); jobs.setdefault(repeat['execution_id'],repeat)
                    row['repeats'][arm]=repeat['execution_id']
            rows.append(row)
    return {'protocol_id':protocol['protocol_id'],'stage':stage,'fixed_pairs':counts,
            'jobs':list(jobs.values()),'comparisons':rows}


def combined_comparisons(protocol,confirmed,n):
    changes={r['switch']:False for r in confirmed['results'] if r['default_change_candidate']}
    if not confirmed['confirmatory'] or confirmed['protocol_id']!=protocol['protocol_id'] or not changes:
        raise ValueError('confirmed harmful-ON switches with estimated cost >=2 points required')
    jobs={};rows=[]
    for i in range(n):
        s=exp.scenario(protocol,'combination',i)
        baseline=exp.arm_settings(protocol,s); proposed=json.loads(json.dumps(baseline))
        for p in s['players']:
            if p['focal']: proposed[str(p['player'])].update(changes)
        row={'switch':'combined','scenario_id':s['scenario_id'],'format':s['format'],
             'opponent':s['opponent'],'stage':'combination','index':i,'repeats':{}}
        for arm in s['order']:
            job=execution(s,proposed if arm=='on' else baseline)
            jobs.setdefault(job['execution_id'],job);row[arm]=job['execution_id']
            if s['repeat']:
                repeat=execution(s,job['settings'],True)
                jobs.setdefault(repeat['execution_id'],repeat);row['repeats'][arm]=repeat['execution_id']
        rows.append(row)
    return {'protocol_id':protocol['protocol_id'],'stage':'combination','fixed_pairs':{'combined':n},
            'changes':changes,'jobs':list(jobs.values()),'comparisons':rows}


def assemble(manifest,results):
    """Missing executions remain unresolved; corrupt receipts must be rejected earlier."""
    grouped=defaultdict(list)
    for row in manifest['comparisons']:
        values={}
        for arm in ('on','off'):
            result=results.get(row[arm]); values[arm]=result['outcome'] if result else None
            if result:
                if result['scenario_id']!=row['scenario_id']: raise exp.IntegrityError('result scenario mismatch')
                if result.get('repeat'): raise exp.IntegrityError('repeat substituted for independent arm')
            repeat=results.get(row['repeats'].get(arm))
            if repeat and result:
                if repeat['configuration_id']!=result['configuration_id'] or exp.deterministic_signature(
                    repeat['receipts'][-1]['terminal'])!=exp.deterministic_signature(result['receipts'][-1]['terminal']):
                    raise exp.IntegrityError('deterministic repeat audit failed; stop dispatch')
        grouped[row['switch']].append({**{k:row[k] for k in ('scenario_id','format','opponent')},**values})
    return grouped


def report(protocol,manifest,results,confirmatory=False):
    if manifest['protocol_id']!=protocol['protocol_id']: raise exp.IntegrityError('manifest freeze mismatch')
    if confirmatory and manifest['stage'] not in ('confirmation','combination'): raise ValueError('pilot/controls cannot make confirmatory claims')
    output=[]
    for switch,pairs in assemble(manifest,results).items():
        expected=manifest['fixed_pairs'][switch]
        if len(pairs)!=expected: raise ValueError('one final analysis requires the fixed independent sample')
        alpha=protocol['combination_alpha'] if manifest['stage']=='combination' else protocol['hypothesis_alpha'] if confirmatory else .05
        overall=stats.analyze(pairs,alpha)
        cells=[]
        for fmt in exp.FORMATS:
            for opponent in exp.OPPONENTS:
                rows=[p for p in pairs if p['format']==fmt and p['opponent']==opponent]
                cells.append({'format':fmt,'opponent':opponent,'weight':.05,'descriptive':True,
                              'result':stats.analyze(rows,.05) if rows else None})
        by_format=[{'format':f,'result':stats.analyze(r,.05) if r else None}
                   for f in exp.FORMATS for r in [[p for p in pairs if p['format']==f]]]
        by_opponent=[{'opponent':o,'result':stats.analyze(r,.05) if r else None}
                     for o in exp.OPPONENTS for r in [[p for p in pairs if p['opponent']==o]]]
        output.append({'switch':switch,'overall':overall,'cells':cells,'by_format':by_format,'by_opponent':by_opponent,
                       'default_change_candidate':confirmatory and overall['direction']=='harmful' and
                            overall['delta'] is not None and overall['delta']<=-.02,
                       'default_change_permitted':False})
    return {'protocol_id':protocol['protocol_id'],'stage':manifest['stage'],'confirmatory':confirmatory,
            'interpretation':'one fixed final analysis' if confirmatory else 'descriptive only; no default recommendations',
            'results':output,'combination_required_before_defaults':True,
            'estimand':protocol.get('estimand','formal win probability'),
            'decision_counts':dict(Counter(r.get('decision_reason','legacy') for r in results.values() if not r.get('repeat')))}


def publish_final(path,protocol,manifest,results):
    if any(j['execution_id'] not in results for j in manifest['jobs']):
        raise exp.IntegrityError('final analysis requires every scheduled execution and repeat; capped outcomes may remain null')
    value=report(protocol,manifest,results,True)
    digest=exp.identity({'manifest':manifest,'results':results})
    value['analysis_input_id']=digest
    if path.exists():
        prior=json.loads(path.read_text())
        if prior['analysis_input_id']!=digest: raise exp.IntegrityError('final analysis already used; changed-data reanalysis forbidden')
        return prior
    exp.atomic(path,value); return value


def controls_checkpoint(protocol,manifest,results,behavior_passed):
    if manifest['stage']!='controls': raise ValueError('controls required')
    report_value=report(protocol,manifest,results)
    if len(report_value['results'])!=1: raise ValueError('exactly one positive-control comparison required')
    test=report_value['results'][0]['overall']
    all_jobs_complete=all(j['execution_id'] in results for j in manifest['jobs'])
    import math
    # Controls fix ten scenarios in each cell: use a bound valid for independent
    # heterogeneous paired differences, rather than claiming an iid binomial law.
    radius=math.sqrt(2*math.log(2/.05)/test['independent_pairs'])
    control_ci=[max(-1,test['outcome_bounds'][0]-radius), min(1,test['outcome_bounds'][1]+radius)]
    passed=behavior_passed and all_jobs_complete and test['independent_pairs']==200 and control_ci[0]>0
    by_cell=[]
    for cell in report_value['results'][0]['cells']:
        data=cell['result']; n=data['independent_pairs']
        win_rate=data['on_observed_wins']/n
        by_cell.append({**cell,'baseline_observed_win_rate':win_rate,
                        'floor_flag':win_rate<=.05,'ceiling_flag':win_rate>=.95})
    times=[r['seconds'] for r in results.values()]
    return {'checkpoint':'after_controls','accepted':passed,'requirement':'behavioral suppression plus baseline-minus-disabled 95% lower bound > 0',
            'acceptance_ci':control_ci,'acceptance_method':'Hoeffding bound for stratified independent paired differences',
            'report':report_value,'cells':by_cell,'complete_executions':len(results),'all_jobs_complete':all_jobs_complete,
            'observed_engine_hours':sum(times)/3600,'mean_seconds':statistics.mean(times) if times else None,
            'next_stage':'sizing_pilot' if passed else 'investigation; no further dispatch'}


def pilot_budget(protocol,manifest,results,transfer_seconds,reserved_slots):
    if manifest['stage']!='pilot': raise ValueError('pilot required')
    if any(j['execution_id'] not in results for j in manifest['jobs']): raise ValueError('pilot executions and repeats must all finish')
    if transfer_seconds<0: raise ValueError('transfer time must be nonnegative')
    grouped=assemble(manifest,results); budgets=[]
    for switch,pairs in grouped.items():
        if len(pairs)!=100: raise ValueError('pilot requires 100 independent pairs')
        sizing=stats.size_from_pilot(pairs,len(protocol['switches']))
        referenced={row[arm] for row in manifest['comparisons'] if row['switch']==switch for arm in ('on','off')}
        timings=[results[k]['seconds'] for k in referenced if k in results]
        # No cost estimate while any execution or repeat remains operationally missing.
        if len(timings)!=len(referenced) or reserved_slots<1: raise ValueError('complete measured runtime and verified reserved capacity required')
        seconds=statistics.mean(timings)
        n=sizing['required_pairs']; hours=n*2*1.05*(seconds+transfer_seconds)/3600 if n else None
        budgets.append({'switch':switch,'sizing':sizing,'engine_plus_transfer_hours_upper_budget':hours,
                        'eta_hours_at_reserved_capacity':hours/reserved_slots if hours else None,
                        'mean_arm_seconds_including_long_continuations':seconds,
                        'repeat_fraction':.05,'shared_baseline_savings':'apply only to identical execution identities'})
    return {'checkpoint':'after_sizing_pilot','protocol_id':protocol['protocol_id'],'allocation_required':True,
            'budgets':budgets,'pilot_not_pooled_with_confirmation':True}
