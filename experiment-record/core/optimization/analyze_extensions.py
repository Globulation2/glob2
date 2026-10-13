#!/usr/bin/env python3
"""Read-only mature loaded-window/full-native/replication evidence analyzer.
Never launches engines, compilers, profilers or benchmark runners.
"""
import argparse
import datetime
import hashlib
import html
import importlib.util
import json
import math
import os
from pathlib import Path

ART=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('paired_analysis',ART/'analyze_performance.py')
A=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(A)
load,digest=A.load,A.digest

def dump(path,value):
 temp=path.with_suffix(path.suffix+'.tmp');temp.write_text(json.dumps(value,indent=2)+'\n');temp.replace(path)
def require(ok,why):
 if not ok:raise ValueError(why)
def source(path):
 p=Path(path)
 return dict(path=str(p),exists=p.exists(),sha256=digest(p) if p.is_file() else None)

def census(directory):
 files=[]
 for pattern in ('*census*.json','*ownership*.json'):
  for p in directory.glob(pattern):files.append(dict(**source(p),scope='diagnostic allocation/capacity evidence; separate from native RSS',data=load(p)))
 return files

def grouped(jobs, kind):
 result=dict(kind=kind,expected_jobs=len(jobs),completed_jobs=0,complete=False,accepted=False,groups={},missing=[],provenance=[],regressions=[],slowdowns=[])
 for job in jobs:
  output=Path(job['output'])
  if kind=='replication':
   # Artifact planner uses baseline/candidate metadata; launched worklist adds identity.
   normalized=dict(job)
   normalized.setdefault('identity',{})
  else:normalized=job
  if not normalized.get('identity') and (output/'campaign-passed.json').exists():
   result['missing'].append(dict(job=job['id'],reason='launched identity/job artifact missing'));continue
  value,reason=A.read_job(normalized)
  if value is None:result['missing'].append(dict(job=job['id'],reason=reason));continue
  result['completed_jobs']+=1
  key=job['factor']+'/'+job['scenario']
  group=result['groups'].setdefault(key,dict(factor=job['factor'],layout=job['scenario'],seeds={},windows={},census=[]))
  group['seeds'][str(job['seed'])]=value
  if kind=='mature':group['windows'][str(job['seed'])]=job['mature']
  group['census']+=census(output)
 for key,group in result['groups'].items():
  group['metrics']={metric:A.stats(group['seeds'],metric) for metric in A.METRICS}
  group['completed_seed_count']=len(group['seeds'])
  expected={job['seed'] for job in jobs if job['factor']==group['factor'] and job['scenario']==group['layout']}
  group['expected_seed_count']=len(expected)
  group['complete']=len(group['seeds'])==len(expected)
  for metric,estimate in group['metrics'].items():
   if metric in ('simulation_cpu_s','simulation_wall_s','cpu_s','wall_s') and estimate['ci95'][0]>1:
    result['slowdowns'].append(dict(group=key,metric=metric,estimate=estimate,scope='Descriptive slowdown vs zero; investigate regardless of5% threshold',provisional=not group['complete']))
   if estimate.get('credible_over5percent_regression'):result['regressions'].append(dict(group=key,metric=metric,estimate=estimate))
 result['complete']=result['completed_jobs']==result['expected_jobs'] and bool(jobs)
 return result

def mature():
 root=ART/'mature';planpath=root/'plan.json'
 if not planpath.exists():return dict(kind='mature',state='pending plan',expected_jobs=96,completed_jobs=0,accepted=False)
 plan=load(planpath);jobpath=root/'performance-jobs.json'
 jobfiles=sorted((root/'performance-jobs').glob('*.json'))
 jobs=[load(p) for p in jobfiles] if jobfiles else load(jobpath) if jobpath.exists() else []
 result=grouped(jobs,'mature')
 result['planned_expected_jobs']=plan['worklist']['paired_window_jobs']
 result['complete']=result['complete'] and len(jobs)==result['planned_expected_jobs']
 result['state']='complete' if result['complete'] else 'partial/pending'
 result['provenance']=[source(planpath),source(jobpath),source(root/'parallel-correctness/complete.json')]+[source(p) for p in jobfiles]
 result['planned_dimensions']=dict(checkpoints=[dict(id=c['id'],tick=c['target_tick'],seed=c['seed'],layout=c['scenario']['id']) for c in plan['checkpoints']],windows=plan['window_lengths'],gpu=plan['gpu_strata'])
 result['scope']='LOADED checkpoint windows rebuild allocator/derived cache state; cannot represent continuous-process lifetime/RSS or full-game completion performance.'
 return result

