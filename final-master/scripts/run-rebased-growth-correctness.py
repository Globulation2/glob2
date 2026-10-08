from pathlib import Path
import os,sys,json
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();base=root/'artifacts/resource-growth/final-master';cases=json.loads((base/'manifest.json').read_text())['scenarios'];rows=[]
for s in cases:
 for name in ['master','owner','shared']:
  dest=base/'correctness'/s['id']/name
  args=s['args']+['--compute-threads','4','--telemetry','checksums','--save','final']
  if name!='master':args+=['--resource-growth-delay','8','--resource-growth-execution',name]
  cwd=base/'master-src' if name=='master' else root
  if not (dest/'result.json').exists():execute(cwd/'build/linux/client/release/src/glob2',args,dest,cwd=cwd)
  result=json.loads((dest/'result.json').read_text());assert result['ticks']==1024
  rows.append({'scenario':s['id'],'variant':name,'result':result});print(s['id'],name,'pass',flush=True)
 a=base/'correctness'/s['id']/'owner';b=base/'correctness'/s['id']/'shared'
 for f in ['world.checksums','game.replay.checksums']:assert digest(a/f)==digest(b/f),(s['id'],f)
(base/'correctness/results.json').write_text(json.dumps(rows,indent=2)+'\n')
