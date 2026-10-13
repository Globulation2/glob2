#!/usr/bin/env python3
"""Read-only finite scaling analysis. Never launches apps, builds, tests or profiles."""
import argparse,datetime,hashlib,html,importlib.util,json,math,statistics
from pathlib import Path
HERE=Path(__file__).resolve().parent;ART=HERE.parent;OUT=HERE/'analysis';ROOT=ART.parents[1]
EXPECTED_RUNNER='9d6259ab48540edd154984faacab60835b0994e0002b24028b8451ab8600cb42'
METRICS={'cpu_s':'Whole process CPU seconds','wall_s':'Whole process wall seconds','peak_rss_bytes':'Peak RSS bytes','simulation_cpu_s':'Engine interval CPU seconds','simulation_wall_s':'Engine interval wall seconds','cpu_per_tick_s':'Whole process CPU seconds/tick','ticks_per_e2e_second':'Ticks/whole process second','ticks_per_run_second':'Ticks/engine interval second','system_cpu_per_tick_s':'System CPU seconds/tick','voluntary_switches_per_tick':'Waited-child voluntary switches/tick','involuntary_switches_per_tick':'Waited-child involuntary switches/tick'}
COUNTS=[1,3,6,12,'auto']
def load(p):return json.loads(Path(p).read_text())
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def require(ok,why):
 if not ok:raise ValueError(why)
def mod(name,p):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
A=mod('scaling_paired_stats',ART/'analyze_performance.py')
def provenance(p):
 p=Path(p);return dict(path=str(p),exists=p.is_file(),sha256=sha(p) if p.is_file() else None)
def preflight():
 require(sha(HERE/'run.py')==EXPECTED_RUNNER,'material runner drift')
 plan=load(HERE/'plan.json');dispatch=load(HERE/'dispatch.json');require(plan['sealed'] and dispatch['plan_sha256']==sha(HERE/'plan.json'),'sealed plan/dispatch mismatch')
 require(dispatch['runner_sha256']==EXPECTED_RUNNER and dispatch['helper_sha256']==sha(HERE/'execute-one.py') and dispatch['execute_sha256']==sha(ROOT/'test/benchmark_parallel_compute.py'),'execute provenance changed')
 require(len(plan['fixtures'])==6 and len(dispatch['scopes'])==2 and dispatch['counts']==COUNTS,'finite workload changed')
 require([len(s['mask']) for s in dispatch['scopes']]==[6,24] and dispatch['planned_native_children']==480 and dispatch['planned_conformance_children']==36,'budgets/counts changed')
 return plan,dispatch

def job_expected(dispatch,stage):
 result=[]
 for si,scope in enumerate(dispatch['scopes']):
  for fi,f in enumerate(dispatch['fixtures']):
   choices=[(12,'on'),(24,'on'),('auto','on')] if stage=='conformance' else [(None,'off'),(None,'on')]
   for count,gpu in choices:
    key=scope['id']+'-'+f['id']+'-gpu'+gpu+('' if count is None else '-n'+str(count))
    result.append(dict(id=key,stage=stage,gpu=gpu,mask=scope['mask'],scope_id=scope['id'],scope_index=si,seed_index=fi%2,fixture_id=f['id'],fixture=f['fixture'],ticks=f['ticks'],seed=f['seed'],layout=f['layout'],requested_count=count,expected_count=(len(scope['mask']) if count=='auto' else count)))
 return result

def phase_seal(stage,expected):
 p=HERE/(stage+'-complete.json')
 if not p.exists():return False,provenance(p)
 v=load(p);require(v['passed'] is True and v['selected_jobs']==expected and v['plan_sha256']==sha(HERE/'plan.json'),'invalid '+stage+' completion seal')
 return True,provenance(p)

