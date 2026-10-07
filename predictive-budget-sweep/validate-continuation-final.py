import json,sys
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
from check_telemetry_simulation import detailed_ticks
b=root/'artifacts/gradient-budget-sweep/binaries/glob2-candidate-mac';old=Path('/Users/bradley/.codex/worktrees/greedy-fetch-comparison/glob2/build/darwin/client/release/src/glob2');out=root/'artifacts/gradient-budget-sweep/continuation-final';out.mkdir(exist_ok=True)
p=root/'artifacts/gradient-budget-sweep/mac/large-economy-710000/warmup/final.game.gz'
base=['--load-game',str(p),'--ticks','16256','--compute-experiments','none','--compute-threads','1','--gradient-workers','0','--telemetry','checksums','--replay','true']
rows=[]
previous={r["name"]:r["run"] for r in json.loads((out/"manifest.json").read_text())} if (out/"manifest.json").exists() else {}
def run(binary,args,name):
 row=previous[name] if name in previous else execute(binary,args,out/name);rows.append(dict(name=name,run=row));(out/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n');return row
run(old,base,'old-greedy');run(b,base,'new-lazy');assert digest(out/'old-greedy/game.replay.checksums')==digest(out/'new-lazy/game.replay.checksums')
print('Experiment-off reference checksums preserved.',flush=True)
policy=['--fork-rule','building-gradient-pipeline=1','--fork-rule','building-gradient-partial=1','--building-gradient-budget-model',str(root/'artifacts/gradient-budget-sweep/models/fixed-300.json')]
run(b,[*base,*policy],'continuous');run(b,[*base,*policy,'--telemetry','building-gradient-impact'],'audited');assert digest(out/'continuous/game.replay.checksums')==digest(out/'audited/game.replay.checksums');print('Auditing preserves checksums.',flush=True)
expected=detailed_ticks((out/'continuous/game.replay.checksums').read_bytes())
for phase in [1,2,3,4,5,8]:
 short=list(base);short[short.index('--ticks')+1]=str(16000+phase)
 checkpoint=run(b,[*short,*policy,'--save','final'],f'phase-{phase}')
 run(b,['--load-game',str(out/f'phase-{phase}/final.game.gz'),'--ticks','16256','--compute-experiments','none','--compute-threads','1','--gradient-workers','4','--telemetry','checksums','--replay','true'],f'resumed-{phase}')
 lines=detailed_ticks((out/f'resumed-{phase}/game.replay.checksums').read_bytes())
 # Replay sidecars include tick numbers; compare the continuation suffix.
 assert lines and all(expected[tick]==value for tick,value in lines.items()),phase
 print('Saved model and delayed jobs continue at phase',phase,flush=True)
