import sys,json,gzip,hashlib,shutil
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
binary=root/'build/darwin/client/release/src/glob2';out=root/'artifacts/building-gradient-857/mac-worker-traces';out.mkdir(exist_ok=True)
fixtures={'large':root/'games/gd-large-4ai.game','arena':root/'games/gd-bigarena-long.game','mixed':root/'test/fixtures/ai-random-streams/numbi-castor-v121.game.gz'}
linux=json.loads((root/'artifacts/building-gradient-857/linux-worker-manifest.json').read_text());rows=[]
def archive(path):
 if path.suffix=='.gz':return
 with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
 path.unlink()
for name,fixture in fixtures.items():
 checkpoint=root/'artifacts/building-gradient-857/cross-platform-inputs'/f'{name}.game.gz'
 for policy in ('eager','staffing','partial','combined'):
  expected=next(r['traceSha256'] for r in linux['checks'] if r['workload']==name and r['policy']==policy and r['workers']==4)
  for workers in (4,):
   target=out/f'{name}-{policy}-w{workers}'
   args=['--load-game',str(checkpoint),'--ticks','768','--compute-experiments','none','--gradient-workers',str(workers),'--fork-rule','building-gradient-pipeline=1','--fork-rule','buildingGradientDelay=4','--fork-rule',f'building-gradient-hybrid={int(policy in ("staffing","combined"))}','--fork-rule',f'building-gradient-partial={int(policy in ("partial","combined"))}','--telemetry','checksums','--replay','true','--save','final']
   row=execute(binary,args,target);trace=target/'game.replay.checksums';value=digest(trace)
   if expected is None:expected=value
   if value!=expected:raise RuntimeError(f'worker determinism mismatch {target}')
   rows.append({'workload':name,'policy':policy,'workers':workers,'traceSha256':value,'command':row['command']});(out/'manifest.json').write_text(json.dumps({'binarySha256':digest(binary),'checks':rows},indent=2))
   for path in (trace,target/'game.replay',target/'final.game'):
    if path.exists():archive(path)
   print('PASS',name,policy,workers,value,flush=True)
 archive(checkpoint)
