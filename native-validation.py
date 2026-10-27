from pathlib import Path
import json,subprocess,os,hashlib,sys,struct,gzip
ROOT=Path.cwd();ART=ROOT/'artifacts/ci-watch/2205'
sys.path.insert(0,str(ROOT/'test'))
from check_telemetry_simulation import detailed_tick_hashes
binary=ART/'hosted-native/build/linux/client/release/src/glob2'
assets=ART/'hosted-native/build/linux/client/release/runtime-assets'
env=dict(os.environ,LD_LIBRARY_PATH=str(ART/'hosted-native/build/sdl3-ci/prefix/lib'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_CHECKSUM_SIDECAR='1')
setup=['--map-file',str(ROOT/'platform/apps/engine-agent/fixtures/ais/two.map.gz'),'--game-seed','19','--player','javascript','--ai-script','0:'+str(ROOT/'examples/javascript/studio-starter.js'),'--player','numbi']
previous=json.loads((ART/'native-commands.json').read_text()) if (ART/'native-commands.json').exists() else []
commands=[]
def run(name,args,exe=binary,assetroot=assets):
 out=ART/name;out.mkdir(exist_ok=True)
 command=[str(exe),'game','run','--data-dir',str(assetroot),*args,'--output-dir',str(out)]
 settings=dict(env,GLOB2_USER_DATA_DIR=str(out/'profile'))
 if exe!=binary:settings['LD_LIBRARY_PATH']='/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix/lib'
 cached=next((item for item in previous if item['name']==name and item['command']==command and item['exit']==0 and item['binary_sha256']==hashlib.sha256(exe.read_bytes()).hexdigest()),None)
 if cached and (out/'result.json').is_file():
  assert json.loads((out/'result.json').read_text())['status']=='completed'
  commands.append(cached)
  return out
 with (out/'run.log').open('w') as log:r=subprocess.run(command,cwd=ROOT,env=settings,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 commands.append({'name':name,'command':command,'exit':r.returncode,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
 (ART/'native-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 assert r.returncode==0,(name,r.returncode)
 return out
telemetry=['--ticks','1024','--write-replay','--telemetry','checksums','--save','initial','--save','final']
full=run('native-w1',[*setup,'--compute-threads','1',*telemetry])
reference=(full/'game.replay.checksums').read_bytes();expected=detailed_tick_hashes(full/'game.replay.checksums')
assert len(expected)==1024
for workers in (4,):
 other=run('native-w'+str(workers),[*setup,'--compute-threads',str(workers),*telemetry])
 assert (other/'game.replay.checksums').read_bytes()==reference
from compare_save_continuation import records
def raw_rows(data):
 position=20;rows={}
 for tick,payload in records(data):
  rows[tick]=data[position:position+8+len(payload)];position+=8+len(payload)
 return rows
fresh_rows=raw_rows(reference)
canonical=None
for workers in (1,4):
 other=run('native-initial-load-w'+str(workers),['--load-game',str(full/'initial.game.gz'),'--compute-threads',str(workers),*telemetry])
 data=(other/'game.replay.checksums').read_bytes()
 assert detailed_tick_hashes(other/'game.replay.checksums')==expected
 if canonical is None:canonical=data
 else:assert data==canonical
# Only the header contribution changes when the old map is serialized at186.
def version(path):
 data=gzip.decompress(path.read_bytes());length=struct.unpack_from('>I',data)[0]
 return struct.unpack_from('>I',data,4+length+4)[0]
old_version=version(ROOT/'platform/apps/engine-agent/fixtures/ais/two.map.gz')
new_version=version(full/'initial.game.gz');assert (old_version,new_version)==(133,186)
teams,players,_,_=struct.unpack_from('<4I',reference,4);rotation=7+teams+players
v=old_version^new_version;delta=((v>>rotation)|(v<<(32-rotation)))&0xffffffff
for tick,row in raw_rows(canonical).items():
 assert row[:4]==fresh_rows[tick][:4] and row[8:]==fresh_rows[tick][8:]
 assert struct.unpack_from('<I',row,4)[0]^struct.unpack_from('<I',fresh_rows[tick],4)[0]==delta
canonical_rows=raw_rows(canonical)
checkpoint=run('native-midpoint-current',[ '--load-game',str(full/'initial.game.gz'),'--compute-threads','4','--ticks','512','--save','final'])
for workers in (1,4):
 resumed=run('native-resume-w'+str(workers),['--load-game',str(checkpoint/'final.game.gz'),'--compute-threads',str(workers),*telemetry])
 ticks=detailed_tick_hashes(resumed/'game.replay.checksums');assert len(ticks)==512
 assert all(expected[tick]==value for tick,value in ticks.items()),workers
 rows=raw_rows((resumed/'game.replay.checksums').read_bytes())
 assert all(canonical_rows[tick]==row for tick,row in rows.items()),workers
# A current-source native build must reproduce the same result before any refresh.
current=run('current-native-w1',[*setup,'--compute-threads','1',*telemetry],ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2',ROOT)
assert (current/'game.replay.checksums').read_bytes()==reference
summary={'producer_revision':'697ad9d29ed4c2c7d5c965c71dd4ad54395e8306','trace_bytes':len(reference),'trace_sha256':hashlib.sha256(reference).hexdigest(),'full_ticks':len(expected),'native_worker_counts':[1,4],'initial_load_detailed_records_match':True,'map_header_upgrade':[old_version,new_version],'expected_header_xor_delta':delta,'current_format_full_traces_match_across_workers':True,'midpoint_full_tick_records_match':True,'midpoint_tick':512,'resume_worker_counts':[1,4],'current_native_full_trace_matches':True}
(ART/'native-validation.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
