import argparse,hashlib,json,os,platform,subprocess,time
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True);parser.add_argument('--base',type=Path,required=True);parser.add_argument('--head',type=Path,required=True);parser.add_argument('--base-cwd',type=Path,required=True);parser.add_argument('--head-cwd',type=Path,required=True);args=parser.parse_args()
p=args.root.resolve();cases=json.loads((p/'cases.json').read_text());bins={'base':args.base.resolve(),'head':args.head.resolve()};cwd={'base':args.base_cwd.resolve(),'head':args.head_cwd.resolve()}
def sha(f):
 h=hashlib.sha256()
 with f.open('rb') as stream:
  for b in iter(lambda:stream.read(1048576),b''):h.update(b)
 return h.hexdigest()
env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
metadata={'platform':platform.platform(),'machine':platform.machine(),'binaries':{v:{'path':str(b),'sha256':sha(b)} for v,b in bins.items()},'cases':cases,'commands':[]}
(p/'run-plan.json').write_text(json.dumps(metadata,indent=2)+'\n');records=[]
def run(name,variant,extra,expected_start):
 dest=p/'runs'/name/variant;dest.parent.mkdir(exist_ok=True,parents=True)
 assert not dest.exists()
 cmd=[str(bins[variant]),'--run-game',*extra,'--telemetry','checksums','--telemetry','team-timeline','--save','initial','--save','final','--output-dir',str(dest)]
 print('RUN',name,variant,flush=True)
 start=time.monotonic()
 with dest.with_suffix('.log').open('w') as f:r=subprocess.run(cmd,cwd=cwd[variant],env=env,stdout=f,stderr=subprocess.STDOUT)
 record={'case':name,'variant':variant,'start_tick':expected_start,'command':cmd,'cwd':str(cwd[variant]),'exit_code':r.returncode,'wall_s':time.monotonic()-start,'files':{}}
 assert r.returncode==0,record
 record['result']=json.loads((dest/'result.json').read_text())
 for f in sorted(dest.iterdir()):
  if f.is_file() and (f.suffix=='.game' or f.name=='game.replay.checksums'):record['files'][f.name]={'sha256':sha(f),'bytes':f.stat().st_size}
 records.append(record);(p/'results.json').write_text(json.dumps(records,indent=2)+'\n')
 print('DONE',name,variant,record['result']['ticks'],flush=True)
for c in cases:
 map_file=p/'inputs'/c['map_file'];assert sha(map_file)==c['map_sha256']
 extra=['--map-file',str(map_file),'--game-seed',str(c['game_seed']),'--ticks',str(c['ticks']),'--save','every:4096']
 for ai in c['players']:extra+=['--player',ai]
 for v in bins:run(c['name'],v,extra,0)
# All hosts load the exact same old-baseline checkpoint provided by macOS.
source=p/'inputs/continuation.game'
if not source.exists():
 import shutil
 shutil.copy2(p/'runs'/cases[-1]['name']/'base/checkpoint-4096.game',source)
metadata['continuation_input_sha256']=sha(source);(p/'run-plan.json').write_text(json.dumps(metadata,indent=2)+'\n')
for v in bins:run('continuation',v,['--load-game',str(source),'--ticks','8192'],4096)
for name in [c['name'] for c in cases]+['continuation']:
 a,b=[next(r for r in records if r['case']==name and r['variant']==v) for v in bins]
 print('COMPARE',name,{k:a['files'][k]==b['files'][k] for k in a['files']},flush=True)
print('COMPLETE',flush=True)
