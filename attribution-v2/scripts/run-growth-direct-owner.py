from pathlib import Path
import sys,json,os,subprocess
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity
root=Path.cwd();base=root/'artifacts/resource-growth/attribution-v2';out=base/'direct-control';out.mkdir(exist_ok=False)
scenarios=json.load(open(root/'artifacts/resource-growth/simple/timing/metadata.json'))['scenarios'][:3]
variants=[('shared',base/'direct-owner-bulk','shared'),('owner-direct',base/'direct-owner-bulk','owner')]
meta={'scenarios':scenarios,'binaries':{str(p):digest(p) for _,p,_ in variants},'repeats':10,'affinity':sorted(os.sched_getaffinity(0))};(out/'metadata.json').write_text(json.dumps(meta,indent=2));rows=[]
with (out/'measurements.jsonl').open('w') as f:
 for s in scenarios:
  for rep in range(-1,10):
   shift=(rep+1)%len(variants);order=variants[shift:]+variants[:shift]
   if rep%2:order=order[::-1]
   for name,exe,mode in order:
    args=s['args']+['--compute-threads','4','--benchmark-warmup','0','--resource-growth-delay','8','--resource-growth-execution',mode]
    before=host_cpu_snapshot();row={'scenario':s['id'],'repeat':rep,'variant':name,**execute(exe,args,out/s['id']/str(rep)/name,cwd=root)}
    row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);assert row['result']['ticks']==1024
    rows.append(row);f.write(json.dumps(row)+'\n');f.flush();print(s['id'],rep,name,round(row['result']['run_ns']/1e6),'ms',flush=True)
for s in scenarios:
 signatures={json.dumps({k:r['result'][k] for k in ['finalChecksum','growth_sampled','growth_proposals','growth_accepted','growth_stockAdded','growth_tilesAdded']},sort_keys=True) for r in rows if r['scenario']==s['id']};assert len(signatures)==1,(s['id'],signatures)
for p,h in meta['binaries'].items():assert digest(p)==h
meta.update(completed=True,matching_checksums_and_growth=True);(out/'metadata.json').write_text(json.dumps(meta,indent=2))
