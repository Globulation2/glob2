import sys,json,gzip,shutil
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
platform=sys.argv[1]; binary=root/f'build/{platform}/client/release/src/glob2'
out=Path(sys.argv[2]).resolve();out.mkdir(exist_ok=True);rows=[]
fixtures={f'seed-{seed}':seed for seed in (19,42,171)}
for name,seed in fixtures.items():
 for delay in (4,8):
  expected=None
  for workers in (0,4):
   target=out/f'{name}-ai{delay}-w{workers}'
   args=['--map-file',str(root/'platform/apps/engine-agent/fixtures/ais/two.map.gz'),'--game-seed',str(seed),'--player','numbi','--player','castor','--ticks','512','--compute-experiments','ai' if workers else 'none','--compute-threads',str(max(workers,1)),'--gradient-workers',str(workers),'--experiment','building-gradient-pipeline','--rule','buildingGradientDelay=4','--ai-order-delay',str(delay),'--telemetry','checksums','--replay','true']
   row=execute(binary,args,target);trace=target/'game.replay.checksums';value=digest(trace)
   if expected is None:expected=value
   if value!=expected:raise RuntimeError(f'combined streams worker mismatch {target}')
   rows.append({'workload':name,'aiDelay':delay,'workers':workers,'traceSha256':value,'command':row['command']})
   for path in (trace,target/'game.replay'):
    with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
    path.unlink()
   (out/'manifest.json').write_text(json.dumps({'binarySha256':digest(binary),'checks':rows},indent=2))
   print('PASS',name,delay,workers,value,flush=True)
