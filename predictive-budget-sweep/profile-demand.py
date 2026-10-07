import argparse,gzip,json,shutil,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--binary',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str(a.root/'test'))
from benchmark_parallel_compute import execute,digest
from analyze_building_gradient_demand import read_match,export_aggregate
preparation=json.loads((a.evidence/'preparation.json').read_text());rows=[]
for run in preparation['runs']:
 if run['split']!='training':continue
 dest=a.evidence/run['id'];profile=dest/'demand';checkpoint=dest/'warmup/final.game.gz'
 args=['--load-game',str(checkpoint),'--ticks',str(run['warmup']['result']['ticks']+10000),'--compute-experiments','none','--compute-threads','1','--gradient-workers','0','--telemetry','building-gradient-demand','--replay','false']
 row=execute(a.binary,args,profile,cwd=a.root)
 match=read_match(profile);export_aggregate(match,profile/'building-gradient-demand-aggregate.json.gz')
 for path in profile.glob('*.csv'):
  with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
  path.unlink()
 rows.append(dict(id=run['id'],run=row,binary_sha256=digest(a.binary)));(a.evidence/'demand-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(run['id'],'observed',row['result']['ticks'],'aggregated',flush=True)
