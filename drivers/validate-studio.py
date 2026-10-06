import sys,json,gzip,shutil
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
binary=root/f'build/{sys.argv[1]}/client/release/src/glob2';out=Path(sys.argv[2]).resolve()
args=['--map-file',str(root/'platform/apps/engine-agent/fixtures/ais/two.map.gz'),'--game-seed','19','--player','javascript','--ai-script',f'0:{root}/examples/javascript/studio-starter.js','--player','numbi','--ticks','1024','--replay','true','--telemetry','checksums','--save','initial','--save','final']
row=execute(binary,args,out);trace=out/'game.replay.checksums'
(out/'manifest.json').write_text(json.dumps({'binarySha256':digest(binary),'traceSha256':digest(trace),'traceBytes':trace.stat().st_size,'command':row['command']},indent=2))
print(digest(trace),trace.stat().st_size,flush=True)
