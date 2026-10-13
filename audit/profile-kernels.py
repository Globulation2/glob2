import os,json,subprocess
from pathlib import Path
root=Path.cwd();out=root/'artifacts/gradient-cpu-audit';rows=[]
env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','SDL_'))};env.update(GLOB2_GRADIENT_BACKEND='cpu',GLOB2_GRADIENT_VERIFY='off',SDL_AUDIODRIVER='dummy')
for v in ('before','runtime','resolved','static-sse','hoisted'):
 cmd=['valgrind','--tool=callgrind','--collect-atstart=no','--toggle-collect=AuditSolve*','--dump-instr=yes','--collect-jumps=yes','--cache-sim=yes','--branch-sim=yes',f'--callgrind-out-file={out/v}.callgrind',str(out/(v+'-kernel')),'512','2']
 with (out/(v+'-kernel-profile.log')).open('w') as log:
  result=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 assert result.returncode==0,(v,result.returncode)
 lines=(out/(v+'.callgrind')).read_text().splitlines();events=next(x for x in lines if x.startswith('events:')).split()[1:];totals=list(map(int,next(x for x in lines if x.startswith('summary:')).split()[1:]));
 rows.append({'variant':v,'command':cmd,'events':dict(zip(events,totals))})
 (out/'kernel-instructions.json').write_text(json.dumps(rows,indent=2)+'\n');print(v,dict(zip(events,totals)),flush=True)