def verified_job(expected):
 path=HERE/'jobs'/(expected['stage']+'-'+expected['id']+'.json');job=load(path);d=Path(job['directory']);gate=load(d/'passed.json')
 for k in ('id','stage','gpu','mask','scope_index','seed_index','fixture_id','fixture','ticks'):require(job[k]==expected[k],'job expected identity differs '+k)
 require(job['plan_sha256']==sha(HERE/'plan.json') and gate['passed'] is True and gate['job_sha256']==sha(path) and gate['plan_sha256']==job['plan_sha256'],'job/pass seal stale')
 require(job['binary']==load(HERE/'dispatch.json')['binary'] and sha(job['binary'])==load(HERE/'dispatch.json')['binary_sha256'],'native binary changed')
 audit=load(d/'input-verification.json');before=load(d/'inputs-before.json');after=load(d/'inputs-after.json');require(audit['passed'] and not audit['changed_inputs'] and before==after,'input audit differs')
 inputs=before['runner_inputs'];require(inputs[str(HERE/'run.py')]==EXPECTED_RUNNER and inputs[str(HERE/'execute-one.py')]==sha(HERE/'execute-one.py'),'runner/helper absent from input audit')
 require(before['affinity']==job['mask'],'actual audit mask differs')
 return job,d

def metrics(m,ticks,proof):
 r=m['result'];switches=proof.get('waited_child_context_switches');require(switches is not None and all(type(switches.get(k)) is int and switches[k]>=0 for k in ('voluntary','involuntary')),'supplemental waitedchild switch proof absent/invalid');require(r['benchmark_measured_ticks']==ticks and r['benchmark_run_cpu_ns']>0 and r['run_ns']>0,'engine interval absent/short')
 return dict(system_cpu_per_tick_s=m['system_s']/ticks,voluntary_switches_per_tick=switches['voluntary']/ticks,involuntary_switches_per_tick=switches['involuntary']/ticks,cpu_s=m['cpu_s'],wall_s=m['wall_s'],peak_rss_bytes=m['peak_rss_bytes'],simulation_cpu_s=r['benchmark_run_cpu_ns']/1e9,simulation_wall_s=r['run_ns']/1e9,cpu_per_tick_s=m['cpu_s']/ticks,ticks_per_e2e_second=ticks/m['wall_s'],ticks_per_run_second=ticks/(r['run_ns']/1e9))
def health(r):
 g=r.get('gradient_execution',{});requested=g.get('gpu_requested_fields');completed=g.get('gpu_complete_fields');selected=g.get('gpu_selected_fields')
 return dict(gpu_requested=r.get('gpu_requested'),gpu_device=r.get('gpu_device'),gpu_error=r.get('gpu_error'),host_waits=r.get('gpu_host_event_waits'),gradient_execution=g,requested_fields=requested,gpu_completed_fields=completed,device_committed_fields=g.get('device_committed_fields'),completion_fraction=completed/requested if requested else None,selection_fraction=selected/requested if requested else None,qualification='Observed cumulative end counters; fields are not GPUtime or chronological recovery proof. No recovery feature is shipped.')

