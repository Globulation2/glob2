#!/usr/bin/env python3
"""Freeze a cross-map control arm from an existing pre-change snapshot."""
import argparse,hashlib,json,random,shutil,sys
from pathlib import Path
import optimize_maxima_portfolio as portfolio
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--seed',type=int,default=2026090588);p.add_argument('--seeds',type=int,default=2);p.add_argument('--max-steps',type=int,default=120000);a=p.parse_args();root=Path(__file__).resolve().parents[1]
if a.output.exists():p.error('output exists')
out=a.output.resolve();frozen=out/'snapshot';frozen.mkdir(parents=True)
m=json.loads((a.source/'manifest.json').read_text())
for rel,sha in m['hashes'].items():
 src=a.source/'snapshot'/rel
 if hashlib.sha256(src.read_bytes()).hexdigest()!=sha:raise ValueError('changed frozen file: '+rel)
 dest=frozen/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest)
names=['FourSquares1','G2','Garden_3','Holiday_Island_2','Isles','Migration','balanced','SmallForTwo','Mazury']
maps={Path(x['file']).stem:x for x in portfolio.discover_maps((frozen/'glob2').resolve(),root)}
seeds=random.Random(a.seed).sample(range(1,2**32),a.seeds);schedule=[]
for mi,name in enumerate(names):
 mp=maps[name];shutil.copy2(root/mp['file'],frozen/Path(mp['file']).name)
 for block,seed in enumerate(seeds):
  for seat in range(mp['teams']):
   schedule.append({'id':len(schedule)+1,'block':mi*a.seeds+block+1,'round':block+1,'format':'duel' if mp['teams']==2 else 'ffa'+str(mp['teams']),'kind':'scenario','map':mp['name'],'map_file':str(frozen/Path(mp['file']).name),'map_teams':mp['teams'],'seed':seed,'players_requested':mp['teams'],'candidate_ai':7,'opponent_ai':5,'candidate_seat':seat,'position_offset':0,'max_steps':a.max_steps,'telemetry':True})
m.update(design='paired controller validation across nine maps, two fresh seeds and every seat',paired_source=str(a.source.resolve()),seed_generator=a.seed,independent_seed_blocks=a.seeds,matches=len(schedule),jobs=2,wall_timeout_seconds=900,max_game_ticks=a.max_steps,schedule=schedule)
m['hashes']={str(p.relative_to(frozen)):hashlib.sha256(p.read_bytes()).hexdigest() for p in frozen.rglob('*') if p.is_file()}
(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(len(schedule),'matches')
