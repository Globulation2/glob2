#!/usr/bin/env python3
"""Read-only isolated GPU evidence analysis. Never launches benchmark/engine/build/profiler processes."""
import datetime, hashlib, html, importlib.util, json, math, os
from pathlib import Path
ART=Path(__file__).resolve().parent;OUT=ART/'gpu-analysis'
spec=importlib.util.spec_from_file_location('gpu_paired_stats',ART/'analyze_performance.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
load,digest=A.load,A.digest
STATUS_KEYS=('gpu_requested','gpu_available','gpu_ready_plans','gpu_error','gpu_required_quarantine_pending','gpu_cancellation_suspended','gpu_cancellation_recoveries','gpu_cancellation_fences','gpu_required_quarantines','gpu_required_proof_failures','gpu_required_cancellations','gpu_host_event_waits')
def require(ok,why):
    if not ok:raise ValueError(why)
def provenance(path):
    p=Path(path);return dict(path=str(p),exists=p.is_file(),sha256=digest(p) if p.is_file() else None)
def health(result):
    execution=result.get('gradient_execution',{});requested=execution.get('gpu_requested_fields');complete=execution.get('gpu_complete_fields');selected=execution.get('gpu_selected_fields')
    def fraction(n,d):return n/d if isinstance(n,(int,float)) and isinstance(d,(int,float)) and d>0 else None
    return dict(status={k:result.get(k) for k in STATUS_KEYS},unexposed_status_keys=[k for k in STATUS_KEYS if k not in result],gradient_execution=execution,gpu_complete_fraction_of_requested=fraction(complete,requested),gpu_selected_fraction_of_requested=fraction(selected,requested),requested_fields=requested,gpu_completed_fields=complete,cpu_completed_fields=execution.get('cpu_complete_fields'),device_committed_fields=execution.get('device_committed_fields'),observed_recovery_events=result.get('gpu_cancellation_recoveries'),accepted_cancellation_flushes=result.get('gpu_cancellation_fences'),flush_is_terminal_proof=False,campaign_fresh_commit_after_recovery_proven=False,all_jobs_recovered_claim=False)
def context(job,factor):
    manifest=load(job['manifest']);require(len(manifest['scenarios'])==1,'single fixture manifest required');scenario=manifest['scenarios'][0];args=scenario['args'];end=int(args[args.index('--ticks')+1]);start=scenario['start_tick']
    require(int(args[args.index('--compute-threads')+1])==job['compute_threads'] and args[args.index('--gpu')+1]==job['gpu'],'job count/GPU mismatch')
    return dict(factor=factor,comparator='untouched-baseline' if factor=='gpu-sync' else 'gpu-sync',cohort=job['stage'],layout=job['scenario'],compute_threads=job['compute_threads'],gpu=job['gpu'],checkpoint_tick=start,window=end-start,expected_end=end)
def case_context(case):
    return dict(cohort=case['stage'],layout=case['scenario']['id'],fixture_id=case['id'],start_tick=case.get('start',0),children=case['children'],mask=case['mask'])
def conformance(folder,plan):
    report=dict(planned_cases=25,planned_children=plan['planned_children'],completed_cases=0,completed_children=0,complete=False,cases=[],pending=[],errors=[],accepted=False)
    for case in plan['cases']:
        key=case['stage']+'-'+case['id'];directory=folder/'conformance'/key;passed=directory/'passed.json';entry=case_context(case);entry['source']=provenance(passed)
        if not passed.exists():report['pending'].append(dict(**entry,failure=provenance(directory/'failure.json')));continue
        gate=load(passed);require(gate['passed'] and gate['binary_sha256']==plan['binary_sha256'] and gate['children']==case['children'] and gate['job']['plan_sha256']==digest(folder/'plan.json'),'conformance gate identity changed')
        invocations=[]
        for path in sorted(directory.glob('*/invocation.json')):
            invocation=load(path);command=invocation['command'];count=command[command.index('--compute-threads')+1];gpu=command[command.index('--gpu')+1]
            require(invocation['returncode']==0 and invocation['binary_sha256']==plan['binary_sha256'] and invocation['affinity']==case['mask'],'conformance invocation changed')
            validated=path.parent/'validated.json';require(validated.is_file(),'conformance raw parser proof missing');result=load(path.parent/'result.json')
            invocations.append(dict(compute_threads=int(count),gpu=gpu,logical_end=result['ticks'],result_source=provenance(path.parent/'result.json'),invocation_source=provenance(path),validated_source=provenance(validated),runtime=health(result)))
        require(len(invocations)==case['children'],'conformance child inventory incomplete')
        entry.update(passed=True,invocations=invocations);report['cases'].append(entry);report['completed_cases']+=1;report['completed_children']+=case['children']
    report['complete']=report['completed_cases']==25 and report['completed_children']==report['planned_children'];return report

def read_timing(job,factor,folder,plan):
    directory=Path(job['output']);own=directory/('sync-passed.json' if factor=='gpu-sync' else 'recovery-passed.json')
    if not own.exists():return None,dict(reason='adapter post-audit pending/not passed',adapter=provenance(own),benchmark=provenance(directory/'campaign-passed.json'),rss_censored=provenance(directory/'rss-censored.json'),log=provenance(folder/'logs'/('performance-'+job['id']+'.log')))
    marker=load(own);require(marker['passed'] and marker['identity']==job['identity'] and marker['plan_sha256']==digest(folder/'plan.json'),'adapter pass identity changed')
    require(marker['adapter_sha256']==digest(folder/'run.py'),'adapter source differs from measured inputs')
    value,reason=A.read_job(job);require(value is not None,'adapter pass but original benchmark not valid: '+str(reason))
    audit=load(directory/'input-verification.json');before=audit['before'];after=audit['after'];require(before==after,'paired input audit before/after differs')
    runner_inputs=before['runner_inputs'];require(runner_inputs.get(str(folder/'run.py'))==digest(folder/'run.py') and runner_inputs.get(str(ART/'performance_campaign.py'))==digest(ART/'performance_campaign.py'),'thin adapter/originalP not in benchmark input audit')
    require(before['affinity']==job['mask'] and before['binaries']['baseline']['sha256']==job['identity']['before_sha256'] and before['binaries']['candidate']['sha256']==job['identity']['after_sha256'],'paired binary/affinity audit mismatch')
    rows=[json.loads(line) for line in (directory/'measurements.jsonl').read_text().splitlines()];require(len(rows)==8,'one warmup+3 paired repeats required');labels={(row['repeat'],row['variant']) for row in rows};require(labels=={(r,v) for r in (-1,0,1,2) for v in ('baseline','candidate')},'warm/cold pair labels invalid')
    runtime=[]
    for row in rows:
        require(row['result']['compute_threads']==job['compute_threads'],'actual participant count differs')
        require(row['peak_rss_bytes']>row['runner_peak_rss_bytes'],'inherited RSS floor accepted unexpectedly')
        require(not row.get('timed_out',False) and not row.get('rss_censored',False),'censored row accepted unexpectedly')
        runtime.append(dict(repeat=row['repeat'],variant=row['variant'],warmup=row['repeat']<0,peak_rss_bytes=row['peak_rss_bytes'],runner_peak_rss_bytes=row['runner_peak_rss_bytes'],health=health(row['result'])))
    value.update(runtime=runtime,adapter_source=provenance(own),job_source=provenance(folder/'jobs'/('performance-'+job['id']+'.json')),context=context(job,factor))
    return value,None

def health_summary(seeds):
    result={}
    for variant in ('baseline','candidate'):
        rows=[row['health'] for seed in seeds.values() for row in seed['runtime'] if not row['warmup'] and row['variant']==variant]
        known=[r for r in rows if r['requested_fields'] is not None and r['gpu_completed_fields'] is not None]
        requested=sum(r['requested_fields'] for r in known);completed=sum(r['gpu_completed_fields'] for r in known)
        result[variant]=dict(measured_processes=len(rows),reported_gpu_processes=len(known),requested_fields_sum=requested,gpu_completed_fields_sum=completed,gpu_completion_fraction=completed/requested if requested else None,device_committed_fields=[r['device_committed_fields'] for r in rows],host_event_waits=[r['status']['gpu_host_event_waits'] for r in rows],recovery_events=[r['observed_recovery_events'] for r in rows],accepted_cancellation_flushes=[r['accepted_cancellation_flushes'] for r in rows],quarantine_pending=[r['status']['gpu_required_quarantine_pending'] for r in rows],suspended=[r['status']['gpu_cancellation_suspended'] for r in rows],gpu_errors=[r['status']['gpu_error'] for r in rows])
    result['scope']='Summed requested/completed field counts over measured processes, warmups excluded; diagnostic request weighting, not GPUtime, equal-seed speed or chronological recovery proof.'
    return result

def analyze_factor(factor):
    folder=ART/(factor+'-campaign');pp=folder/'plan.json'
    if not pp.exists():return dict(factor=factor,state='pending sealed plan',accepted=False,complete=False)
    plan=load(pp);require(plan['sealed'],'unsealed campaign');jobs=plan['performance_jobs'];expected=14 if factor=='gpu-sync' else 39;require(len(jobs)==expected,'finite timing worklist changed')
    result=dict(factor=factor,state='pending/partial',accepted=False,complete=False,planned_jobs=expected,completed_jobs=0,groups={},missing=[],errors=[],regressions=[],slowdowns=[],provenance=[provenance(pp),provenance(folder/'run.py'),provenance(folder/'performance-complete.json')],conformance=conformance(folder,plan))
    phasepath=folder/'performance-complete.json';phase=load(phasepath) if phasepath.exists() else None
    phase_sealed=bool(phase and phase.get('passed') and phase.get('selected_jobs')==expected and phase.get('plan_sha256')==digest(pp))
    result['phase_completion_seal_valid']=phase_sealed
    result['statistics_withheld_until_phase_complete']=not phase_sealed
    result['verified_job_evidence']=[]
    expected_groups={}
    for job in jobs:
        c=context(job,factor);key=json.dumps(c,sort_keys=True);expected_groups.setdefault(key,set()).add(job['seed'])
        value,reason=read_timing(job,factor,folder,plan)
        if value is None:result['missing'].append(dict(job=job['id'],context=c,details=reason));continue
        result['completed_jobs']+=1
        if not phase_sealed:
            result['verified_job_evidence'].append(dict(job=job['id'],context=c,source=value['provenance'],runtime=value['runtime']));continue
        group=result['groups'].setdefault(key,dict(context=c,seeds={},complete=False));require(str(job['seed']) not in group['seeds'],'duplicate seed/context');group['seeds'][str(job['seed'])]=value
    for key,group in result['groups'].items():
        group['completed_seed_count']=len(group['seeds']);group['expected_seed_count']=len(expected_groups[key]);group['complete']=group['completed_seed_count']==group['expected_seed_count'];group['metrics']={m:A.stats(group['seeds'],m) for m in A.METRICS}
        for metric,estimate in group['metrics'].items():
            if metric in ('cpu_s','wall_s','simulation_cpu_s','simulation_wall_s') and estimate['ci95'][0]>1:result['slowdowns'].append(dict(context=group['context'],metric=metric,estimate=estimate,provisional=not group['complete']))
            if estimate.get('credible_over5percent_regression'):result['regressions'].append(dict(context=group['context'],metric=metric,estimate=estimate,provisional=not group['complete']))
        group['health_summary']=health_summary(group['seeds'])
        group['health_scope']='Per-process cumulative counters retained, warmup separate; GPUfraction is completed/requested fields, not time/speed. End counters do not establish commit chronology, terminal fences or alljob recovery.'
    result['complete']=result['completed_jobs']==expected and phase_sealed;result['state']='complete measured matrix; adoption review pending' if result['complete'] else 'pending/partial measured matrix'
    result['recommendation']='Retain GPU synchronization as a correctness prerequisite, subject to final-source/platform checks; no speed claim required.' if factor=='gpu-sync' else 'Recovery adoption pending safe and useful measured benefit; retries can slow small/mixed workloads even while increasing GPU work.'
    return result

def render(report):
    parts=['<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>GPU synchronization and recovery evidence</title><style>body{font:15px/1.5 system-ui;margin:28px;color:#20314b}table{border-collapse:collapse;font-size:13px}th,td{padding:7px;border-bottom:1px solid #ccc;text-align:left}.note{background:#fff4cb;padding:16px}.adverse{color:#a22222}</style><h1>GPU evidence: comparator and workload scopes remain separate</h1><p class="note">Read-only analysis; no benchmark or engine launched. Acceptance remains false. Synchronization is a correctness prerequisite; recovery needs useful measured benefit and final compatibility gates. No universal GPU speedup or recovery claim.</p>']
    for factor in ('gpu-sync','gpu-recovery'):
        r=report[factor];parts.append('<h2>'+factor+'</h2><p>'+html.escape(r['state'])+'</p>')
        if 'planned_jobs' not in r:continue
        parts.append(f"<p>Timing jobs {r['completed_jobs']}/{r['planned_jobs']}; conformance {r['conformance']['completed_cases']}/25 cases, {r['conformance']['completed_children']}/{r['conformance']['planned_children']} children. Hardware and exact conformance prove specific tested paths, separately from timing.</p><p>"+html.escape(r['recommendation'])+'</p><table><tr><th>Comparator/cohort/layout/GPU/count/window</th><th>Seeds</th><th>GPU completed/requested, comparator→candidate</th>'+''.join('<th>'+html.escape(v)+'</th>' for v in A.METRICS.values())+'</tr>')
        for group in r['groups'].values():
            c=group['context'];label=f"{c['comparator']} / {c['cohort']} / {c['layout']} / GPU{c['gpu']} / N{c['compute_threads']} / {c['checkpoint_tick']}+{c['window']}";parts.append('<tr><td>'+html.escape(label)+f"</td><td>{group['completed_seed_count']}/{group['expected_seed_count']}</td>")
            fractions=[group['health_summary'][name]['gpu_completion_fraction'] for name in ('baseline','candidate')];parts.append('<td>'+' → '.join('unreported' if value is None else f'{100*value:.2f}%' for value in fractions)+'</td>')
            for metric in A.METRICS:
                m=group['metrics'][metric];lo,hi=m['percent_ci95'];cls='adverse' if lo>0 and metric!='peak_rss_bytes' else '';parts.append(f'<td class="{cls}">{m["percent_change"]:+.2f}% [{lo:+.2f}, {hi:+.2f}]</td>')
            parts.append('</tr>')
        parts.append('</table>')
        for slow in r['slowdowns']:parts.append('<p class="adverse">Preserve slowdown: '+html.escape(str(slow['context'])+' / '+slow['metric'])+'</p>')
    parts.append('<p>Phase statistics are withheld until the full14/39 completion seal validates; partial counts and exact completed-job input/runtime evidence may be shown. No recovery adoption decision before all53 timing jobs complete.</p><p>Whole-process CPU/wall/RSS include setup and teardown; engine interval CPU and run wall have separate boundaries. One discarded warmup pair plus three balanced measured pairs per seed; two seeds per cell, three large-mixed seeds when tested. Descriptive hierarchical bootstrap, 4,000 draws and RNG7134; few seeds/repeats and multiple comparisons limit inference. No pooling across comparators, GPU modes, counts, windows, initial versus loaded states or adverse cells. Loaded checkpoints rebuild cache/allocator state and do not establish continuous mature peak heap.</p><p>Runtime gpu_cancellation_fences counts accepted flushes, not terminal proof. End recovery totals and GPU commits do not show chronological fresh commits after recovery; that is proven only by the focused hardware marker. GPU completion fractions do not measure GPU time or speed. Zero events remain zero; unavailable fields are not imputed.</p><p><a href="analysis.json">Exact pairs, health counters, masks, input audits, pending jobs and source hashes</a></p></html>');return ''.join(parts)

def main():
    os.sched_setaffinity(0,{15,31});OUT.mkdir(parents=True,exist_ok=True)
    report=dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),analyzer_sha256=digest(Path(__file__)),accepted=False,**{factor:analyze_factor(factor) for factor in ('gpu-sync','gpu-recovery')})
    A.dump(OUT/'analysis.json',report);(OUT/'report.html').write_text(render(report));print(json.dumps({k:{'jobs':v['completed_jobs'],'expected':v['planned_jobs'],'conformance':v['conformance']['completed_cases'],'accepted':False} for k,v in report.items() if k in ('gpu-sync','gpu-recovery')}))
if __name__=='__main__':main()