def verify_rows(job,d,expected,C):
 rows=load(d/'rows.json');require(len(rows)==20,'four complete count blocks required');labels={(row['repeat'],str(row['requested_count'])) for row in rows};require(labels=={(rep,str(count)) for rep in (-1,0,1,2) for count in COUNTS},'row labels invalid')
 parents=set();verified=[]
 for row in rows:
  count=row['requested_count'];resolved=len(job['mask']) if count=='auto' else count;require(row['expected_count']==resolved,'expected count wrong');m=row['measurement'];r=m['result'];output=d/str(row['repeat'])/str(count);require(Path(row['directory'])==output,'row output identity differs')
  request=d/(str(row['repeat'])+'-'+str(count)+'.execute.json');inputs=load(request);proof=load(output/'controller-proof.json');stored=load(output/'controller-row.json');require(stored==m,'native row mutated')
  args=['--load-game',job['fixture'],'--ticks',str(job['ticks']),'--compute-threads',str(count),'--gpu',job['gpu'],'--write-replay','--benchmark-warmup','0'];command=[job['binary'],'game','run',*args,'--output-dir',str(output)]
  require(inputs['native_timeout_seconds']==proof['native_timeout_seconds']==3600 and proof['signals_observed']==[],'onehour proof/signals differ');require(inputs['args']==args and inputs['binary']==job['binary'] and inputs['cwd']==str(ROOT) and inputs['affinity']==job['mask'] and m['command']==command,'per-game invocation differs')
  require(inputs['helper_sha256']==proof['execute_helper_sha256']==sha(HERE/'execute-one.py') and inputs['execute_sha256']==proof['execute_sha256']==sha(ROOT/'test/benchmark_parallel_compute.py') and proof['controller_input_sha256']==sha(request),'per-game source/request seal differs')
  require(proof['controller_parent_pid']==proof['process_group'],'helper is not direct child of owned stdlib worker');parents.add(proof['controller_parent_pid']);require(proof['affinity']==job['mask'],'helper actual affinity differs')
  require(m['peak_rss_bytes']==proof['native_peak_rss_bytes'] and m['runner_peak_rss_bytes']==proof['native_runner_peak_rss_bytes'] and m['peak_rss_bytes']>m['runner_peak_rss_bytes'] and proof['floor_censored'] is False,'censored RSS/primitive proof')
  require(r['compute_threads']==resolved and r['gpu_requested']==int(job['gpu']=='on') and r['termination']=='tick_cap' and r['ticks']==job['ticks'],'actualcount/GPU/end differs')
  validate=load(output/'validated.json');require(validate['passed'],'native validation failed');outcome={k:r.get(k) for k in ('ticks','termination','winning_teams','initialChecksum','finalChecksum')};require(all(type(outcome[k]) is int for k in ('initialChecksum','finalChecksum')),'CRC missing')
  require(outcome==validate['outcome'] and sha(output/'game.replay')==validate['replay']['sha256'] and (output/'game.replay').stat().st_size==validate['replay']['bytes'],'native outcome/replay mutated')
  verified.append(dict(repeat=row['repeat'],requested_count=count,actual_count=resolved,metrics=metrics(m,job['ticks'],proof),health=health(r),outcome=outcome,replay=validate['replay'],floor_bytes=m['runner_peak_rss_bytes'],controller_parent=proof['controller_parent_pid'],sources=[provenance(request),provenance(output/'controller-proof.json'),provenance(output/'controller-row.json'),provenance(output/'validated.json')]))
 require(len(parents)==1,'per-job lightweight parent inconsistent')
 # Reparse one reference per block; exact wholefile hashes bind every count to it.
 for rep in (-1,0,1,2):
  block=[r for r in verified if r['repeat']==rep];baseline=next(r for r in block if r['requested_count']==1);parsed=C.ait1(d/str(rep)/'1/game.replay');require(parsed==baseline['replay']['ait1'],'canonical AIT1 proof stale')
  for r in block:require(r['outcome']==baseline['outcome'] and r['replay']==baseline['replay'],'cross-count arithmetic/replay differs')
 return verified

