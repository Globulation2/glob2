import subprocess,json,os
from pathlib import Path
c=Path(__file__).parent;out=c/'final-sim80-parity-mac';out.mkdir(exist_ok=True)
binary=c/'bin/glob2-final-ca6365e83';row=next(r for r in json.loads((c/'final-performance-maps/manifest.json').read_text()) if r['format']=='3ffa');spec=row['spec']
for label,threads,probe in [('one',1,False),('four',4,False),('probe',1,True)]:
 d=out/label;d.mkdir(exist_ok=True);cmd=[str(binary),'game','run','--map-file',row['map'],'--game-seed',str(spec['game_seed']),'--compute-threads',str(threads),'--gpu','off','--ticks','4096','--telemetry','checksums','--save','every:512','--save','final','--output-dir',str(d)]
 for ai in spec['ais']:cmd+=['--player',ai]
 for a in spec['alliances']:cmd+=['--alliance',str(a)]
 if probe:cmd+=['--resource-gradient-probe']
 (d/'command.json').write_text(json.dumps(cmd,indent=2))
 with (d/'run.log').open('w') as f:subprocess.run(cmd,cwd='/Users/bradley/.codex/worktrees/adaptive-final-integration/glob2',stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
 print(label,'complete',flush=True)
for name in ['world.checksums','snapshot.checksums','game.replay.checksums']:
 ref=(out/'one'/name).read_bytes()
 for label in ['four','probe']:
  assert (out/label/name).read_bytes()==ref,(name,label)
print('PASS all per-tick checksums: 1 versus4threads and probe on/off',flush=True)
