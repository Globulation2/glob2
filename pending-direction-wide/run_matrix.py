from pathlib import Path
import os,json,subprocess,time,gzip,hashlib,traceback
from concurrent.futures import ThreadPoolExecutor
ROOT=Path.cwd();BASE=ROOT/'artifacts/pending-direction-wide';PROBE=ROOT/'artifacts/pending-direction/glob2-probe';CONTROL=ROOT/'artifacts/proactive-attribution/binaries/profile'
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--source',choices=['generator','bundled']);parser.add_argument('--case',action='append');args=parser.parse_args()
workloads=[w for w in json.load(open(BASE/'workloads.json')) if (not args.source or w['source']==args.source) and (not args.case or w['id'] in args.case)]
def invoke(cmd,path,env):
 path.mkdir(parents=True,exist_ok=True)
 (path/'command.json').write_text(json.dumps(cmd,indent=2));start=time.monotonic()
 with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=out,stderr=err)
 meta={'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'exit_code':p.returncode,'elapsed_seconds':time.monotonic()-start};(path/'execution.json').write_text(json.dumps(meta,indent=2))
 if p.returncode:raise RuntimeError(str(path)+': '+(path/'stderr.log').read_text()[-1000:]+' '+(path/'stdout.log').read_text()[-1000:])
 return meta
def run_one(w):
 path=BASE/'runs'/w['id'];path.mkdir(parents=True,exist_ok=True)
 if (path/'run.json').exists() and json.load(open(path/'run.json')).get('complete'):return
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_GRADIENT_ATTRIBUTION='baseline')
 try:
  if w['source']=='generator':
   gen=path/'generation'
   if not (gen/'map-r0.map.gz').exists():
    cmd=[str(PROBE),'map','study',w['generator'],'--seed',str(w['map_seed']),'--candidates','0','--write-map','--output-dir',str(gen)]
    for k,v in w['controls'].items():cmd+=['--set',f'{k}={v}']
    invoke(cmd,path/'generation-command',env)
   mapfile=gen/'map-r0.map.gz'
  else:mapfile=Path(w['map_file'])
  results={}
  for policy,bin in [('probe',PROBE),('control',CONTROL)]:
   job=path/policy
   if not (job/'game/result.json').exists():
    cmd=[str(bin),'game','run','--map-file',str(mapfile),'--game-seed',str(w['game_seed']),'--ticks',str(w['ticks']),'--compute-threads','4','--gpu','off','--gradient-backend','cpu','--output-dir',str(job/'game')]
    for ai in w['players']:cmd+=['--player',ai]
    for alliance in w['alliances']:cmd+=['--alliance',str(alliance)]
    if policy=='probe':env['GLOB2_PENDING_DIRECTIONS']=str(path/'directions.csv')
    else:env.pop('GLOB2_PENDING_DIRECTIONS',None)
    invoke(cmd,job,env)
   results[policy]=json.load(open(job/'game/result.json'))
  keys=['ticks','initialChecksum','finalChecksum'];match=all(results['probe'][k]==results['control'][k] for k in keys)
  if not match:raise RuntimeError('Probe/control simulation differs '+w['id'])
  rows=0
  if (path/'directions.csv').exists():
   with (path/'directions.csv').open('rb') as src,gzip.open(path/'directions.csv.gz','wb') as dst:
    for line in src:dst.write(line);rows+=1
   (path/'directions.csv').unlink()
  meta=dict(workload=w,complete=True,probe_control_match=match,samples=max(0,rows-1)//2,map_sha256=hashlib.sha256(mapfile.read_bytes()).hexdigest(),**{k:results['probe'][k] for k in keys})
  (path/'run.json').write_text(json.dumps(meta,indent=2));print(json.dumps({'id':w['id'],'ticks':meta['ticks'],'samples':meta['samples'],'match':match}),flush=True)
 except Exception as e:
  (path/'failure.json').write_text(json.dumps({'error':str(e),'traceback':traceback.format_exc()},indent=2));print('FAILED '+w['id']+' '+str(e),flush=True)
if __name__=='__main__':
 with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(run_one,workloads))
 print('FINISHED',flush=True)