def analyze():
 plan,dispatch=preflight();cv,cp=phase_seal('conformance',36);pv,pp=phase_seal('performance',24)
 result=dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),complete=False,accepted=False,conformance_complete=cv,performance_complete=pv,completion_sources=[cp,pp],planned_native=480,verified_native=0,warmups=[],groups={},individual_slower_points=[],rejected=[],provenance=[provenance(Path(__file__)),provenance(ART/'analyze_performance.py'),provenance(HERE/'plan.json'),provenance(HERE/'dispatch.json')],scope='Final9e only, six vs24logicalCPUbudgets; GPUoff/GPUon separate; requested1/3/6/12/auto, auto6/24. Every ratio pairs same seed/block withN1. No production optimum selection or transfer to othermachines. 2seeds×3blocks are descriptive; no unmatched workload pooling.')
 if not(cv and pv):result['state']='pending full36+24 gates; all statistics withheld';return result
 for path,expectedsha in plan['seals'].items():require(sha(path)==expectedsha,'sealed source/fixture/proof input changed '+path)
 F=mod('scaling_readonly_helpers',ART/'final-integration/run.py');C=F.C
 for expected in job_expected(dispatch,'conformance'):
  try:
   job,d=verified_job(expected);v=load(d/'validated.json');require(v['passed'] and v['actual_count']==expected['expected_count'],'widecount validation stale');f=next(f for f in plan['fixtures'] if f['id']==job['fixture_id']);actual=F.V.revalidate(d/'game',f['scenario'],f['seed'],2048,Path(plan['binary']),Path(f['fixture']),job['requested_count'],'on');C.same(actual,f['baseline1'],'scaling widecount raw baseline1 controls');require(v['baseline1']==actual,'derived widecount proof stale')
  except Exception as error:result['rejected'].append(dict(stage='conformance',job=expected['id'],error=repr(error)))
 for expected in job_expected(dispatch,'performance'):
  try:
   job,d=verified_job(expected);rows=verify_rows(job,d,expected,C);result['verified_native']+=len(rows)
   result['warmups'].append(dict(job=expected['id'],rows=[r for r in rows if r['repeat']<0]))
   for count in COUNTS:
    context=dict(budget=expected['scope_id'],mask=expected['mask'],layout=expected['layout'],gpu=expected['gpu'],requested_count=count,actual_count=len(expected['mask']) if count=='auto' else count,window=expected['ticks']);key=json.dumps(context,sort_keys=True);g=result['groups'].setdefault(key,dict(context=context,seeds={},metrics={},absolute={},health=[]))
    chosen=[r for r in rows if r['repeat']>=0 and r['requested_count']==count];reference={r['repeat']:r for r in rows if r['repeat']>=0 and r['requested_count']==1};paired={m:dict(paired_log_ratios=[math.log(r['metrics'][m]/reference[r['repeat']]['metrics'][m]) for r in chosen if r['metrics'][m]>0 and reference[r['repeat']]['metrics'][m]>0],baseline_values=[reference[r['repeat']]['metrics'][m] for r in chosen],candidate_values=[r['metrics'][m] for r in chosen]) for m in METRICS};g['seeds'][str(expected['seed'])]=dict(metrics=paired,raw_rows=chosen)
    g['health'].extend(r['health'] for r in chosen)
    for r in chosen:
     changes={m:100*(r['metrics'][m]/reference[r['repeat']]['metrics'][m]-1) if reference[r['repeat']]['metrics'][m]>0 else None for m in METRICS}
     if count!=1 and (changes['cpu_s']>0 or changes['wall_s']>0):result['individual_slower_points'].append(dict(context=context,seed=expected['seed'],repeat=r['repeat'],percent_changes=changes))
  except Exception as error:result['rejected'].append(dict(stage='performance',job=expected['id'],error=repr(error)))
 if result['rejected'] or result['verified_native']!=480:
  result['state']='invalid/incomplete raw evidence; comparison statistics withheld';result['groups']={};return result
 for g in result['groups'].values():
  require(len(g['seeds'])==2,'expected two matchedseeds');g['metrics']={m:A.stats(g['seeds'],m) if all(len(seed['metrics'][m]['paired_log_ratios'])==3 for seed in g['seeds'].values()) else dict(ratio=None,percent_change=None,percent_ci95=None,qualification='Zero inpairedcount/denominator: logratio undefined; all absolutevalues retained, no hiddenzero substitution.') for m in METRICS}
  for metric in METRICS:
   values=[v for s in g['seeds'].values() for v in s['metrics'][metric]['candidate_values']];g['absolute'][metric]=dict(mean=statistics.mean(values),median=statistics.median(values),minimum=min(values),maximum=max(values),processes=len(values))
  known=[h for h in g['health'] if h['requested_fields'] is not None and h['gpu_completed_fields'] is not None];requested=sum(h['requested_fields'] for h in known);completed=sum(h['gpu_completed_fields'] for h in known);g['GPUrequest_weighted_fraction']=completed/requested if requested else None;g['GPUfraction_scope']='Summed completed/requested fields over measuredprocesses only; not GPUtime, not equal-seed speed or chronological recovery.'
 result['complete']=True;result['state']='complete descriptive countcurves; no optimum/adoption selected';result['floor_range_bytes']=[min(r['floor_bytes'] for g in result['groups'].values() for s in g['seeds'].values() for r in s['raw_rows']),max(r['floor_bytes'] for g in result['groups'].values() for s in g['seeds'].values() for r in s['raw_rows'])];return result