def full():
 root=ART/'mature';planpath=root/'plan.json'
 if not planpath.exists():return dict(kind='full-native',state='pending plan',expected_cases=18,completed_pairs=0,accepted=False)
 plan=load(planpath);cases=plan['full_cases'];result=dict(kind='full-native',state='partial/pending',expected_cases=len(cases),completed_pairs=0,accepted=False,cases=[],groups={},censors=[],missing=[],failures=[],provenance=[source(planpath),source(root/'full-native-complete.json'),source(root/'full-diagnostic-complete.json')])
 completed={}
 for case in cases:
  directory=root/'full-native'/case['id'];p=directory/'measurement.json'
  if not p.exists():result['missing'].append(dict(case=case['id'],reason='native measurement pending'));continue
  record=load(p);item=dict(id=case['id'],layout=case['scenario']['id'],seed=case['seed'],variant=case['variant'],source=source(p),census=census(directory))
  require(not record['diagnostic'] and not record['timing_excluded'],'diagnostic timing cannot be pooled as native')
  require(record['input_verification']['unchanged'] and record['input_verification']['before']==record['input_verification']['after'],'full native inputs not verified')
  require(record['job']['variant']==case['variant'] and record['job']['affinity']==case['affinity'] and record['job']['seed']==case['seed'] and record['job']['scenario']==case['scenario'],'full native case identity mismatch')
  if record['timed_out']:
   item.update(state='right-censored one-hour timeout',wall_s=record['wall_s'],cpu_s=record['cpu_s'],timing_excluded_from_completion_means=True)
   result['censors'].append(item);result['cases'].append(item);continue
  if record['returncode'] or record.get('rss_censored') or not record.get('result'):
   item.update(state='failed/censoredRSS',returncode=record['returncode'],rss_censored=record.get('rss_censored'));result['failures'].append(item);result['cases'].append(item);continue
  replaypath=directory/'replay-validation.json'
  if not replaypath.exists():result['missing'].append(dict(case=case['id'],reason='post-timing bounded replay validation pending'));continue
  native=record['result'];require(native['game_seed']==case['seed'] and native['termination']=='engine_end' and native['winning_teams'],'native case has no natural winner')
  replay=load(replaypath)
  require(digest(directory/'game.replay')==replay['replay_sha256'],'native replay changed')
  item.update(state='completed native; pair comparison pending',ticks=native['ticks'],winner=native['winning_teams'],termination=native['termination'],wall_s=record['wall_s'],cpu_s=record['cpu_s'],peak_rss_bytes=record['peak_rss_bytes'],cpu_per_tick_s=record['cpu_s']/native['ticks'],replay=replay,replay_source=source(replaypath),affinity=case['affinity'])
  result['cases'].append(item);completed[(item['layout'],item['seed'],item['variant'])]=item
 for layout,seed in sorted({(c['scenario']['id'],c['seed']) for c in cases}):
  pair=[completed.get((layout,seed,name)) for name in ('baseline','combined')]
  if not all(pair):continue
  before,after=pair
  require((before['ticks'],before['winner'],before['termination'])==(after['ticks'],after['winner'],after['termination']),'full native natural outcome diverges')
  require(before['affinity']==after['affinity'] and before['replay']==after['replay'],'native pair mask/replay mismatch')
  result['completed_pairs']+=1
  group=result['groups'].setdefault(layout,dict(seeds={},completed_seed_count=0,expected_seed_count=len({c['seed'] for c in cases if c['scenario']['id']==layout})))
  value=dict(seed=seed,metrics={},ticks=before['ticks'],winner=before['winner'],native_replay_gate=True,scope='Native winner/end/ticks and canonical replay/AIT1 equality; full every-tick detailedGCS remains separate diagnostic gate')
  for metric in ('cpu_s','wall_s','peak_rss_bytes'):
   value['metrics'][metric]=dict(paired_log_ratios=[math.log(after[metric]/before[metric])],baseline_values=[before[metric]],candidate_values=[after[metric]])
  group['seeds'][str(seed)]=value
 for group in result['groups'].values():
  group['completed_seed_count']=len(group['seeds']);group['metrics']={m:A.stats(group['seeds'],m) for m in ('cpu_s','wall_s','peak_rss_bytes')}
  # Completion means include only complete matched uncensored pairs.
  group['completion_means']={name:{m:sum(s['metrics'][m][name+'_values'][0] for s in group['seeds'].values())/len(group['seeds']) for m in ('cpu_s','wall_s','peak_rss_bytes')} for name in ('baseline','candidate')}
 result['complete']=result['completed_pairs']==len(cases)//2
 result['state']='complete native pairs; diagnostic coverage separate' if result['complete'] else 'partial/pending; censors explicitly excluded'
 result['scope']='Continuous from frozen tick0 to naturalwinner; includes canonical replay and startup/teardown.9matchedseedpairs planned;1rep each. Native outcome/replay equality is not every-native-tick detailed verification.'
 return result

