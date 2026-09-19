from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess, os, json, gzip, shutil
base=Path('/tmp/bastion-swim-games')
binary=base/'glob2'
jobs=[(seed,r,'nicowar') for seed in (211,307) for r in range(4)] + [(seed,0,'maxima') for seed in (211,307)]
def run(job):
 seed,r,ai=job; name=f'{seed}-r{r}-{ai}';out=base/name
 with (base/f'{name}.log').open('w') as log:
  rc=subprocess.run([str(binary),'--run-game','--map-file',str(base/f'maps/{seed}/map-r{r}.map'),'--game-seed','2',*sum((['--player',ai] for _ in range(4)),[]),'--ticks','60000','--telemetry','team-timeline','--save','final','--output-dir',str(out)],stdout=log,stderr=subprocess.STDOUT,env=os.environ|{'SDL_VIDEODRIVER':'dummy'}).returncode
 # Only compress closed outputs after the process completes.
 for p in [base/f'{name}.log',out/'final.game']:
  if p.exists():
   with p.open('rb') as src,gzip.open(str(p)+'.gz','wb',compresslevel=3) as dst:shutil.copyfileobj(src,dst)
   p.unlink()
 return name,rc
with ThreadPoolExecutor(6) as pool:
 for result in pool.map(run,jobs):print(result,flush=True)
