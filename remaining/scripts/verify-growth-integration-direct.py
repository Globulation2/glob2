from pathlib import Path
import sys,json,subprocess
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'integration-direct-verification';out.mkdir(exist_ok=True);rows=[]
with (out/'tests.log').open('w') as f:subprocess.run([str(base/'integration-direct-tests'),'--test-suite=ResourceGrowth','--reporters=console'],cwd=base/'integration-src',stdout=f,stderr=subprocess.STDOUT,check=True)
for s in json.load(open(base/'manifest.json'))['scenarios']:
 args=s['args'].copy();args[args.index('--ticks')+1]='128';args+=['--compute-threads','4','--resource-growth-delay','8','--resource-growth-execution','owner','--telemetry','checksums']
 dest=out/s['id'];run=execute(base/'integration-direct',args,dest,cwd=base/'integration-src');ref=base/'verification'/s['id']/'8'/'baseline-shared-4'
 assert all(digest(dest/f)==digest(ref/f) for f in ['world.checksums','game.replay.checksums'])
 rows.append({'scenario':s['id'],**run});print(s['id'],'match',flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2))