def replication():
 root=ART/'telemetry-replication';planpath=root/'plan.json'
 if not planpath.exists():return dict(kind='replication',state='pending plan',accepted=False)
 plan=load(planpath);selected=root/'selected-worklist.json'
 # All immutable launched jobs are used, not only last selected batch.
 launched=[load(p) for p in (root/'jobs').glob('*.json')];byid={j['id']:j for j in launched}
 jobs=[byid.get(j['id'],j) for j in plan['jobs']]
 result=grouped(jobs,'replication');result['state']='complete' if result['complete'] else 'partial/pending';result['provenance']=[source(planpath),source(selected)]
 result['reused_v1_evidence']=plan['reused_v1_jobs'];result['reuse_scope']='Prior18V1jobs retained as initial evidence; not pooled as independently replicated observations.'
 result['scope']='Fresh baseline/V1/V2/latercombined; original2+thirdholdoutlargeMixed with6balanced repeats, others3repeats; RSS/run scopes remain distinct.'
 return result

def followup():
 root=ART/'v2-followup';planpath=root/'plan.json'
 if not planpath.exists():return dict(kind='v2-followup',state='pending plan',expected_jobs=68,completed_jobs=0,accepted=False)
 plan=load(planpath);launched={j['id']:j for j in [load(p) for p in (root/'jobs').glob('*.json')]};jobs=[]
 for planned in plan['jobs']:
  job=dict(launched.get(planned['id'],planned));context=job['context']
  job['factor']=context['cohort']+'/'+context['candidate']+'-vs-'+context['comparator']+'/gpu'+context['gpu']+'/w'+str(context['window'])+'/checkpoint'+str(context['checkpoint_tick'])
  jobs.append(job)
 result=grouped(jobs,'replication');result['kind']='v2-followup';result['state']='complete' if result['complete'] else 'partial/pending'
 result['expected_jobs']=plan['total_jobs'];result['provenance']=[source(planpath),source(root/'status.json')]
 result['contexts']={j['id']:j['context'] for j in plan['jobs']}
 result['scope']='Finite68pairedjobs. Comparator, cohort, GPU, checkpoint and window are separate context strata; baseline-vs-V2 and V1-vs-V2 never pooled. Loadedmatureallocator/cache reconstruction distinct from continuous full games.'
 return result

