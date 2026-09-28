from pathlib import Path
import subprocess,json,random,concurrent.futures
D=Path('/home/bradley/glob2-maxima-final/artifacts/final-maxima/benchmark');D.mkdir(exist_ok=True)
A=Path('/home/bradley/glob2-farm-cost-attribution')
scenarios=[('crowded',Path('/home/bradley/glob2-maxima-final/inputs/crowded.game'),575328),('g15',A/'games/g15-s6101-r0/pre-farming/final.game',32768),('g52',A/'games/g52-s6101-r0/pre-farming/final.game',32768)]
binaries={v:Path('/home/bradley')/p/'build/linux/client/release/src/glob2' for v,p in [('master','glob2-maxima-master-check'),('final','glob2-maxima-final')]}
def run(core):
 jobs=[(s,v) for s in scenarios for v in binaries];random.Random(920+core).shuffle(jobs);rows=[]
 for (name,path,start),v in jobs:
  out=D/f'core{core}-{name}-{v}';out.mkdir(exist_ok=True)
  cmd=['taskset','-c',str(core),str(binaries[v]),'--run-game','--load-game',str(path),'--ticks',str(start+8192),'--output-dir',str(out),'--replay','false']
  (out/'command.json').write_text(json.dumps(cmd,indent=2))
  with (out/'run.log').open('w') as f:subprocess.run(['/usr/bin/time','-f','%U %S %e %M','-o',str(out/'cpu.txt'),*cmd],cwd='/home/bradley/glob2-maxima-final',stdout=f,stderr=f,check=True)
  nums=list(map(float,(out/'cpu.txt').read_text().split()));row={'core':core,'scenario':name,'variant':v,'cpu_seconds':sum(nums[:2])};rows.append(row);print(row,flush=True)
 return rows
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:rows=sum(ex.map(run,[0,2]),[])
(D/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
