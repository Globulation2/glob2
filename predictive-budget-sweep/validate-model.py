import hashlib,json,subprocess,sys
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
binary=root/'build/darwin/client/release/src/glob2';out=root/'artifacts/gradient-budget-sweep/validation';out.mkdir(exist_ok=True)
checkpoint=root/'artifacts/gradient-budget-sweep/mac-prepass-excluded/large-economy-710000/warmup/final.game.gz'
common=['--load-game',str(checkpoint),'--ticks','16512','--compute-experiments','none','--compute-threads','1','--fork-rule','building-gradient-pipeline=1','--fork-rule','building-gradient-partial=1','--telemetry','checksums','--replay','true','--save','final']
rows=[]
for cost in [0,300]:
 checks=[]
 for workers in [0,1,2,4,8]:
  dest=out/f'cost-{cost}-workers-{workers}'
  row=execute(binary,[*common,'--gradient-workers',str(workers),'--building-gradient-budget-model',str(root/f'artifacts/gradient-budget-sweep/models/fixed-{cost}.json')],dest)
  paths=list(dest.glob('*.checksums'));assert len(paths)==1,paths
  h=digest(paths[0]);checks.append(h)
  rows.append(dict(cost=cost,workers=workers,checksum_sha256=h,run=row))
  (out/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
  print(cost,workers,row['result']['finalChecksum'],h,flush=True)
 assert len(set(checks))==1,(cost,checks)
print('Per-tick checksums agree across 0/1/2/4/8 workers for both budgets.',flush=True)
