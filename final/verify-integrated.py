import sys,json,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from validate_wall_release import run,OUT,RUNS,ROOT,digest
sys.path.insert(0,str(ROOT/'test'))
from check_telemetry_simulation import detailed_tick_hashes
binary=ROOT/'build/linux/client/release/src/glob2'
rows=[]
for stage,start,save in [('early',0,'initial.game.gz'),('middle',8192,'checkpoint-8192.game.gz'),('later',16384,'final.game.gz')]:
 curves={}
 for workers in (1,4):
  name=f'verify-pr-integrated-{stage}-{workers}'
  result=run(binary,name,['--load-game',str(RUNS/'frozen'/save),'--ticks',str(start+64),'--compute-threads',str(workers),'--gradient-backend','cpu','--telemetry','checksums','--telemetry','team-timeline','--save','final','--write-replay'])
  curves[workers]=detailed_tick_hashes(RUNS/name/'game.replay.checksums')
  rows.append({'stage':stage,'workers':workers,'matching_ticks':len(curves[workers]),'finalChecksum':result['finalChecksum'],'run':name})
 assert curves[1]==curves[4],(stage,'serial/threaded mismatch')
 print(stage,'serial/threaded match',len(curves[1]),flush=True)
for workers in (1,4):
 whole=f'continuity-pr-integrated-whole-{workers}'
 run(binary,whole,['--load-game',str(RUNS/'frozen/initial.game.gz'),'--ticks','128','--compute-threads',str(workers),'--gradient-backend','cpu','--telemetry','checksums','--write-replay'])
 resumed=f'continuity-pr-integrated-resumed-{workers}'
 run(binary,resumed,['--load-game',str(RUNS/f'verify-pr-integrated-early-{workers}/final.game.gz'),'--ticks','128','--compute-threads',str(workers),'--gradient-backend','cpu','--telemetry','checksums','--write-replay'])
 a=detailed_tick_hashes(RUNS/whole/'game.replay.checksums');b=detailed_tick_hashes(RUNS/resumed/'game.replay.checksums')
 shared=a.keys()&b.keys();assert len(shared)>=64
 assert all(a[k]==b[k] for k in shared),('continuation',workers)
 rows.append({'continuation_workers':workers,'matching_ticks':len(shared),'runs':[whole,resumed]})
 print('continuation',workers,'match',len(shared),flush=True)
(OUT/'pr-final/simulation-verification.json').write_text(json.dumps({'revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'rows':rows,'binary_sha256':digest(binary)},indent=2)+'\n')