def render(result):
 parts=['<!doctype html><meta charset="utf-8"><title>Thread scaling descriptive evidence</title><style>body{font:15px system-ui;margin:28px}table{border-collapse:collapse;font-size:13px}td,th{padding:7px;border-bottom:1px solid #ccc;text-align:left}.note{background:#fff3cc;padding:14px}</style><h1>Thread scaling: separate budgets and GPU strata</h1><p class="note">'+html.escape(result['scope'])+'</p><p>'+html.escape(result['state'])+f"; verified native {result['verified_native']}/480; no optimal production count selected.</p>"]
 if not result['complete']:
  parts.append('<pre>'+html.escape(json.dumps(result['rejected'],indent=2))+'</pre>');return ''.join(parts)
 parts.append('<table><tr><th>Budget / layout / GPU / requested→actual / ticks</th>'+''.join('<th>'+html.escape(label)+' change vsN1 (descriptive95%)</th>' for label in METRICS.values())+'<th>GPU completed/requested</th></tr>')
 for g in result['groups'].values():
  c=g['context'];parts.append('<tr><td>'+html.escape(f"{c['budget']} / {c['layout']} / {c['gpu']} / {c['requested_count']}→{c['actual_count']} / {c['window']}")+'</td>')
  for key in METRICS:
   m=g['metrics'][key]
   if m['percent_change'] is None:parts.append('<td>undefined ratio (zero counter); absolute values retained</td>');continue
   parts.append(f"<td>{m['percent_change']:+.2f}% [{m['percent_ci95'][0]:+.2f},{m['percent_ci95'][1]:+.2f}]</td>")
  fraction=g['GPUrequest_weighted_fraction'];parts.append('<td>'+('unreported/none' if fraction is None else f'{100*fraction:.3f}%')+'</td></tr>')
 parts.append('</table><h2>Waited-child scheduling indicators (mean/min/max per tick)</h2><p>System CPU includes kernel IO/allocator/driver work; context switches are observations, not an exact wasted/useful CPU split. Controller switches excluded.</p><table><tr><th>Budget/layout/GPU/count</th><th>System CPU s/tick</th><th>Voluntary switches/tick</th><th>Involuntary switches/tick</th></tr>')
 for g in result['groups'].values():
  c=g['context'];parts.append('<tr><td>'+html.escape(f"{c['budget']} / {c['layout']} / {c['gpu']} / {c['requested_count']}→{c['actual_count']}")+'</td>')
  for metric in ('system_cpu_per_tick_s','voluntary_switches_per_tick','involuntary_switches_per_tick'):
   v=g['absolute'][metric];parts.append(f"<td>{v['mean']:.6g} [{v['minimum']:.6g},{v['maximum']:.6g}]</td>")
  parts.append('</tr>')
 parts.append('</table><p>Absolute means/median/range, per-seed/block slower points, cold warmups, counters, floor diagnostics and provenance are retained in analysis.json. GPUfieldfraction is not GPUtime. Native windows establish exactendingCRC/replay, with2048 detailed GCS proof separately; they do not establish everytick for wholeperformancewindow or everymachine.</p>')
 for metric in ('cpu_s','wall_s','peak_rss_bytes','system_cpu_per_tick_s','voluntary_switches_per_tick','involuntary_switches_per_tick'):parts.append('<p><img style="max-width:100%" src="'+metric+'-curves.png" alt="Separate map/GPU count curves"></p>')
 return ''.join(parts)

