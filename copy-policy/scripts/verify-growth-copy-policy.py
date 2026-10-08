from pathlib import Path
import sys,json
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
root=Path.cwd();out=root/'artifacts/resource-growth/copy-policy/verification';out.mkdir(exist_ok=True)
scenarios=json.load(open(root/'artifacts/resource-growth/simple/timing/metadata.json'))['scenarios']
rows=[]
for s in scenarios:
 for delay in (1,3,8):
  ref=None
  for name,exe,mode,threads in [('before',out.parent/'before-glob2','shared',4),('owner',root/'build/linux/client/release/src/glob2','owner',4)]+[(f'shared{t}',root/'build/linux/client/release/src/glob2','shared',t) for t in (1,2,4,8)]:
   args=s['args'].copy();args[args.index('--ticks')+1]='128'
   args+=['--resource-growth-delay',str(delay),'--resource-growth-execution',mode,'--compute-threads',str(threads),'--telemetry','checksums']
   dest=out/s['id']/str(delay)/name
   row={'scenario':s['id'],'delay':delay,'variant':name,**execute(exe,args,dest,cwd=root)}
   signature=[digest(dest/f) for f in ('world.checksums','game.replay.checksums')]
   if ref is None:ref=signature
   assert signature==ref,(s['id'],delay,name)
   rows.append(row);print(s['id'],delay,name,'match',flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2))
