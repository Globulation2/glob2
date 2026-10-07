"""Prepare independent greedy checkpoints; deliberately no policy timings here."""
import argparse,hashlib,json,os,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--host',choices=['mac','linux'],required=True);p.add_argument('--binary',type=Path,required=True);p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--evidence',type=Path,required=True);p.add_argument('--protocol',type=Path,required=True);a=p.parse_args()
a.root=a.root.resolve();a.binary=a.binary.resolve();a.evidence=a.evidence.resolve();a.protocol=a.protocol.resolve()
sys.path.insert(0,str(a.root/'test'))
from benchmark_parallel_compute import execute,digest
protocol=json.loads(a.protocol.read_text());a.evidence.mkdir(parents=True,exist_ok=True)
manifest=a.evidence/'preparation.json';record=json.loads(manifest.read_text()) if manifest.exists() else dict(binary_sha256=digest(a.binary),source=subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.root,text=True).strip(),host=a.host,runs=[])
families={f['id']:f for f in protocol['families']};done={r['id'] for r in record['runs']}
for split in ['training','evaluation']:
 for scenario in protocol[split]:
  if scenario['host']!=a.host or scenario['id'] in done:continue
  family=families[scenario['family']];dest=a.evidence/scenario['id'];dest.mkdir(exist_ok=True)
  fixture=a.root/family['map'] if 'map' in family else dest/'map.map.gz'
  generation=None
  if 'generator' in family:
   generation=[str(a.binary),'--generate-map',family['generator'],'--seed',str(scenario['map_seed']),'--width',str(family['size']),'--height',str(family['size']),'--teams',str(len(family['players'])),'--output',str(fixture),'--json',str(dest/'map.json')]
   if not fixture.exists():
    with (dest/'generation.log').open('w') as log:subprocess.run(generation,cwd=a.root,stdout=log,stderr=subprocess.STDOUT,check=True)
  args=['--map-file',str(fixture),'--game-seed',str(scenario['game_seed']),*sum((['--player',player] for player in family['players']),[]),'--ticks',str(family['warmup']),'--save','final','--replay','false','--compute-experiments','none','--compute-threads','1','--gradient-workers','0']
  row=execute(a.binary,args,dest/'warmup',cwd=a.root)
  if row['result']['ticks']<family['warmup']:
   raise RuntimeError(f"Preregistered scenario ended early: {scenario['id']}; retain and review before changing cohort")
  record['runs'].append(dict(**scenario,split=split,generation_command=generation,map_sha256=digest(fixture),warmup=row,checkpoint_sha256=digest(dest/'warmup/final.game.gz')))
  manifest.write_text(json.dumps(record,indent=2)+'\n')
  print(scenario['id'],row['result']['ticks'],row['wall_s'],flush=True)
