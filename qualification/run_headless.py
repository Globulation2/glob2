from pathlib import Path
import hashlib,json,os,platform,subprocess,sys,time
root=Path.cwd();name=sys.argv[1];build={'macos':'darwin','linux':'linux'}[name];binary=root/f'build/{build}/client/debug/dev-dev_fast-true-linker-auto/src/glob2';out=root/'artifacts/hiring-gradient-production'/('headless-'+name);out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
identity={'revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'binary_sha256':sha(binary),'platform':platform.platform(),'architecture':platform.machine(),'build':'dev_fast=1, -O0 -g1, linker=auto; correctness only, no performance claim'}
(out/'identity.json').write_text(json.dumps(identity,indent=2))
for workload,mapfile,players in [('holiday','Holiday_Island_2.map.gz',['nicowar','econo','castor','warrush']),('large-eleven','Oazis.map.gz',(['nicowar','econo','castor','warrush']*3)[:11])]:
 results={}
 for participants in [1,4]:
  path=out/f'{workload}-p{participants}'
  old=None
  try:old=json.loads((path/'run.json').read_text())
  except (FileNotFoundError,json.JSONDecodeError):pass
  if old and old.get('exit_code')==0 and (path/'game/result.json').exists():
   results[participants]=json.loads((path/'game/result.json').read_text());print('REUSE',name,workload,participants,flush=True);continue
  if path.exists():
   excluded=out.parent/'headless-excluded-disk';excluded.mkdir(exist_ok=True);path.rename(excluded/(name+'-'+path.name+'-'+str(time.time_ns())))
  path.mkdir(exist_ok=True)
  env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DATA_DIR=str(path/'profile'),GLOB2_TEST_SEED='42')
  if name=='linux':env['LD_LIBRARY_PATH']='/opt/glob2-sdl/lib'
  cmd=[str(binary),'game','run','--map-file',str(root/'maps'/mapfile),'--game-seed','42','--ticks','2048','--compute-threads',str(participants),'--gpu','off','--gradient-backend','cpu','--telemetry','checksums','--write-replay','--save','final','--output-dir',str(path/'game')]
  for player in players:cmd+=['--player',player]
  record={'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('GLOB2_','SDL_','LD_'))},'map_sha256':sha(root/'maps'/mapfile)}
  with (path/'stdout.log').open('w') as stdout,(path/'stderr.log').open('w') as stderr:r=subprocess.run(cmd,env=env,stdout=stdout,stderr=stderr)
  record['exit_code']=r.returncode;(path/'run.json').write_text(json.dumps(record,indent=2));assert r.returncode==0,(name,path,(path/'stderr.log').read_text()[-1800:])
  v=json.loads((path/'game/result.json').read_text());results[participants]=v;print(name,workload,participants,v['initialChecksum'],v['finalChecksum'],flush=True)
 assert results[1]['initialChecksum']==results[4]['initialChecksum'];assert results[1]['finalChecksum']==results[4]['finalChecksum']
 for file in ['world.checksums','game.replay.checksums']:
  assert (out/f'{workload}-p1/game'/file).read_bytes()==(out/f'{workload}-p4/game'/file).read_bytes(),(workload,file)
 print('MATCHED PARTICIPANTS',name,workload,flush=True)
