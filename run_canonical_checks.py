import hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path.cwd();OUT=ROOT/'artifacts/pr1220/games';OUT.mkdir(parents=True,exist_ok=True)
BINARY=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
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
# Canonicalize the historical format-70 map header before comparing saves.
start,_=run('canonical-initial',players,1,1,mapfile='Holiday_Island_2.map.gz',save='initial')
initial=next((start/'game').glob('initial.game*'))
a,ra=run('canonical-whole',players,4096,1,load=initial)
prefix,rp=run('canonical-prefix',players,2048,1,load=initial,save=True)
resume,rr=run('canonical-resume',players,4096,4,load=next((prefix/'game').glob('final.game*')))
def trace(p):return dict(line.split() for line in (p/'game/world.checksums').read_text().splitlines() if line.strip())
whole=trace(a);part=trace(prefix);suffix=trace(resume)
assert all(whole.get(k)==v for k,v in part.items())
assert all(whole.get(k)==v for k,v in suffix.items())
assert len(set(part)|set(suffix))==len(whole)
assert rr['finalChecksum']==ra['finalChecksum']
print('Canonical raw-world continuation passes for every tick',flush=True)
a,ra=run('oazis-p1',(players*3)[:11],2048,1,mapfile='Oazis.map.gz')
b,rb=run('oazis-p4',(players*3)[:11],2048,4,mapfile='Oazis.map.gz')
for file in ['world.checksums','game.replay.checksums']:assert (a/'game'/file).read_bytes()==(b/'game'/file).read_bytes(),file
assert ra['finalChecksum']==rb['finalChecksum']
print('Eleven-team traces match across one and four participants',flush=True)
(OUT/'canonical-commands.json').write_text(json.dumps(commands,indent=2))
