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
 if save:cmd+=['--save','final']
 record={'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_'))},'binary_sha256':hashlib.sha256(BINARY.read_bytes()).hexdigest()};commands.append(record)
 with (path/'stdout.log').open('w') as out,(path/'stderr.log').open('w') as err:r=subprocess.run(cmd,env=env,stdout=out,stderr=err)
 record['exit_code']=r.returncode;(path/'command.json').write_text(json.dumps(record,indent=2));assert r.returncode==0,name
 result=json.load(open(path/'game/result.json'));print(name,result['initialChecksum'],result['finalChecksum'],flush=True)
 return path,result
summary={}
for label,mapfile,players,ticks in [('holiday','Holiday_Island_2.map.gz',['nicowar','econo','castor','warrush'],4096),('oazis','Oazis.map.gz',(['nicowar','econo','castor','warrush']*3)[:11],2048)]:
 a,ra=run(label+'-p1',players,ticks,1,mapfile=mapfile);b,rb=run(label+'-p4',players,ticks,4,mapfile=mapfile)
 assert ra['initialChecksum']==rb['initialChecksum'] and ra['finalChecksum']==rb['finalChecksum'],label
 for file in ['world.checksums','game.replay.checksums']:assert (a/'game'/file).read_bytes()==(b/'game'/file).read_bytes(),(label,file)
 summary[label]={'ticks':ticks,'world_trace_lines':len((a/'game/world.checksums').read_text().splitlines()),'world_sha256':hashlib.sha256((a/'game/world.checksums').read_bytes()).hexdigest(),'entity_sha256':hashlib.sha256((a/'game/game.replay.checksums').read_bytes()).hexdigest(),'participants':[1,4],'equal':True}
 if label=='holiday':
  prefix,rp=run('holiday-prefix',players,2048,1,mapfile=mapfile,save=True)
  save=next((prefix/'game').glob('final.game*'))
  resume,rr=run('holiday-resume',players,4096,4,load=save)
  def trace(path):return dict(line.split() for line in (path/'game/world.checksums').read_text().splitlines() if line.strip())
  whole=trace(a);part=trace(prefix);suffix=trace(resume)
  for partname,parttrace in [('prefix',part),('resume',suffix)]:
   assert parttrace and all(whole.get(tick)==value for tick,value in parttrace.items()),partname
  assert len(set(part)|set(suffix))==len(whole)
  assert rr['finalChecksum']==ra['finalChecksum']
  summary['continuation']={'save_tick':2048,'end_tick':4096,'resumed_participants':4,'uninterrupted_participants':1,'world_checksums_equal':True,'final_checksum':rr['finalChecksum'],'save':str(save)}
(OUT/'summary.json').write_text(json.dumps(summary,indent=2));(OUT/'commands.json').write_text(json.dumps(commands,indent=2));print(json.dumps(summary,indent=2))
