from pathlib import Path
import json,sys,subprocess,os
sys.path.insert(0,'test');from benchmark_parallel_compute import execute
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'master-work';out.mkdir(exist_ok=True)
scenarios=json.load(open(base/'manifest.json'))['scenarios'];rows=[]
for s in scenarios:
 for tag in ('baseline','master'):
  exe=base/'baseline/glob2' if tag=='baseline' else base/'master-src/build/linux/client/release/src/glob2'
  args=s['args']+['--compute-threads','4','--telemetry','checksums','--save','final']
  if tag=='baseline':args+=['--resource-growth-delay','8','--resource-growth-execution','shared']
  dest=out/s['id']/tag;cwd=base/'master-src' if tag=='master' else root
  if not (dest/'result.json').exists():execute(exe,args,dest,cwd=cwd)
  with (dest/'inspect.log').open('w') as f:
   subprocess.run([str(base/'control-components'),'--test-case=inspect remaining experiment saves*','--no-skip'],env=dict(os.environ,GLOB2_REMAINING_INSPECT_INPUT=str(dest/'final.game'),GLOB2_REMAINING_INSPECT_OUTPUT=str(dest/'stocks.json')),stdout=f,stderr=subprocess.STDOUT,check=True)
  rows.append({'scenario':s['id'],'variant':tag,'result':json.load(open(dest/'result.json')),'stocks':json.load(open(dest/'stocks.json'))})
  print('master work',s['id'],tag,'complete',flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2))
