from pathlib import Path
import json,sys
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'extra-verification';out.mkdir(exist_ok=True)
rows=[]
for s in json.load(open(base/'manifest.json'))['scenarios']:
 args=s['args'].copy();args[args.index('--ticks')+1]='128'
 for variant,threads in [('master',4),('original',1),('original',2),('original',4),('original',8),('direct',4)]:
  cwd=base/('latest-master-src' if variant=='master' else 'optimized-original-src') if variant!='direct' else root
  exe=cwd/'build/linux/client/release/src/glob2' if variant!='direct' else base/'production-direct'
  options=args+['--compute-threads',str(threads),'--telemetry','checksums']
  if variant=='direct':options+=['--resource-growth-delay','8','--resource-growth-execution','owner']
  dest=out/s['id']/f'{variant}-{threads}'
  if not (dest/'result.json').exists():execute(exe,options,dest,cwd=cwd)
  reference=out/s['id']/'master-4' if variant!='direct' else base/'verification'/s['id']/'8'/'baseline-shared-4'
  fields=['game.replay.checksums'] if variant!='direct' else ['world.checksums','game.replay.checksums']
  assert all(digest(dest/f)==digest(reference/f) for f in fields),(s['id'],variant,threads)
  rows.append({'scenario':s['id'],'variant':variant,'threads':threads,'binary_sha256':digest(exe),'result':json.load(open(dest/'result.json')),'matching_traces':fields})
 print(s['id'],'original and direct controls match',flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2))
