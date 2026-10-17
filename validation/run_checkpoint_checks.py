import hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/team-quota-pr/games';OUT.mkdir(parents=True,exist_ok=True)
BINARY=ROOT/'build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
SEED=73
commands=[]
def run(name,players,ticks,threads,mapfile=None,load=None,save=False):
 path=OUT/name;path.mkdir(exist_ok=True)
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
 env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED=str(SEED))
 cmd=[str(BINARY),'game','run','--ticks',str(ticks),'--compute-threads',str(threads),'--compute-telemetry','waits','--gpu','off','--gradient-backend','cpu','--benchmark-warmup','0','--telemetry','checksums','--output-dir',str(path/'game')]
 if load:cmd+=['--load-game',str(load)]
 else:
  cmd+=['--map-file',str(ROOT/'maps'/mapfile),'--game-seed',str(SEED)]
  for player in players:cmd+=['--player',player]
 if save:cmd+=['--save',save if isinstance(save,str) else 'final']
 record={'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':hashlib.sha256(BINARY.read_bytes()).hexdigest()};commands.append(record)
 with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:r=subprocess.run(cmd,env=env,stdout=out,stderr=err)
 record['exit_code']=r.returncode;(path/'command.json').write_text(json.dumps(record,indent=2));assert r.returncode==0,name
 result=json.load(open(path/'game/result.json'));print(name,result['initialChecksum'],result['finalChecksum'],flush=True)
 return path,result
players=['nicowar','econo','castor','warrush']
a,ra=run('holiday-checkpoint-full',players,4096,1,mapfile='Holiday_Island_2.map.gz',save='every:2048')
b,rb=run('holiday-checkpoint-resume',players,4096,4,load=next((a/'game').glob('checkpoint-2048.game*')))
whole=dict(line.split() for line in (a/'game/world.checksums').read_text().splitlines() if line.strip())
suffix=dict(line.split() for line in (b/'game/world.checksums').read_text().splitlines() if line.strip())
print('checkpoint resume:',all(whole.get(k)==v for k,v in suffix.items()),ra['finalChecksum']==rb['finalChecksum'],flush=True)
a,ra=run('oazis-p1',(players*3)[:11],2048,1,mapfile='Oazis.map.gz')
b,rb=run('oazis-p4',(players*3)[:11],2048,4,mapfile='Oazis.map.gz')
for file in ['world.checksums','game.replay.checksums']:assert (a/'game'/file).read_bytes()==(b/'game'/file).read_bytes(),file
assert ra['finalChecksum']==rb['finalChecksum']
print('Oazis participant traces equal',flush=True)
(OUT/'checkpoint-commands.json').write_text(json.dumps(commands,indent=2))
