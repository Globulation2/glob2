from pathlib import Path
import json,sys,subprocess,os
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();b=root/'artifacts/resource-growth/remaining';rows=[]
for s in json.load(open(b/'manifest.json'))['scenarios']:
 for name,exe,cwd,extra in [('final-retained-work',b/'final-retained',root,['--resource-growth-delay','8','--resource-growth-execution','shared']),('final-master-work',b/'final-master-src/build/linux/client/release/src/glob2',b/'final-master-src',[])]:
  dest=b/name/s['id'];dest.parent.mkdir(exist_ok=True)
  if not (dest/'result.json').exists():execute(exe,s['args']+['--compute-threads','4','--telemetry','checksums','--save','final']+extra,dest,cwd=cwd)
  with (dest/'inspect.log').open('w') as f:subprocess.run([str(b/'final-inspector'),'--test-case=inspect remaining experiment saves*','--no-skip'],cwd=root,env=dict(os.environ,GLOB2_REMAINING_INSPECT_INPUT=str(dest/'final.game'),GLOB2_REMAINING_INSPECT_OUTPUT=str(dest/'stocks.json')),stdout=f,stderr=subprocess.STDOUT,check=True)
  rows.append({'scenario':s['id'],'variant':name,'binary_sha256':digest(exe),'result':json.load(open(dest/'result.json')),'stocks':json.load(open(dest/'stocks.json'))});print(name,s['id'],flush=True)
(b/'final-retained-work/results.json').write_text(json.dumps(rows,indent=2))
