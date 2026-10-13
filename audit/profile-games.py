from pathlib import Path
import json,os,subprocess
root=Path.cwd();out=root/'artifacts/gradient-cpu-audit';env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','SDL_'))};env.update(GLOB2_GRADIENT_BACKEND='cpu',GLOB2_GRADIENT_ALGORITHM='buckets',GLOB2_GRADIENT_VERIFY='off',SDL_AUDIODRIVER='dummy')
rows=[]
for v,binary in [('before',out/'before-symbols'),('fixed',root/'artifacts/gradient-cpu-profile/after-symbols'),('candidate',out/'candidate-symbols')]:
 directory=out/(v+'-game-profile');directory.mkdir(exist_ok=True)
 cmd=['valgrind','--tool=callgrind','--collect-atstart=no','--toggle-collect=Game::syncStep*','--dump-instr=yes','--collect-jumps=yes','--cache-sim=yes','--branch-sim=yes',f'--callgrind-out-file={out/v}-game.callgrind',str(binary),'game','run','--load-game',str(root/'artifacts/gradient-cpu-ab/fixtures/512/initial.game.gz'),'--ticks','128','--compute-threads','1','--gpu','off','--data-dir',str(root/'data'),'--output-dir',str(directory),'--profile','cpu-audit']
 with (out/(v+'-game-profile.log')).open('w') as log:r=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 assert r.returncode==0,(v,r.returncode)
 d=json.loads((directory/'result.json').read_text());assert d['finalChecksum']==82182947 and not d.get('gpu_requested')
 lines=(out/(v+'-game.callgrind')).read_text().splitlines();events=next(x for x in lines if x.startswith('events:')).split()[1:];totals=list(map(int,next(x for x in lines if x.startswith('summary:')).split()[1:]));rows.append({'variant':v,'binary':str(binary),'command':cmd,'checksum':d['finalChecksum'],'events':dict(zip(events,totals))})
 (out/'game-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(v,dict(zip(events,totals)),flush=True)
 subprocess.run(['callgrind_annotate','--threshold=99',str(out/(v+'-game.callgrind'))],stdout=(out/(v+'-game-annotated.txt')).open('w'),check=True)
