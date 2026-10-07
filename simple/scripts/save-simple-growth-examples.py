from pathlib import Path
import json,sys
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute
root=Path.cwd();out=root/'artifacts/resource-growth/simple/playable';rows=[]
for sc in ['dense','multi']:
 args=['--load-game',str(root/f'artifacts/resource-growth/controlled-fixtures/{sc}/initial.game.gz'),'--ticks','1024','--compute-threads','4','--gradient-workers','2','--compute-experiments','ai','--resource-growth-delay','8','--resource-growth-execution','shared','--save','final']
 rows.append({'scenario':sc,**execute(root/'build/linux/client/release/src/glob2',args,out/sc)})
(out/'summary.json').write_text(json.dumps(rows,indent=2))
