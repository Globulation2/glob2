"""Serial balanced policy timing, with local lazy controls bracketing each block."""
import argparse,gzip,json,os,random,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--binary',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--models',type=Path,required=True);p.add_argument('--protocol',type=Path,required=True);p.add_argument('--original-binary',type=Path,required=True);a=p.parse_args()
a.root=a.root.resolve();a.evidence=a.evidence.resolve();a.models=a.models.resolve();a.protocol=a.protocol.resolve();a.original_binary=a.original_binary.resolve()
sys.path.insert(0,str(a.root/'test'))
from benchmark_parallel_compute import execute,digest
protocol=json.loads(a.protocol.read_text());prep=json.loads((a.evidence/'preparation.json').read_text());rows=[]
manifest=a.evidence/'timing-manifest.json';a.binary=a.binary.resolve()
metadata=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.root,text=True).strip(),binary_sha256=digest(a.binary),original_binary_sha256=digest(a.original_binary),protocol_sha256=digest(a.protocol))
for scenario in prep['runs']:
 if scenario['split']!='evaluation':continue
 checkpoint=a.evidence/scenario['id']/'warmup/final.game.gz';start=scenario['warmup']['result']['ticks'];stop=start+protocol['measured_ticks']
 common=['--load-game',str(checkpoint),'--ticks',str(stop),'--compute-experiments','none','--compute-threads','1','--replay','false','--building-gradient-instrumentation','off','--benchmark-warmup','0','--fork-rule','building-gradient-hybrid=0']
 blocks=[(d,w) for d in protocol['publication_delays'] for w in protocol['gradient_workers']]
 random.Random(scenario['game_seed']).shuffle(blocks)
 for rep in range(protocol['balanced_repetitions']):
  for delay,workers in (blocks if rep%2==0 else list(reversed(blocks))):
   quantiles=list(protocol['quantiles']);random.Random(scenario['game_seed']+delay*71+workers).shuffle(quantiles)
   if rep%2:quantiles.reverse()
   configs=[('original-before',None),('before',None),*[(f'p{round(q*100):02d}',q) for q in quantiles],('after',None),('original-after',None)]
   for role,q in configs:
    name=f'r{rep}-d{delay}-w{workers}-{role}';dest=a.evidence/scenario['id']/'timing'/name;stored=dest/'measurement.json.gz'
    if stored.exists():row=json.loads(gzip.open(stored,'rt').read())
    else:
     args=[*common,'--gradient-workers',str(workers),'--fork-rule',f'buildingGradientDelay={delay}','--fork-rule',f'building-gradient-pipeline={int(q is not None)}','--fork-rule',f'building-gradient-partial={int(q is not None and q<1)}']
     model=None
     if q is not None and q<1:
      model=a.models/f'p{round(q*100):02d}.json';args+=['--building-gradient-budget-model',str(model)]
     if role.startswith('original-'):
      args=['--load-game',str(checkpoint),'--ticks',str(stop),'--compute-experiments','none','--compute-threads','1','--replay','false','--benchmark-warmup','0','--gradient-workers',str(workers)]
     run=execute(a.original_binary if role.startswith('original-') else a.binary,args,dest,cwd=a.root)
     if run['result']['benchmark_measured_ticks']!=protocol['measured_ticks']:raise RuntimeError(f'Incomplete window: {name}')
     row=dict(**metadata,scenario=scenario['id'],family=scenario['family'],repeat=rep,delay=delay,workers=workers,role=role,quantile=q,background_load=os.getloadavg(),model_sha256=digest(model) if model else None,checkpoint_sha256=digest(checkpoint),run=run)
     with gzip.open(stored,'wt',compresslevel=1) as output:json.dump(row,output)
     log=dest/'engine.log'
     with log.open('rb') as src,gzip.open(str(log)+'.gz','wb',compresslevel=1) as output:output.write(src.read())
     log.unlink()
    rows.append(dict(scenario=row['scenario'],family=row['family'],repeat=rep,delay=delay,workers=workers,role=role,quantile=q,measurement=str(stored.relative_to(a.evidence)),run_ns=row['run']['result']['run_ns'],cpu_ns=row['run']['result']['benchmark_run_cpu_ns'],ticks=row['run']['result']['benchmark_measured_ticks'],peak_rss_bytes=row['run']['peak_rss_bytes']))
    manifest.write_text(json.dumps(dict(**metadata,runs=rows),indent=2)+'\n')
    print(scenario['id'],name,'ticks/s',round(protocol['measured_ticks']/(row['run']['result']['run_ns']/1e9),1),'CPU ms/tick',round(row['run']['result']['benchmark_run_cpu_ns']/protocol['measured_ticks']/1e6,3),flush=True)
print('Complete',len(rows),'retained timing runs',flush=True)
