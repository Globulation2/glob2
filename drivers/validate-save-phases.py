import sys,json,gzip,shutil,struct
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
from check_telemetry_simulation import detailed_ticks
platform=sys.argv[1];binary=root/f'build/{platform}/client/release/src/glob2';checkpoint=Path(sys.argv[2]).resolve();out=Path(sys.argv[3]).resolve();out.mkdir(exist_ok=True);rows=[];baselines=[]
def records(path):
 data=path.read_bytes();pos=20;result={}
 for tick,fields in detailed_ticks(data).items():
  result[tick]=data[pos+4:pos+8]+fields;pos+=8+len(fields)
 return result
def archive(path):
 with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
 path.unlink()
for delay in (2,4,8):
 target=out/f'd{delay}-uninterrupted'
 row=execute(binary,['--load-game',str(checkpoint),'--ticks','632','--compute-experiments','none','--gradient-workers','4','--fork-rule','building-gradient-pipeline=1','--fork-rule',f'buildingGradientDelay={delay}','--telemetry','checksums','--replay','true','--save','every:1'],target)
 baselines.append(row['command'])
 expected=records(target/'game.replay.checksums')
 for phase in range(1,delay+1):
  start=600+phase;source=target/f'checkpoint-{start}.game.gz'
  for workers in (0,4):
   dest=out/f'd{delay}-phase{phase}-w{workers}'
   result=execute(binary,['--load-game',str(source),'--ticks','632','--compute-experiments','none','--gradient-workers',str(workers),'--telemetry','checksums','--replay','true'],dest)
   actual=records(dest/'game.replay.checksums');tail={t:v for t,v in actual.items() if t>start}
   if tail!={t:expected[t] for t in tail}:raise RuntimeError(f'save continuation mismatch {dest}')
   rows.append({'delay':delay,'phase':phase,'workers':workers,'comparedTicks':len(tail),'traceSha256':digest(dest/'game.replay.checksums'),'checkpointSha256':digest(source),'command':result['command']})
   for path in (dest/'game.replay.checksums',dest/'game.replay'):archive(path)
   print('PASS',delay,phase,workers,len(tail),flush=True)
 for path in target.glob('checkpoint-*.game.gz'):
  if not 601<=int(path.name.split('-')[1].split('.')[0])<=600+delay:path.unlink()
 for path in (target/'game.replay.checksums',target/'game.replay'):archive(path)
 (out/'manifest.json').write_text(json.dumps({'binarySha256':digest(binary),'baselineCommands':baselines,'checks':rows},indent=2))
