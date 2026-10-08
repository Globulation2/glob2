from pathlib import Path
import sys,json,os,subprocess
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot
root=Path.cwd();base=root/'artifacts/resource-growth/attribution-v2'
scenarios=json.load(open(root/'artifacts/resource-growth/simple/timing/metadata.json'))['scenarios']
kind='copy-policy'
variants=[('before',base.parent/'copy-policy/before-glob2',root),('after',root/'build/linux/client/release/src/glob2',root)];repeats=10
out=base.parent/'copy-policy/timing';out.mkdir(exist_ok=True)
prior_meta=json.load(open(out/'metadata.json')) if (out/'metadata.json').exists() else None
meta={'scenarios':scenarios,'binaries':{str(p):digest(p) for _,p,_ in variants},'repeats':repeats,'affinity':sorted(os.sched_getaffinity(0)),'source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()};(out/'metadata.json').write_text(json.dumps(meta,indent=2))
for s in scenarios:
 for p,h in s['fixture_sha256'].items():assert digest(p)==h
rows=[json.loads(line) for line in (out/'measurements.jsonl').read_text().splitlines()] if (out/'measurements.jsonl').exists() else []
if prior_meta:assert prior_meta['binaries']==meta['binaries']
with (out/'measurements.jsonl').open('a') as f:
 for s in scenarios:
  for rep in range(-1,repeats):
   shift=(rep+1)%len(variants);order=variants[shift:]+variants[:shift]
   for name,exe,cwd in order:
    if any(r['scenario']==s['id'] and r['repeat']==rep and r['variant']==name for r in rows):continue
    dest=out/s['id']/str(rep)/name;args=s['args']+['--compute-threads','4','--benchmark-warmup','0']
    if dest.exists():dest=dest/'resume'
    if not name.startswith('legacy'):args+=['--resource-growth-delay','8','--resource-growth-execution','shared']
    if kind=='stages':os.environ['GLOB2_STAGE_CPU']=str(dest/'stage-cpu.json')
    before=host_cpu_snapshot();freq=frequency_snapshot()
    row={'scenario':s['id'],'repeat':rep,'variant':name,**execute(exe,args,dest,cwd=cwd)}
    row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);row['frequency_before']=freq;row['frequency_after']=frequency_snapshot()
    assert row['result']['ticks']==1024
    if kind=='stages':row['stage_cpu']=json.loads((dest/'stage-cpu.json').read_text())
    rows.append(row);f.write(json.dumps(row)+'\n');f.flush();print(kind,s['id'],rep,name,round(row['result']['run_ns']/1e6),'ms',flush=True)
for p,h in meta['binaries'].items():assert digest(p)==h
if kind not in ['stages','fair']:
 for s in scenarios:
  keys=['growth_sampled','growth_proposals','growth_accepted','growth_rejected','growth_stockAdded','growth_tilesAdded']
  signatures={json.dumps({k:r['result'][k] for k in keys},sort_keys=True) for r in rows if r['scenario']==s['id']}
  assert len(signatures)==1,(s['id'],signatures)
 meta['matching_growth_counters']=True
 if kind=='copy-policy':
  for s in scenarios:
   checks={r['result']['finalChecksum'] for r in rows if r['scenario']==s['id']}
   assert len(checks)==1,(s['id'],checks)
  meta['matching_final_checksums']=True
meta['completed']=True;(out/'metadata.json').write_text(json.dumps(meta,indent=2))
