import hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/team-quota-pr/baseline-global4-games';OUT.mkdir(parents=True,exist_ok=True)
BINARY=Path('/Users/bradley/.codex/worktrees/gradient-staleness/glob2/build/darwin/client/release/client-full-direct-1111/src/glob2')
SEED=73
commands=[]
def run(name,players,ticks,threads,mapfile=None,load=None,save=False):
 path=OUT/name;path.mkdir(exist_ok=True)
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
 env['GLOB2_BUILDING_ADMISSION']='capped'
 env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED=str(SEED))
 cmd=[str(BINARY),'game','run','--ticks',str(ticks),'--compute-threads',str(threads),'--compute-telemetry','waits','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--telemetry','checksums','--output-dir',str(path/'game')]
 if load:cmd+=['--load-game',str(load)]
 else:
  cmd+=['--map-file',str(ROOT/'maps'/mapfile),'--game-seed',str(SEED)]
  for player in players:cmd+=['--player',player]
 if save:cmd+=['--save','final']
 record={'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':hashlib.sha256(BINARY.read_bytes()).hexdigest()};commands.append(record)
 with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:r=subprocess.run(cmd,env=env,stdout=out,stderr=err)
 record['exit_code']=r.returncode;(path/'command.json').write_text(json.dumps(record,indent=2));assert r.returncode==0,name
 result=json.load(open(path/'game/result.json'));print(name,result['initialChecksum'],result['finalChecksum'],flush=True)
 return path,result
players=['nicowar','econo','castor','warrush']
a,ra=run('full',players,4096,1,mapfile='Holiday_Island_2.map.gz')
p,rp=run('prefix',players,2048,1,mapfile='Holiday_Island_2.map.gz',save=True)
b,rb=run('resume',players,4096,4,load=next((p/'game').glob('final.game*')))
print('base continuation matches:',ra['finalChecksum']==rb['finalChecksum'],flush=True)
(OUT/'commands.json').write_text(json.dumps(commands,indent=2))
