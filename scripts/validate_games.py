from pathlib import Path
import subprocess,json,hashlib,os
W=Path('/dev/shm/glob2-adaptive-official');A=Path('/home/bradley/glob2/artifacts/gradient-official-validation');binary=A/'glob2'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def call(cmd,log):
 with log.open('w') as f:subprocess.run(list(map(str,cmd)),cwd=W,stdout=f,stderr=subprocess.STDOUT,check=True)
reports=[]
for generator,size,players,seed in [('isles',64,2,7801),('river',128,4,7802)]:
 m=A/f'{generator}.map.gz';call([binary,'map','generate',generator,'--seed',seed,'--width',size,'--height',size,'--teams',players,'--output',m],A/f'{generator}-generate.log')
 rows=[]
 for name,threads,probe in [('serial',1,False),('observed',1,True),('parallel',4,False)]:
  out=A/f'{generator}-{name}';cmd=[binary,'game','run','--map-file',m,'--game-seed',seed,'--ticks',8192,'--compute-threads',threads,'--gpu','off','--telemetry','checksums','--write-replay','--output-dir',out]
  for p in range(players):cmd+=['--player','nicowar','--alliance',p+1]
  if probe:cmd+=['--resource-gradient-probe']
  (A/f'{generator}-{name}-command.json').write_text(json.dumps(list(map(str,cmd)),indent=2));call(cmd,A/f'{generator}-{name}.log');r=json.loads((out/'result.json').read_text());rows.append(dict(name=name,ticks=r['ticks'],final=r['finalChecksum'],initial=r['initialChecksum'],gradient_jobs=r['gradient_jobs'],binary=sha(binary)))
  print(generator,name,r['ticks'],r['finalChecksum'],flush=True)
 for filename in ['world.checksums','snapshot.checksums','game.replay.checksums']:
  paths=[A/f'{generator}-{name}'/filename for name in ['serial','observed','parallel']]
  if not all(p.exists() for p in paths):raise RuntimeError('Missing checksum evidence '+str(paths))
  if len({sha(p) for p in paths})!=1:raise RuntimeError('Checksum divergence '+str(paths))
  reports.append(dict(generator=generator,artifact=filename,sha256=sha(paths[0]),runs=rows))
 (A/'equivalence.json').write_text(json.dumps(reports,indent=2))
print('All observer/worker checksum comparisons matched',flush=True)
