import os,subprocess,json,hashlib,gzip,struct
from pathlib import Path
root=Path.cwd();out=root/'artifacts/memory-optimization/peak-followup';fixtures=root/'artifacts/memory-profile/game'
binaries={'baseline':out/'baseline/glob2','candidate':root/'build/darwin/client/release/src/glob2'}
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env.pop('MallocStackLogging',None)
records=[]
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
for tick,name in [(0,'initial.game.gz'),(15000,'checkpoint-15000.game.gz'),(45000,'checkpoint-45000.game.gz')]:
 values={}
 for label,binary in binaries.items():
  d=out/f'continuation-{tick}-{label}';d.mkdir()
  command=[str(binary),'--run-game','--load-game',str(fixtures/name),'--ticks',str(tick+1024),'--telemetry','checksums','--replay','true','--save','final','--output-dir',str(d)]
  with (d/'run.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env=env,check=True)
  values[label]={'command':command,'save_sha256':digest(d/'final.game.gz'),'replay_sha256':digest(d/'game.replay'),'checksum_sha256':digest(d/'game.replay.checksums'),'bytes':(d/'game.replay.checksums').stat().st_size}
 assert values['baseline']['save_sha256']==values['candidate']['save_sha256']
 assert values['baseline']['replay_sha256']==values['candidate']['replay_sha256']
 assert values['baseline']['checksum_sha256']==values['candidate']['checksum_sha256']
 path=out/f'continuation-{tick}-baseline/game.replay.checksums'
 with path.open('rb') as f:header=f.read(20)
 ticks_written=struct.unpack('<I',header[12:16])[0];assert ticks_written==1024
 values['ticks_written']=ticks_written;values['initial_tick']=tick
 # Retain a complete compressed copy of the matched stream; delete duplicate raw data.
 with path.open('rb') as src,gzip.open(out/f'continuation-{tick}-checksums.bin.gz','wb',compresslevel=1) as dst:
  for block in iter(lambda:src.read(1048576),b''):dst.write(block)
 path.unlink();(out/f'continuation-{tick}-candidate/game.replay.checksums').unlink()
 records.append(values);(out/'continuation-comparison.json').write_text(json.dumps(records,indent=2));print(tick,'matched',ticks_written,'ticks and',values['baseline']['bytes'],'bytes; saves and replay identical',flush=True)
