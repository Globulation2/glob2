from pathlib import Path
import json,sys
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();b=root/'artifacts/resource-growth/remaining';cwd=b/'current-master-src';exe=cwd/'build/linux/client/release/src/glob2';out=b/'current-master-work';out.mkdir(exist_ok=True);rows=[]
for s in json.load(open(b/'manifest.json'))['scenarios']:
 dest=out/s['id'];args=s['args']+['--compute-threads','4','--telemetry','checksums','--save','final']
 run=execute(exe,args,dest,cwd=cwd);old=b/'latest-master-work'/s['id']/'game.replay.checksums'
 match=digest(dest/'game.replay.checksums')==digest(old)
 rows.append({'scenario':s['id'],'revision':'6487b873dd3f29ad3ab75c7513597fa47905c132','binary_sha256':digest(exe),'replay_matches_0f1':match,**run});print(s['id'],'matches old master:',match,flush=True)
 assert match
(out/'results.json').write_text(json.dumps(rows,indent=2))
