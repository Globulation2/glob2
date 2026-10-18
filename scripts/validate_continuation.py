from pathlib import Path
import subprocess,json
W=Path('/dev/shm/glob2-adaptive-official');A=Path('/home/bradley/glob2/artifacts/gradient-official-validation');binary=A/'glob2';saved=A/'checkpoint/final.game.gz'
def call(command,log):
 with (A/log).open('w') as out:subprocess.run(list(map(str,command)),cwd=W,stdout=out,stderr=subprocess.STDOUT,check=True)
cmd=[binary,'game','run','--map-file',A/'river.map.gz','--game-seed',7802,'--ticks',4096,'--compute-threads',1,'--gpu','off','--telemetry','checksums','--write-replay','--save','final','--output-dir',A/'checkpoint']
for p in range(4):cmd+=['--player','nicowar','--alliance',p+1]
call(cmd,'checkpoint.log')
call([binary,'game','run','--load-game',saved,'--ticks',8192,'--compute-threads',4,'--gpu','off','--telemetry','checksums','--write-replay','--output-dir',A/'resumed'],'resumed.log')
def rows(p):return {int(a):int(b) for a,b in (line.split() for line in p.read_text().splitlines())}
full=rows(A/'river-serial/world.checksums');resumed=rows(A/'resumed/world.checksums');common=sorted(set(full)&set(resumed));bad=[t for t in common if full[t]!=resumed[t]]
report=dict(compared_ticks=len(common),start=min(common) if common else None,end=max(common) if common else None,divergences=bad[:10],resumed_result=json.loads((A/'resumed/result.json').read_text()))
(A/'continuation.json').write_text(json.dumps(report,indent=2));print({k:v for k,v in report.items() if k!='resumed_result'})
assert common and not bad
