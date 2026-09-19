import argparse, concurrent.futures, json, os, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('--binary',default='build/src/glob2');p.add_argument('--out',required=True);p.add_argument('--reference',action='store_true');a=p.parse_args()
root=pathlib.Path.cwd()
base=(root/a.out).resolve(); base.mkdir(parents=True,exist_ok=True)
binary=str((root/a.binary).resolve())
env=dict(os.environ, SDL_VIDEODRIVER='dummy', GLOB2_USER_DIR=str(base/'profile'))
def run(args, folder):
 folder.mkdir(parents=True,exist_ok=True)
 with (folder/'command.json').open('w') as f:json.dump(args,f)
 with (folder/'run.log').open('w') as f:return subprocess.run([binary,*args,'--output-dir',str(folder)],stdout=f,stderr=subprocess.STDOUT,env=env).returncode
def generation(method,seed):
 folder=base/f'map-{method}-{seed}'
 code=run(['--generate-map','--generator',str(method),'--map-seed',str(seed),'--param','width=8','--param','height=8','--param','teams=4','--write-map','true','--rotations','4'],folder)
 return method,seed,folder,code
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
 maps=list(ex.map(lambda x:generation(*x),([(38,101)] if a.reference else [(64,101),(64,202),(64,303),(64,404)])))
jobs=[]
for method,seed,folder,code in maps:
 if code:print('GEN FAILED',method,seed,flush=True);continue
 for rotation in range(4):
  for ai in ['mixed']:
   target=base/f'game-{method}-{seed}-{ai}-r{rotation}'
   args=['--run-game','--map-file',str(folder/f'map-r{rotation}.map'),'--game-seed','19','--ticks','45000','--telemetry','team-timeline','--save','final']
   for player in ['nicowar','cortex','cabino','maxima']:args+=['--player',player]
   jobs.append((args,target))
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
 for job,code in zip(jobs,ex.map(lambda job:run(*job),jobs)):
  print(job[1].name,code,flush=True)
