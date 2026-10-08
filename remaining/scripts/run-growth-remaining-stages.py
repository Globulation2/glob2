from pathlib import Path
import sys,json,os
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'stages';out.mkdir(exist_ok=True)
scenarios=[s for s in json.load(open(base/'manifest.json'))['scenarios'] if s['id'] in ['dense','multi','ai512','disabled512']];variants=['control','ABCD']
meta={'scenarios':scenarios,'repeats':3,'warmups':1,'purpose':'separate attribution; not primary timing or adoption evidence','affinity':sorted(os.sched_getaffinity(0)),'binaries':{v:digest(base/(v+'-stages')) for v in variants}}
(out/'metadata.json').write_text(json.dumps(meta,indent=2));rows=[]
with (out/'measurements.jsonl').open('w') as log:
 for s in scenarios:
  for p,h in s['fixture_sha256'].items():assert digest(p)==h
  for rep in range(-1,3):
   shift=(rep+1)%len(variants)
   for variant in variants[shift:]+variants[:shift]:
    dest=out/s['id']/str(rep)/variant;os.environ['GLOB2_STAGE_CPU']=str(dest/'stages.json')
    args=s['args']+['--compute-threads','4','--benchmark-warmup','0','--resource-growth-delay','8','--resource-growth-execution','shared']
    row={'scenario':s['id'],'repeat':rep,'variant':variant,**execute(base/(variant+'-stages'),args,dest,cwd=root)}
    row['stages']=json.load(open(dest/'stages.json'));row['snapshot']=json.load(open(dest/'stages.json.snapshots.json'))
    rows.append(row);log.write(json.dumps(row)+'\n');log.flush()
   print('stages',s['id'],rep,'complete',flush=True)
for variant,h in meta['binaries'].items():assert digest(base/(variant+'-stages'))==h
fields=['finalChecksum','growth_sampled','growth_proposals','growth_accepted','growth_rejected','growth_clamped','growth_stockAdded','growth_tilesAdded']
for s in scenarios:assert len({tuple(r['result'][k] for k in fields) for r in rows if r['scenario']==s['id']})==1
meta.update(completed=True,checksums_and_work_match=True);(out/'metadata.json').write_text(json.dumps(meta,indent=2))
