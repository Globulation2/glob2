import os,sys,json,subprocess,time,hashlib,argparse,gzip,shutil
from pathlib import Path
ROOT=Path.cwd();BASE=ROOT/'artifacts/pending-direction';BIN=BASE/'glob2-probe'
p=argparse.ArgumentParser();p.add_argument('--pilot',action='store_true');a=p.parse_args()
workloads=[('holiday','Holiday_Island_2.map.gz',['nicowar','econo','castor','warrush']),('large-eleven','Oazis.map.gz',(['nicowar','econo','castor','warrush']*3)[:11])]
def run_one(item):
  seed,(name,mapname,players)=item
  path=BASE/('pilot' if a.pilot else 'runs')/name/f'seed-{seed}';path.mkdir(parents=True,exist_ok=True)
  if (path/'run.json').exists() and json.loads((path/'run.json').read_text()).get('exit_code')==0:return
  env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_GRADIENT_ATTRIBUTION='baseline',GLOB2_PENDING_DIRECTIONS=str(path/'directions.csv'))
  ticks=2048 if a.pilot else 32768
  cmd=[str(BIN),'game','run','--map-file',str(ROOT/'maps'/mapname),'--game-seed',str(seed),'--ticks',str(ticks),'--compute-threads','4','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--output-dir',str(path/'game')]
  for ai in players:cmd+=['--player',ai]
  if a.pilot:cmd+=['--telemetry','checksums']
  meta={'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':hashlib.sha256(BIN.read_bytes()).hexdigest()};start=time.monotonic()
  with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:proc=subprocess.run(cmd,cwd=ROOT,env=env,stdout=out,stderr=err)
  meta.update(exit_code=proc.returncode,elapsed_seconds=time.monotonic()-start)
  if proc.returncode:print((path/'stderr.log').read_text()[-2000:]);raise SystemExit(proc.returncode)
  result=json.loads((path/'game/result.json').read_text());meta['ticks']=result['ticks'];meta['initial_checksum']=result['initialChecksum'];meta['final_checksum']=result['finalChecksum']
  if not a.pilot:
   old=json.loads((ROOT/'artifacts/proactive-attribution/runs'/name/f'seed-{seed}-baseline-p4/game/result.json').read_text())
   meta['matches_uninstrumented_simulation']=(result['ticks']==old['ticks'] and result['initialChecksum']==old['initialChecksum'] and result['finalChecksum']==old['finalChecksum'])
   if not meta['matches_uninstrumented_simulation']:raise RuntimeError('Simulation differs '+str(path))
  rows=0
  if (path/'directions.csv').exists():
   with (path/'directions.csv').open('rb') as src,gzip.open(path/'directions.csv.gz','wb') as dst:
    for line in src:dst.write(line);rows+=1
   (path/'directions.csv').unlink()
  meta['samples']=max(0,rows-1)//2;(path/'run.json').write_text(json.dumps(meta,indent=2));print(name,seed,meta,flush=True)

if __name__=='__main__':
 from concurrent.futures import ThreadPoolExecutor
 items=[(seed,w) for seed in ([42] if a.pilot else range(42,74)) for w in workloads]
 with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(run_one,items))
