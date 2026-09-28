import json,subprocess
from pathlib import Path
p=Path(__file__).resolve().parent;root=p.parents[1]
cmd=['xcrun','xctrace','record','--template','CPU Counters','--no-prompt','--time-limit','90s','--output',str(p/'cpu-counters.trace'),'--env','SDL_VIDEODRIVER=dummy','--env','SDL_AUDIODRIVER=dummy','--launch','--',str(p/'glob2'),'--run-game','--map-file',str(root/'artifacts/lazy-gradient/runs/arena512-4/generated/map-r0.map'),'--game-seed','19','--ticks','16384','--telemetry','team-timeline','--lazy-building-gradients','true','--output-dir',str(p/'hardware-run')]
for ai in ['nicowar','warrush','cortex','maxima']:cmd+=['--player',ai]
(p/'hardware-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
with (p/'hardware.log').open('w') as f:r=subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT)
(p/'hardware-status.json').write_text(json.dumps({'exit':r.returncode})+'\n')
print('xctrace exited',r.returncode)
