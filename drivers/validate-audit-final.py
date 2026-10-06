import sys,json,gzip,shutil
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
binary=root/'build/darwin/client/release/src/glob2';out=root/'artifacts/building-gradient-rebase-851/audit-final';out.mkdir(exist_ok=True);rows=[]
for name in ('large','arena','mixed'):
 checkpoint=root/'artifacts/building-gradient-rebase-851/cross-platform-inputs-final'/f'{name}.game.gz'
 for policy in ('eager','staffing','partial','combined'):
  expected=None
  for audit in (False,True):
   target=out/f'{name}-{policy}-audit{int(audit)}'
   args=['--load-game',str(checkpoint),'--ticks','544','--compute-experiments','none','--gradient-workers','4','--fork-rule','building-gradient-pipeline=1','--fork-rule','buildingGradientDelay=4','--fork-rule',f'building-gradient-hybrid={int(policy in ("staffing","combined"))}','--fork-rule',f'building-gradient-partial={int(policy in ("partial","combined"))}','--telemetry','checksums','--replay','true']
   if audit:args += ['--telemetry','building-gradient-impact']
   row=execute(binary,args,target);trace=target/'game.replay.checksums';value=digest(trace)
   if expected is None:expected=value
   if value!=expected:raise RuntimeError(f'audit changes simulation {target}')
   rows.append({'workload':name,'policy':policy,'audited':audit,'traceSha256':value,'command':row['command']})
   for path in (trace,target/'game.replay'):
    if path.exists():
     with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
     path.unlink()
   print('PASS',name,policy,'audit',audit,flush=True)
   (out/'manifest.json').write_text(json.dumps({'binarySha256':digest(binary),'checks':rows},indent=2))
