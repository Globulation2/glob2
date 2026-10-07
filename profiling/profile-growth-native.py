import json,os,sys,time,statistics as S
from pathlib import Path
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot
from benchmark_resource_growth import interval
root=Path.cwd();out=root/'artifacts/resource-growth/profiling/native';out.mkdir(exist_ok=False)
a=json.load(open('artifacts/resource-growth/controlled-fixtures/manifest.json'))['scenarios'];b=json.load(open('artifacts/resource-growth/fixtures/manifest-256.json'))['scenarios']
scenarios=[next(s for s in a if s['id']==n) for n in ['dense','multi']]+[next(s for s in b if s['id']==n) for n in ['ai512','disabled512']]
for s in scenarios:s['args'][s['args'].index('--ticks')+1]='1024'
variants=[('legacy',root/'artifacts/resource-growth/baseline-glob2',root/'artifacts/resource-growth/baseline-src',[]),('owner',root/'build/linux/client/release/src/glob2',root,['--resource-growth-delay','8','--resource-growth-execution','owner']),('shared',root/'build/linux/client/release/src/glob2',root,['--resource-growth-delay','8','--resource-growth-execution','shared'])]
meta={'scenarios':scenarios,'affinity':sorted(os.sched_getaffinity(0)),'binaries':{str(p):digest(p) for _,p,_,_ in variants},'repetitions':10,'warmups':1,'source':os.popen('git rev-parse HEAD').read().strip()}
(out/'metadata.json').write_text(json.dumps(meta,indent=2));rows=[]
with (out/'measurements.jsonl').open('w') as f:
 for s in scenarios:
  for rep in range(-1,10):
   order=variants[(rep+1)%3:]+variants[:(rep+1)%3]
   if rep%2:order=order[::-1]
   for name,exe,cwd,flags in order:
    before=host_cpu_snapshot();freq=frequency_snapshot()
    row={'scenario':s['id'],'repeat':rep,'variant':name,**execute(exe,s['args']+flags+['--compute-threads','4','--benchmark-warmup','0'],out/s['id']/str(rep)/name,cwd=cwd)}
    after=host_cpu_snapshot();row['host_activity']=host_cpu_activity(before,after,row['cpu_s']);row['frequency_before']=freq;row['frequency_after']=frequency_snapshot()
    if row['result']['ticks']!=1024:raise RuntimeError('early end')
    rows.append(row);f.write(json.dumps(row)+'\n');f.flush()
    print(s['id'],rep,name,round(1024e9/row['result']['run_ns'],1),'ticks/s',flush=True)
for p,h in meta['binaries'].items():assert digest(p)==h
summary={}
for s in scenarios:
 rs={v:{r['repeat']:r for r in rows if r['scenario']==s['id'] and r['variant']==v and r['repeat']>=0} for v,_,_,_ in variants};entry={}
 for v,r in rs.items():
  e={k:S.median(a['result'][k] for a in r.values()) for k in ['run_ns','benchmark_run_cpu_ns']};e['ticks_per_second']=S.median(1024e9/a['result']['run_ns'] for a in r.values())
  for control in ['legacy','owner']:
   ratios=[a['result']['run_ns']/rs[control][i]['result']['run_ns'] for i,a in r.items()];cpus=[a['result']['benchmark_run_cpu_ns']/rs[control][i]['result']['benchmark_run_cpu_ns'] for i,a in r.items()]
   e[control]={'elapsed_ratio':S.median(ratios),'ci':interval(ratios),'cpu_ratio':S.median(cpus),'cpu_ci':interval(cpus)}
  entry[v]=e
 summary[s['id']]=entry
(out/'summary.json').write_text(json.dumps(summary,indent=2));meta['completed']=True;(out/'metadata.json').write_text(json.dumps(meta,indent=2))
