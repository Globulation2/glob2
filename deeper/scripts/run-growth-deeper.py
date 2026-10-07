from pathlib import Path
import sys,json,os,statistics as S
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot
from benchmark_resource_growth import interval
root=Path.cwd();base=root/'artifacts/resource-growth/deeper';kind=sys.argv[1];out=base/kind;out.mkdir(exist_ok=False)
scenarios=json.load(open(root/'artifacts/resource-growth/profiling/native/metadata.json'))['scenarios']
if kind=='stages':
 variants=[('legacy',base/'legacy-stages',root/'artifacts/resource-growth/baseline-src'),('current',base/'current-stages',root),('layout',base/'layout-stages',root)];repeats=5
else:variants=[('current',base/'before-layout-glob2',root),('layout',base/'layout-glob2',root)];repeats=10
meta={'scenarios':scenarios,'binaries':{str(p):digest(p) for _,p,_ in variants},'repeats':repeats,'affinity':sorted(os.sched_getaffinity(0)),'kind':kind};(out/'metadata.json').write_text(json.dumps(meta,indent=2));rows=[]
for s in scenarios:
 for p,h in s['fixture_sha256'].items():assert digest(p)==h
with (out/'measurements.jsonl').open('w') as f:
 for s in scenarios:
  for rep in range(-1,repeats):
   shift=(rep+1)%len(variants);order=variants[shift:]+variants[:shift]
   if rep%2:order=order[::-1]
   for name,exe,cwd in order:
    d=out/s['id']/str(rep)/name;args=s['args']+['--compute-threads','4','--benchmark-warmup','0']
    if name!='legacy':args+=['--resource-growth-delay','8','--resource-growth-execution','shared']
    if kind=='stages':os.environ['GLOB2_STAGE_CPU']=str(d/'stage-cpu.json')
    before=host_cpu_snapshot();frequency=frequency_snapshot();row={'scenario':s['id'],'repeat':rep,'variant':name,**execute(exe,args,d,cwd=cwd)}
    row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);row['frequency_before']=frequency;row['frequency_after']=frequency_snapshot()
    if kind=='stages':row['stage_cpu']=json.loads((d/'stage-cpu.json').read_text())
    assert row['result']['ticks']==1024
    rows.append(row);f.write(json.dumps(row)+'\n');f.flush();print(kind,s['id'],rep,name,round(row['result']['benchmark_run_cpu_ns']/1e6),'CPU ms',flush=True)
for p,h in meta['binaries'].items():assert digest(p)==h
for s in scenarios:
 sig={json.dumps({k:r['result'][k] for k in ['finalChecksum','growth_sampled','growth_proposals','growth_accepted','growth_stockAdded']},sort_keys=True) for r in rows if r['scenario']==s['id'] and r['variant']!='legacy'}
 assert len(sig)==1,(s['id'],sig)
meta['completed']=True;meta['matching_layout_checksums_and_counters']=True;(out/'metadata.json').write_text(json.dumps(meta,indent=2))