def render(results):
 chunks=['<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Mature and replication evidence</title><style>body{font:15px/1.5 system-ui;margin:30px;color:#20314b}table{border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #ccc;text-align:left}.note{padding:15px;background:#fff5cb}.slow{color:#a22222}</style><h1>Mature, full-native and telemetry replication evidence</h1><p class="note">Conditional review only. Pending/partial cells are explicit. No acceptance or universal speed conclusion; a credible slowdown is investigated regardless of a5% alert threshold.</p>']
 for name,result in results.items():
  if name=='generated_utc':continue
  chunks.append('<h2>'+html.escape(name)+'</h2><p>'+html.escape(result.get('state','pending'))+'</p><p>'+html.escape(result.get('scope','No measurements available yet.'))+'</p>')
  if name=='mature':chunks.append('<p>LOADED checkpoint windows reset prior allocator/derived-cache lifetimes; continuous native full games are a separate cohort.</p>')
  if name=='full-native':chunks.append(f"<p>Complete matched pairs: {result.get('completed_pairs',0)}; explicit timeout censors: {len(result.get('censors',[]))}; pending cases: {len(result.get('missing',[]))}.</p>")
  else:chunks.append(f"<p>Verified jobs: {result.get('completed_jobs',0)}/{result.get('planned_expected_jobs',result.get('expected_jobs','pending'))}.</p>")
  chunks.append('<table><tr><th>Checkpoint/window/factor/layout</th><th>Seeds</th><th>Whole CPU%</th><th>E2E wall%</th><th>RSS%</th></tr>')
  for key,group in result.get('groups',{}).items():
   cells=[]
   for m in ('cpu_s','wall_s','peak_rss_bytes'):
    v=group['metrics'][m];lo,hi=v['percent_ci95'];cells.append(f"{v['percent_change']:+.2f} [{lo:+.2f},{hi:+.2f}]")
   chunks.append('<tr><td>'+html.escape(key)+'</td><td>'+str(group['completed_seed_count'])+'/'+str(group['expected_seed_count'])+'</td>'+''.join('<td>'+v+'</td>' for v in cells)+'</tr>')
  chunks.append('</table>')
  for slowdown in result.get('slowdowns',[]):chunks.append('<p class="slow">Investigate '+html.escape(slowdown['group']+'/'+slowdown['metric'])+' descriptive interval above no-change; this is not waived by the5% threshold.</p>')
  chunks.append('<ul>'+''.join('<li>'+html.escape(s['path'])+': '+('hashed source present' if s['exists'] else 'pending source')+'</li>' for s in result.get('provenance',[]))+'</ul>')
 for name,result in results.items():
  if isinstance(result,dict) and result.get('complete') and (ART/f'{name}-extension.svg').exists():
   chunks.append('<p>'+html.escape(name)+' plots: <a href="'+name+'-extension.svg">SVG</a> / <a href="'+name+'-extension.png">PNG</a>.</p>')
 chunks+=['<p>Descriptive seed→paired-repeat bootstrap RNG7134;4000draws. Fewseeds/repeats and many comparisons limit inference. Instrumented census counters are separate ownership/capacity observations and cannot be subtracted from RSS. Replays preserve winner/order/AIT1 evidence; native timings have no per-tick GCS observer unless explicitly diagnostic.</p><p><a href="extension-analysis.json">All pairs, scopes, censors, diagnostics and source hashes</a> · <a href="optimization-report.html">Complete initial matrix</a></p></html>']
 return ''.join(chunks)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plots',action='store_true');args=p.parse_args();os.sched_setaffinity(0,{15,31})
 results={'generated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'mature':mature(),'full-native':full(),'replication':replication(),'v2-followup':followup()}
 if args.plots:
  # Render measured groups only after each entire planned cohort completes.
  import matplotlib;matplotlib.use('Agg');import matplotlib.pyplot as plt
  for name,result in results.items():
   if not isinstance(result,dict) or not result.get('complete') or not result.get('groups'):continue
   fig,axes=plt.subplots(1,3,figsize=(18,max(4,len(result['groups'])*.35)),layout='constrained')
   keys=list(result['groups'])
   for ax,m in zip(axes,('cpu_s','wall_s','peak_rss_bytes')):
    values=[result['groups'][k]['metrics'][m] for k in keys];ys=[v['percent_change'] for v in values]
    ax.errorbar(ys,range(len(keys)),xerr=[[v['percent_change']-v['percent_ci95'][0] for v in values],[v['percent_ci95'][1]-v['percent_change'] for v in values]],fmt='o',capsize=3)
    ax.set_yticks(range(len(keys)),keys);ax.axvline(0,color='#555',lw=1);ax.set_title(A.METRICS[m]);ax.set_xlabel('Candidate change%;descriptive95% interval')
   fig.suptitle(name+'; acceptedfalse; scopes distinct')
   for ext in ('png','svg'):fig.savefig(ART/f'{name}-extension.{ext}',dpi=150)
   plt.close(fig)
 dump(ART/'extension-analysis.json',results);(ART/'extension-report.html').write_text(render(results))
 print(json.dumps({k:dict(state=v.get('state'),jobs=v.get('completed_jobs'),pairs=v.get('completed_pairs')) for k,v in results.items() if isinstance(v,dict)}))
if __name__=='__main__':main()
