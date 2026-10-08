from pathlib import Path
import json,sys,subprocess,os
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'latest-master-work';out.mkdir(exist_ok=True)
scenarios=json.load(open(base/'manifest.json'))['scenarios'];rows=[];exe=base/'latest-master-src/build/linux/client/release/src/glob2'
for s in scenarios:
 dest=out/s['id'];args=s['args']+['--compute-threads','4','--telemetry','checksums','--save','final']
 if not (dest/'result.json').exists():execute(exe,args,dest,cwd=base/'latest-master-src')
 with (dest/'inspect.log').open('w') as f:
  subprocess.run([str(base/'control-components'),'--test-case=inspect remaining experiment saves*','--no-skip'],env=dict(os.environ,GLOB2_REMAINING_INSPECT_INPUT=str(dest/'final.game'),GLOB2_REMAINING_INSPECT_OUTPUT=str(dest/'stocks.json')),stdout=f,stderr=subprocess.STDOUT,check=True)
 old=base/'master-work'/s['id']/'master';matching={n:(digest(dest/n)==digest(old/n) if (dest/n).exists() and (old/n).exists() else None) for n in ['world.checksums','game.replay.checksums']}
 rows.append({'scenario':s['id'],'revision':'0f1a2569ab7f23c8702a078978054f73f4ddb9cc','binary_sha256':digest(exe),'result':json.load(open(dest/'result.json')),'stocks':json.load(open(dest/'stocks.json')),'matches_previous_master':matching})
 print(s['id'],matching,flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2))
