import hashlib,json,subprocess,sys
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
checkpoint=Path(sys.argv[2]).resolve()
common=['--load-game',str(checkpoint),'--ticks','16128','--compute-experiments','none','--compute-threads','1','--fork-rule','building-gradient-pipeline=1','--fork-rule','building-gradient-partial=1','--telemetry','checksums','--replay','true','--save','final']
rows=[]
for delay in [2,4,8]:
 for cost in [0,300]:
  checks=[]
  for workers in [0,1,2,4,8]:
   dest=out/f'delay-{delay}-cost-{cost}-workers-{workers}'
   row=execute(binary,[*common,'--fork-rule',f'buildingGradientDelay={delay}','--gradient-workers',str(workers),'--building-gradient-budget-model',str(root/f'artifacts/gradient-budget-sweep/models/fixed-{cost}.json')],dest)
   paths=list(dest.glob('*.checksums'));assert len(paths)==1,paths
   h=digest(paths[0]);checks.append(h)
   rows.append(dict(delay=delay,cost=cost,workers=workers,checksum_sha256=h,run=row))
   (out/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
   print(cost,workers,row['result']['finalChecksum'],h,flush=True)
  assert len(set(checks))==1,(cost,checks)
print('Per-tick checksums agree across 0/1/2/4/8 workers for both budgets.',flush=True)
