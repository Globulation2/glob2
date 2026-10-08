from pathlib import Path
import sys,json,os,subprocess
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot,runtime_libraries
root=Path.cwd();base=root/'artifacts/resource-growth/remaining'
mode=sys.argv[1] if len(sys.argv)>1 else 'timing';out=base/mode;out.mkdir(exist_ok=True)
scenarios=json.load(open(base/'manifest.json'))['scenarios']
variants=sys.argv[2:] or ['control','A','B','C','D','AB','ABCD','master']
repeats=int(os.environ.get('GROWTH_REPEATS','10'))
meta={'scenarios':scenarios,'repeats':repeats,'variants':variants,'affinity':sorted(os.sched_getaffinity(0)),'binaries':{},'baseline_revision':'edb09d40204a1fdb3a6d0e5934ef34e9c344de60','master_revision':json.load(open(base/'baseline.json'))['master_revision']}
def binary(name):return base/'master-src/build/linux/client/release/src/glob2' if name=='master' else base/'baseline/glob2' if name=='baseline' else base/name
for name in variants+['baseline']:meta['binaries'][name]={'path':str(binary(name)),'sha256':digest(binary(name))}
previous=json.load(open(out/'metadata.json')) if (out/'metadata.json').exists() else None
if previous:assert previous['binaries']==meta['binaries']
(out/'metadata.json').write_text(json.dumps(meta,indent=2))
rows=[json.loads(l) for l in (out/'measurements.jsonl').read_text().splitlines()] if (out/'measurements.jsonl').exists() else []
keys={(r['scenario'],r['candidate'],r['repeat'],r['variant']) for r in rows}
for s in scenarios:
 for p,h in s['fixture_sha256'].items():assert digest(p)==h
with (out/'measurements.jsonl').open('a') as log:
 for s in scenarios:
  for rep in range(-1,repeats):
   shift=(rep+1)%len(variants);order=variants[shift:]+variants[:shift]
   for index,candidate in enumerate(order):
    pair=['baseline',candidate] if (rep+variants.index(candidate))%2 else [candidate,'baseline']
    for name in pair:
     key=(s['id'],candidate,rep,name)
     if key in keys:continue
     dest=out/s['id']/candidate/str(rep)/name
     if dest.exists():dest=dest/'retry'
     args=s['args']+['--compute-threads','4','--benchmark-warmup','0']
     if name!='master':args+=['--resource-growth-delay','8','--resource-growth-execution','shared']
     cwd=base/'master-src' if name=='master' else root
     before=host_cpu_snapshot();frequency=frequency_snapshot()
     row={'scenario':s['id'],'candidate':candidate,'repeat':rep,'variant':name,**execute(binary(name),args,dest,cwd=cwd)}
     row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);row['frequency_before']=frequency;row['frequency_after']=frequency_snapshot()
     assert row['result']['ticks']==1024
     rows.append(row);keys.add(key);log.write(json.dumps(row)+'\n');log.flush()
     print(mode,s['id'],candidate,rep,name,round(row['result']['run_ns']/1e6),'ms',flush=True)
for name,data in meta['binaries'].items():assert digest(data['path'])==data['sha256']
for s in scenarios:
 selected=[r for r in rows if r['scenario']==s['id'] and r['variant']!='master']
 checks={r['result']['finalChecksum'] for r in selected};assert len(checks)==1,(s['id'],checks)
 counters=['growth_sampled','growth_proposals','growth_accepted','growth_rejected','growth_clamped','growth_stockAdded','growth_tilesAdded']
 assert len({json.dumps({k:r['result'][k] for k in counters},sort_keys=True) for r in selected})==1
meta['completed']=True;meta['checksums_and_growth_match']=True;(out/'metadata.json').write_text(json.dumps(meta,indent=2))
