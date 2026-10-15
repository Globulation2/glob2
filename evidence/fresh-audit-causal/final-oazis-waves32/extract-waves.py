"""Run only after audit.py has validated all64 Oazis wave games. Reads only."""
from pathlib import Path
import json,hashlib
O=Path(__file__).resolve().parent;R=next(q for q in O.parents if q.name=='october-regression');C=R/'coordination-redesign/confirmation/final-cohort'
d=json.loads((O/'raw-audit.json').read_text());assert d['completed_runs']==64 and d['matched_winner_pairs']==32
waves=[];last=0
for n in range(8):
 pair={}
 for role in (['baseline','wake-only'] if n%2==0 else ['wake-only','baseline']):
  q=C/'waves'/f'wave-{n}-Oazis-{role}.json';w=json.loads(q.read_text());assert w['seed_indices']==list(range(n*4,n*4+4)) and w['winners']==4 and w['caps']==0
  assert w['started_monotonic']>=last;last=w['finished_monotonic'];assert abs(last-w['started_monotonic']-w['makespan_seconds'])<1e-6
  members=[p[role] for p in d['pairs'] if p['index']//4==n];assert len(members)==4 and all(w['started_monotonic']<=p['start']<=p['end']<=last for p in members)
  masks=[json.loads(Path(p['receipt']).read_text())['allocation'] for p in members];assert len(set(sum(masks,[])))==24
  pair[role]={'makespan_seconds':w['makespan_seconds'],'winners':4,'file':str(q),'sha256':hashlib.sha256(q.read_bytes()).hexdigest(),'games':members}
 waves.append(pair)
b=sum(w['baseline']['makespan_seconds'] for w in waves);w=sum(x['wake-only']['makespan_seconds'] for x in waves)
out={'state':'completed_OA_map_waves_independently_validated','wave_pairs':waves,'actual_role_active_seconds':{'baseline':b,'wake-only':w},'makespan_change_percent':100*(w/b-1),'completed_games_per_hour':{'baseline':32*3600/b,'wake-only':32*3600/w},'throughput_change_percent':100*(b/w-1),'per_game_metrics':d['metrics'],'scope':'Eight paired4gamewaves,4/4roleorder,explicit6participants/disjoint6logicalCPUs; no perTick/platform/browser/feel proof or automaticacceptance.'}
(O/'wave-input.json').write_text(json.dumps(out,separators=(',',':'))+'\n')
print(json.dumps({k:v for k,v in out.items() if k not in ['wave_pairs','per_game_metrics']},indent=2))