def plot(result):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 layouts=['playground-ni','oazis-ni','large-mixed'];budgets=['six-logical-three-physical','24-logical-12-physical'];colors=['#2376a4','#dc6f24']
 for metric in ('cpu_s','wall_s','peak_rss_bytes','system_cpu_per_tick_s','voluntary_switches_per_tick','involuntary_switches_per_tick'):
  fig,axes=plt.subplots(3,2,figsize=(12,11),sharex=True)
  for row,layout in enumerate(layouts):
   for col,gpu in enumerate(('off','on')):
    ax=axes[row,col]
    for budget,color in zip(budgets,colors):
     groups=[g for g in result['groups'].values() if g['context']['budget']==budget and g['context']['layout']==layout and g['context']['gpu']==gpu];by={str(g['context']['requested_count']):g for g in groups};require(len(by)==5,'plot count cell missing')
     if metric in ('system_cpu_per_tick_s','voluntary_switches_per_tick','involuntary_switches_per_tick'):
      values=[by[str(c)]['absolute'][metric] for c in COUNTS];y=[v['mean'] for v in values];lo=[a-v['minimum'] for a,v in zip(y,values)];hi=[v['maximum']-a for a,v in zip(y,values)];ax.errorbar(range(5),y,yerr=[lo,hi],marker='o',capsize=3,label=budget,color=color)
     else:
      values=[by[str(c)]['metrics'][metric] for c in COUNTS];y=[v['percent_change'] for v in values];lo=[a-b['percent_ci95'][0] for a,b in zip(y,values)];hi=[b['percent_ci95'][1]-a for a,b in zip(y,values)];ax.errorbar(range(5),y,yerr=[lo,hi],marker='o',capsize=3,label=budget,color=color)
    ax.axhline(0,color='#999',linewidth=.7);ax.set_title(layout+' GPU'+gpu);ax.set_ylabel(METRICS[metric]+'; mean/min-max' if metric in ('system_cpu_per_tick_s','voluntary_switches_per_tick','involuntary_switches_per_tick') else 'Change vs pairedN1 (%)');ax.set_xticks(range(5),['1','3','6','12','auto']);ax.grid(alpha=.2)
    if row==0:ax.legend(fontsize=8)
  fig.suptitle(METRICS[metric]+'; requested counts (auto6/24), two seeds×three blocks\nBudgets/GPU strata separate; categorical positions, descriptive intervals; no optimum selected; supplement spans min/max (notCI)',fontsize=12);fig.tight_layout(rect=[0,0,1,.94]);fig.savefig(OUT/(metric+'-curves.png'),dpi=150);fig.savefig(OUT/(metric+'-curves.svg'));plt.close(fig)

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--folder',type=Path);parser.add_argument('--analyze',action='store_true');parser.add_argument('--preflight',action='store_true');parser.add_argument('--no-plots',action='store_true');args=parser.parse_args()
 if not(args.analyze or args.preflight):print(json.dumps(dict(launches=0,state='plan-only; --preflight verifiesseals; --analyze reads completed36/24 only',statistics_requires_native_rows=480)));return
 require(args.folder is not None and args.folder.resolve()==HERE and HERE.name=='thread-scaling-v2','explicit --folder must name this v2 folder; no v1 fallback')
 if args.preflight:
  plan,dispatch=preflight();print(json.dumps(dict(passed=True,runner_sha256=sha(HERE/'run.py'),helper_sha256=sha(HERE/'execute-one.py'),analyzer_sha256=sha(Path(__file__)),finite_jobs=24,finite_controls=36,native_rows=480,launches=0,completed_gates={s:phase_seal(s,n)[0] for s,n in [('conformance',36),('performance',24)]})));return
 result=analyze();OUT.mkdir(exist_ok=True)
 # Existing pinned plotting environment only. No dependency installation or native child.
 if result['complete'] and not args.no_plots:plot(result)
 (OUT/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');(OUT/'report.html').write_text(render(result));print(json.dumps(dict(complete=result['complete'],state=result['state'],verified_native=result['verified_native'],rejected=result['rejected'],launches=0)))
if __name__=='__main__':main()
