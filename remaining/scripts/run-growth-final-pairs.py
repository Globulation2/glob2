from pathlib import Path
import sys,json,os
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
from benchmark_resource_refactor import host_cpu_snapshot,host_cpu_activity,frequency_snapshot
root=Path.cwd();base=root/'artifacts/resource-growth/remaining'
plan=json.load(open(sys.argv[1]))
if plan['output']=='sensitivity' and (base/'defer-sensitivity').exists():
 print('Sensitivity sweep deferred until the fresh integrated combination passes qualification.',flush=True);sys.exit(0)
out=base/plan['output'];out.mkdir(exist_ok=True)
scenarios=json.load(open(base/'manifest.json'))['scenarios'];cases=plan['cases'];repeats=plan.get('repeats',10)
meta={'plan':plan,'scenarios':scenarios,'affinity':sorted(os.sched_getaffinity(0)),'binaries':{v['binary']:digest(v['binary']) for case in cases for v in (case['reference'],case['candidate'])}}
if (out/'metadata.json').exists():
 old=json.load(open(out/'metadata.json'));assert old['binaries']==meta['binaries'];assert old['plan']['cases']==plan['cases']
(out/'metadata.json').write_text(json.dumps(meta,indent=2))
path=out/'measurements.jsonl';rows=[json.loads(l) for l in path.read_text().splitlines()] if path.exists() else [];keys={(r['scenario'],r['case'],r['repeat'],r['variant']) for r in rows}
with path.open('a') as log:
 for s in scenarios:
  active=[c for c in cases if s['id'] in c['scenarios']]
  if not active:continue
  for p,h in s['fixture_sha256'].items():assert digest(p)==h
  for rep in range(-1,repeats):
   shift=(rep+1)%len(active)
   for c in active[shift:]+active[:shift]:
    pair=['reference','candidate'] if (rep+active.index(c))%2 else ['candidate','reference']
    for role in pair:
     key=(s['id'],c['id'],rep,role)
     if key in keys:continue
     v=c[role];dest=out/s['id']/c['id']/str(rep)/role
     if dest.exists():dest=dest/'retry'
     args=s['args']+v['args']+['--benchmark-warmup','0'];before=host_cpu_snapshot();frequency=frequency_snapshot()
     row={'scenario':s['id'],'case':c['id'],'repeat':rep,'variant':role,'name':v['name'],**execute(Path(v['binary']),args,dest,cwd=Path(v.get('cwd',str(root))))}
     row['host_activity']=host_cpu_activity(before,host_cpu_snapshot(),row['cpu_s']);row['frequency_before']=frequency;row['frequency_after']=frequency_snapshot()
     assert row['result']['ticks']==1024
     rows.append(row);keys.add(key);log.write(json.dumps(row)+'\n');log.flush();print(plan['output'],s['id'],c['id'],rep,role,round(row['result']['run_ns']/1e6),'ms',flush=True)
for p,h in meta['binaries'].items():assert digest(p)==h
fields=['finalChecksum','growth_sampled','growth_proposals','growth_accepted','growth_rejected','growth_clamped','growth_stockAdded','growth_tilesAdded']
for c in cases:
 if not c['same_behavior']:continue
 for s in c['scenarios']:
  selected=[r for r in rows if r['case']==c['id'] and r['scenario']==s]
  assert len({tuple(r['result'][k] for k in fields) for r in selected})==1,(c['id'],s)
meta.update(completed=True,matching_same_behavior_cases=True);(out/'metadata.json').write_text(json.dumps(meta,indent=2))
