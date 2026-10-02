import os,json,subprocess,re,hashlib,shutil
from pathlib import Path
root=Path.cwd();out=root/'artifacts/memory-optimization/peak-followup';fixtures=root/'artifacts/memory-profile/game'
binaries={'baseline':out/'baseline/glob2','candidate':root/'build/darwin/client/release/src/glob2'}
records=[]
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env.pop('MallocStackLogging',None)
for tick,name in [(0,'initial.game.gz'),(15000,'checkpoint-15000.game.gz'),(45000,'checkpoint-45000.game.gz')]:
 for variant,binary in binaries.items():
  for save in [False,True]:
   directory=out/f'peak-{tick}-{variant}-{"save" if save else "load"}'
   if directory.exists(): shutil.rmtree(directory)
   directory.mkdir()
   cmd=['/usr/bin/time','-l',str(binary),'--run-game','--load-game',str(fixtures/name),'--ticks',str(tick+1),'--benchmark-warmup','0','--output-dir',str(directory)]
   if save:cmd+=['--save','final']
   with (directory/'run.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,check=True)
   log=(directory/'run.log').read_text();rss=int(re.search(r'(\d+)\s+maximum resident set size',log)[1]);result=json.loads((directory/'result.json').read_text())
   record={'tick':tick,'variant':variant,'save':save,'maximum_resident_bytes':rss,'checksum':re.findall(r'nox::gui.game.checkSum\(\) = ([0-9a-f]+)',log)[-1],'command':cmd,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
   if save:
    path=directory/'final.game.gz';record['save_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   records.append(record);(out/'peak-comparison.json').write_text(json.dumps(records,indent=2));print(tick,variant,'save' if save else 'load',round(rss/2**30,3),flush=True)
