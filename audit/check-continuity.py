from pathlib import Path
import os,subprocess,json
root=Path.cwd();out=root/'artifacts/gradient-cpu-audit';env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','SDL_'))};env.update(GLOB2_GRADIENT_BACKEND='cpu',GLOB2_GRADIENT_ALGORITHM='buckets',GLOB2_GRADIENT_VERIFY='off',SDL_AUDIODRIVER='dummy')
bins={'master':root/'artifacts/gradient-cpu-profile/master-baseline','fixed':out/'candidate-release'};rows=[];reference=None
save=out/'closeout/final/validate-128-established-8-before/final.game.gz'
for count in (1,2,4,8):
 for v,binary in bins.items():
  directory=out/f'continuation-{count}-{v}';directory.mkdir(exist_ok=True)
  cmd=[str(binary),'game','run','--load-game',str(save),'--ticks','6656','--compute-threads',str(count),'--gpu','off','--telemetry','checksums','--save','final','--write-replay','--data-dir',str(root/'data'),'--output-dir',str(directory),'--profile','audit-continuation']
  with (out/f'continuation-{count}-{v}.log').open('w') as log:r=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  assert r.returncode==0,(v,count,r.returncode)
  trace=(directory/'world.checksums').read_bytes()
  if reference is None:reference=trace
  assert trace==reference,(v,count)
  data=json.loads((directory/'result.json').read_text());rows.append(dict(type='save',variant=v,count=count,command=cmd,checksum=data['finalChecksum'],trace=str(directory/'world.checksums')))
 for v in ('fixed',):
  directory=out/f'golden-final-{count}';directory.mkdir(exist_ok=True)
  cmd=[str(bins[v]),'match','verify',str(root/'test/fixtures/multiplayer/FourSquares1.g2mr'),'--map-file',str(root/'maps/FourSquares1.map.gz'),'--compute-threads',str(count),'--output-dir',str(directory)]
  with (out/f'golden-final-{count}.log').open('w') as log:r=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  assert r.returncode==0,(v,count,r.returncode)
  verdict=json.loads((directory/'verdict.json').read_text());assert verdict['verdict']=='verified'
  rows.append(dict(type='canonical',variant=v,count=count,command=cmd,verdict=verdict))
(out/'continuity-final.json').write_text(json.dumps(rows,indent=2)+'\n');print('COMPLETE',len(rows))
