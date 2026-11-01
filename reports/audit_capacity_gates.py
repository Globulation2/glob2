"""Bounded completed-cohort helper-gate audit; no matches or strategy edits."""
import hashlib,json,shutil
from collections import Counter,defaultdict
from pathlib import Path
from tools.tournaments.results import Results
ROOT=Path(__file__).resolve().parent
assert shutil.disk_usage(ROOT).free>2*1024**3
source=Results(ROOT/'results');records=list(source);assert len(records)==288 and all(r['category']=='success' for r in records)
by_group=defaultdict(lambda:{'counts':Counter(),'sampled_histograms':defaultdict(Counter),'binding_combinations':Counter(),'unsampled_states':Counter(),'higher_capacity_games':set()})
for r in records:
 labels=r['job']['labels'];arm=labels['variant']
 if arm=='baseline':continue
 g=by_group[labels['format'],arm];race=None;meta=next(a for a in r['artifacts'] if a['path']=='stdout.log');h=hashlib.sha256();size=0
 with source.open_artifact(r,'stdout.log','rb') as f:
  while True:
   raw=f.readline(1024**2+1)
   if not raw:break
   assert len(raw)<=1024**2;h.update(raw);size+=len(raw);assert size<=meta['raw_bytes']
   if not raw.startswith(b'MAXIMA_TELEMETRY\t'):continue
   fields=raw.rstrip(b'\n').split(b'\t')
   if fields[2]!=b'0' or fields[3] not in (b'prestige_race',b'prestige_capacity'):continue
   d={k.decode():int(v) for k,v in (a.split(b'=',1) for a in fields[4:])}
   if fields[3]==b'prestige_race':race=d;continue
   g['counts']['capacity_records']+=1;assert race and race['game_tick']==d['game_tick']
   if d['pipelines']>1:g['higher_capacity_games'].add(labels['pair']);g['counts']['chosen_above1']+=1
   if not d['sampled']:
    g['counts']['unsampled_base1_records']+=1;assert d['pipelines']==1
    label=('configured1_' if d['configured']==1 else 'adaptive_')+{0:'Hold',1:'Prepare',2:'Invest',3:'Finish'}[race['state']]
    g['unsampled_states'][label]+=1;continue
   c=g['counts'];c['sampled_records']+=1
   spare=d['qualified_idle']+d['root_assigned']-d['training_reserve']-d['ordinary_unmet']-d['unobserved_crews'];worker=d['worker_limit'];material=d['material_limit'];remaining=max(1,d['remaining_steps']);chosen=d['pipelines']
   assert worker==max(1,min(d['configured'],spare//d['crew'])) and chosen==min(worker,material,remaining)
   for k in ('worker_limit','material_limit','remaining_steps','pipelines'):g['sampled_histograms'][k][str(d[k])]+=1
   g['sampled_histograms']['net_qualified_spare_workers'][str(spare)]+=1
   gates=[name for name,value in [('worker',worker),('material',material),('remaining',remaining)] if value==chosen];g['binding_combinations']['+'.join(gates)]+=1
   c['worker_limit1']+=worker==1;c['worker_limit_above1']+=worker>1;c['material_limit1']+=material==1;c['remaining_limit1']+=remaining==1
   c['worker_above1_but_chosen1']+=worker>1 and chosen==1
   c['worker_above1_blocked_by_material1']+=worker>1 and material==1
   c['worker_above1_blocked_by_remaining1']+=worker>1 and remaining==1
   c['strictly_worker_bound']+=worker<material and worker<remaining
   c['strictly_material_bound']+=material<worker and material<remaining
   c['strictly_remaining_bound']+=remaining<worker and remaining<material
 assert size==meta['raw_bytes'] and h.hexdigest()==meta['raw_sha256']
rows=[]
for (fmt,arm),g in by_group.items():
 g['higher_capacity_games']=sorted(g['higher_capacity_games']);rows.append({'format':fmt,'arm':arm,**g})
out={'scope':'complete fresh v2 enabled subject streams only; baseline has no helper decisions','groups':rows,'limits':'Binding counters are decision observations, not independent games or causal effects. Tied binding gates are reported jointly. Worker limit1 means insufficient net qualified headroom for two crews; it does not mean zero spare workers. Finish/configured1 bypasses intentionally do not sample crew/material facts.'}
text=json.dumps(out,indent=2)+'\n';assert len(text.encode())<2*1024**2;(ROOT/'capacity-gates.json').write_text(text)
print(json.dumps({'groups':[{k:v for k,v in r.items() if k!='sampled_histograms'} for r in rows]},indent=2))
