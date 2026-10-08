from pathlib import Path
import sys,json,os
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';original=base/'timing';out=base/'extended';out.mkdir(exist_ok=True)
selection=json.load(open(base/'extension-selection.json'))
meta=json.load(open(original/'metadata.json'));assert meta['completed']
meta.update(extension_selection=selection,extension_repeats=20,extension_warmups=1,affinity=sorted(os.sched_getaffinity(0)),completed=False)
for name,data in meta['binaries'].items():assert digest(data['path'])==data['sha256']
(out/'metadata.json').write_text(json.dumps(meta,indent=2))
logpath=out/'measurements.jsonl'
if not logpath.exists():logpath.write_bytes((original/'measurements.jsonl').read_bytes())
rows=[json.loads(l) for l in logpath.read_text().splitlines()];keys={(r['scenario'],r['candidate'],r['repeat'],r['variant']) for r in rows}
with logpath.open('a') as log:
 for s in meta['scenarios']:
  candidates=selection.get(s['id'],[])
  for p,h in s['fixture_sha256'].items():assert digest(p)==h
  for rep in [-2,*range(10,30)]:
   if not candidates:break
   shift=(rep+2)%len(candidates);order=candidates[shift:]+candidates[:shift]
   for candidate in order:
    pair=['baseline',candidate] if (rep+candidates.index(candidate))%2 else [candidate,'baseline']
    for name in pair:
     key=(s['id'],candidate,rep,name)
     if key in keys:continue
     dest=out/s['id']/candidate/str(rep)/name
     if dest.exists():dest=dest/'retry'
     args=s['args']+['--compute-threads','4','--benchmark-warmup','0','--resource-growth-delay','8','--resource-growth-execution','shared']
     before=host_cpu_snapshot();frequency=frequency_snapshot()
     row={'scenario':s['id'],'candidate':candidate,'repeat':rep,'variant':name,**execute(Path(meta['binaries'][name]['path']),args,dest,cwd=root)}
     row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);row['frequency_before']=frequency;row['frequency_after']=frequency_snapshot()
     assert row['result']['ticks']==1024
     rows.append(row);keys.add(key);log.write(json.dumps(row)+'\n');log.flush()
     print('extended',s['id'],candidate,rep,name,round(row['result']['run_ns']/1e6),'ms',flush=True)
for name,data in meta['binaries'].items():assert digest(data['path'])==data['sha256']
for s in meta['scenarios']:
 selected=[r for r in rows if r['scenario']==s['id'] and r['variant']!='master']
 fields=['finalChecksum','growth_sampled','growth_proposals','growth_accepted','growth_rejected','growth_clamped','growth_stockAdded','growth_tilesAdded']
 assert len({tuple(r['result'][k] for k in fields) for r in selected})==1
meta.update(completed=True,checksums_and_growth_match=True);(out/'metadata.json').write_text(json.dumps(meta,indent=2))
