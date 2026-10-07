from pathlib import Path
import json,os,sys,statistics as S
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot
from benchmark_resource_growth import interval
root=Path.cwd();out=root/'artifacts/resource-growth/simple/timing';out.mkdir(exist_ok=False)
scenarios=json.load(open(root/'artifacts/resource-growth/profiling/native/metadata.json'))['scenarios']
# Allow an evidence checkout to be relocated while retaining fixture hashes.
for scenario in scenarios:
 category='controlled-fixtures' if scenario['group']=='controlled' else 'fixtures'
 replacement=str(root/'artifacts/resource-growth'/category/scenario['id']/'initial.game.gz')
 original=scenario['fixture_sha256']
 scenario['args']=[replacement if arg in original else arg for arg in scenario['args']]
 scenario['fixture_sha256']={replacement:next(iter(original.values()))}

variants=[('legacy',root/'artifacts/resource-growth/baseline-glob2',root/'artifacts/resource-growth/baseline-src'),('previous',root/'artifacts/resource-growth/simple/before-glob2',root/'artifacts/resource-growth/baseline-src'),('owner',root/'build/linux/client/release/src/glob2',root),('shared',root/'build/linux/client/release/src/glob2',root)]
meta={'scenarios':scenarios,'binaries':{str(p):digest(p) for _,p,_ in variants},'repeats':10,'affinity':sorted(os.sched_getaffinity(0))};(out/'metadata.json').write_text(json.dumps(meta,indent=2));rows=[]
for s in scenarios:
 for p,h in s['fixture_sha256'].items():assert digest(p)==h
with (out/'measurements.jsonl').open('w') as f:
 for s in scenarios:
  for rep in range(-1,10):
   shift=(rep+1)%len(variants);order=variants[shift:]+variants[:shift]
   if rep%2:order=order[::-1]
   for name,exe,cwd in order:
    d=out/s['id']/str(rep)/name;args=s['args']+['--compute-threads','4','--benchmark-warmup','0']
    if name!='legacy':args+=['--resource-growth-delay','8','--resource-growth-execution','owner' if name=='owner' else 'shared']
    before=host_cpu_snapshot();frequency=frequency_snapshot();row={'scenario':s['id'],'repeat':rep,'variant':name,**execute(exe,args,d,cwd=cwd)}
    row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);row['frequency_before']=frequency;row['frequency_after']=frequency_snapshot()
    assert row['result']['ticks']==1024
    rows.append(row);f.write(json.dumps(row)+'\n');f.flush();print(s['id'],rep,name,round(row['result']['run_ns']/1e6),'ms',flush=True)
for p,h in meta['binaries'].items():assert digest(p)==h
for s in scenarios:
 sig={json.dumps({k:r['result'][k] for k in ['finalChecksum','growth_sampled','growth_proposals','growth_accepted','growth_stockAdded']},sort_keys=True) for r in rows if r['scenario']==s['id'] and r['variant'] in ['owner','shared']};assert len(sig)==1,(s['id'],sig)
meta['completed']=True;meta['matching_owner_shared_checksums_and_counters']=True;(out/'metadata.json').write_text(json.dumps(meta,indent=2))
summary={}
for s in scenarios:
 rs=[r for r in rows if r['scenario']==s['id'] and r['repeat']>=0];vs={v:{r['repeat']:r for r in rs if r['variant']==v} for v,_,_ in variants};d={}
 for v,x in vs.items():
  d[v]={'ticks_per_second':S.median(1024e9/r['result']['run_ns'] for r in x.values()),'cpu_ms':S.median(r['result']['benchmark_run_cpu_ns']/1e6 for r in x.values()),'peak_rss_bytes':S.median(r['peak_rss_bytes'] for r in x.values())}
  for k in ['growth_capacityGrowthBatches','growth_maxProposals','growth_proposals','growth_accepted','growth_stockAdded','growth_tilesAdded','growth_maxProposalBytes']:
   d[v][k]=S.median(r['result'].get(k,0) for r in x.values())
 for before in ['legacy','previous','owner']:
  ratios=[vs[before][i]['result']['run_ns']/vs['shared'][i]['result']['run_ns']-1 for i in vs[before]];cpu=[vs['shared'][i]['result']['benchmark_run_cpu_ns']/vs[before][i]['result']['benchmark_run_cpu_ns']-1 for i in vs[before]]
  d['shared-vs-'+before]={'throughput':S.median(ratios),'throughput_ci':interval(ratios),'cpu':S.median(cpu),'cpu_ci':interval(cpu)}
 summary[s['id']]=d
(out/'summary.json').write_text(json.dumps(summary,indent=2))
