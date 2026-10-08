from pathlib import Path
import sys,json,subprocess,os
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute,digest
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/os.environ.get('GROWTH_VERIFY_OUTPUT','verification');out.mkdir(exist_ok=True)
scenarios=json.load(open(base/'manifest.json'))['scenarios'];variants=os.environ.get('GROWTH_VERIFY_VARIANTS','control,A,B,C,D,AB,ABCD').split(',');rows=[]
for name in ([] if os.environ.get("GROWTH_VERIFY_ENGINE_ONLY") else variants):
 with (out/(name+'-tests.log')).open('w') as f:
  p=subprocess.run([str(base/(name+'-tests')),'--test-suite=ResourceGrowth,WorldSnapshot,ComputeExecutor','--reporters=console'],stdout=f,stderr=subprocess.STDOUT)
  if p.returncode:raise RuntimeError(name+' tests failed')
 print('focused tests',name,'pass',flush=True)
for s in scenarios:
 for delay in (1,3,8):
  ref=None
  choices=[('baseline',4,'shared')]+[(v,t,m) for v in variants for t,m in [(4,'owner'),(1,'shared'),(2,'shared'),(4,'shared'),(8,'shared')]]
  for name,threads,execution in choices:
   exe=base/'baseline/glob2' if name=='baseline' else base/name
   dest=out/s['id']/str(delay)/f'{name}-{execution}-{threads}'
   args=s['args'].copy();args[args.index('--ticks')+1]='128'
   args+=['--compute-threads',str(threads),'--resource-growth-delay',str(delay),'--resource-growth-execution',execution,'--telemetry','checksums']
   run=({'result':json.load(open(dest/'result.json')),'reused':True} if (dest/'result.json').exists() else execute(exe,args,dest,cwd=Path(os.environ.get('GROWTH_VERIFY_CWD',str(root)))))
   row={'scenario':s['id'],'delay':delay,'variant':name,'threads':threads,'execution':execution,**run}
   sig=[digest(dest/f) for f in ('world.checksums','game.replay.checksums')]
   if ref is None:ref=sig
   assert sig==ref,(s['id'],delay,name,threads,execution)
   rows.append(row)
  print('per-tick',s['id'],delay,'all match',flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2))
