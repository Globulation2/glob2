import sys,json,gzip,shutil
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
from check_telemetry_simulation import detailed_ticks
control=Path('/Users/bradley/.codex/worktrees/async-ai-engine/glob2/build/darwin/client/release/src/glob2');candidate=root/'build/darwin/client/release/src/glob2';out=root/'artifacts/building-gradient-857/off-ai-base-final';out.mkdir(exist_ok=True)
fixtures={'large':root/'games/gd-large-4ai.game','arena':root/'games/gd-bigarena-long.game','mixed':root/'test/fixtures/ai-random-streams/numbi-castor-v121.game.gz'}
rows=[]
for name,fixture in fixtures.items():
 stage=out/(name+'-ai-v140');
 if not stage.exists():execute(control,['--load-game',str(fixture),'--ticks','512','--compute-experiments','none','--gradient-workers','0','--save','final'],stage)
 for label,source in (('fresh',fixture),('ai-v140-load',stage/'final.game.gz')):
  expected=None
  for variant,binary in (('master',control),('candidate',candidate)):
   output=out/f'{name}-{label}-{variant}';args=['--load-game',str(source),'--ticks','768','--compute-experiments','none','--gradient-workers','0','--telemetry','checksums','--replay','true']
   row=execute(binary,args,output);trace=output/'game.replay.checksums';ticks=detailed_ticks(trace.read_bytes())
   if expected is None:expected=ticks
   if ticks!=expected:
    for tick in ticks:
     if ticks[tick]!=expected.get(tick):raise RuntimeError(f'OFF reference mismatch {name} {label} at {tick}')
   rows.append({'workload':name,'case':label,'variant':variant,'command':row['command'],'traceSha256':digest(trace),'ticks':len(ticks)})
   for path in (trace,output/'game.replay'):
    if path.exists():
     with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
     path.unlink()
   print('PASS',name,label,variant,len(ticks),flush=True)
(out/'manifest.json').write_text(json.dumps({'controlSha256':digest(control),'candidateSha256':digest(candidate),'checks':rows},indent=2))
