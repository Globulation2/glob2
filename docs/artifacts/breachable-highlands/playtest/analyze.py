"""Summarize accepted games, counting exact final totals once, never repeated history."""
import json,sys
from pathlib import Path
from tools.tournaments.results import Results
from tools.tournaments.game_telemetry import parse
source=Results(sys.argv[1]);rows=[]
for r in source:
 if r['job']['type']!='game':continue
 out={'job':r['job']['id'],**r['job']['labels'],'category':r['category'],'performance':r.get('resource_usage',{})}
 result=r.get('result',{});out.update({k:result.get(k) for k in ['ticks','termination','winning_teams']})
 out['teams']=[{k:t[k] for k in ['team','alive','units','workers','warriors','buildings','sites','eliminated_tick']} for t in result.get('teams',[])]
 final={}; first_combat=None; first_melee=None; errors=[]
 with source.open_artifact(r,'stdout.log') as stream:
  for line in stream:
   # AI diagnostic records can be very large; only gameplay snapshots enter these metrics.
   if not line.startswith('GLOB2_MEASURE '):continue
   try:_,v=parse(line)
   except ValueError as e:errors.append(str(e));continue
   if any(val>0 for k,val in v.items() if k.startswith('damageDealt_')):
    first_combat=min(first_combat or v['tick'],v['tick'])
   if any(val>0 for k,val in v.items() if k.startswith('damageDealt_0_')):
    first_melee=min(first_melee or v['tick'],v['tick'])
   if v.get('final')==1:final[v['team']]=v
 out['first_combat_sample']=first_combat;out['first_melee_sample']=first_melee;out['telemetry_errors']=errors
 for t in out['teams']:
  v=final.get(t['team'],{});t['measured']=bool(v)
  for k in ['hungry','critical','meals']:t[k]=v.get(k)
  for kind in range(3):
   t['starvation_'+str(kind)]=v.get('deaths_'+str(kind)+'_1')
   t['births_'+str(kind)]=v.get('births_'+str(kind))
  for name,prefix in [('starvation','deaths_'),('combat_deaths','deaths_')]:
   cause='1' if name=='starvation' else '0'
   t[name]=sum(val for k,val in v.items() if k.startswith(prefix) and k.endswith('_'+cause))
  for name,prefix in [('melee_damage','damageDealt_0_'),('tower_damage','damageDealt_2_'),('damage','damageDealt_'),('cleared','cleared_'),('births','births_')]:
   t[name]=sum(val for k,val in v.items() if k.startswith(prefix))
 rows.append(out)
path=Path(sys.argv[1]).parent/(Path(sys.argv[1]).name+'-summary.json');path.write_text(json.dumps(rows,indent=2))
for r in rows:
 print(r['variant'],r['map_seed'],r['ai'],r['ticks'],r['termination'],'contact',r['first_combat_sample'],{k:sum(t.get(k) or 0 for t in r['teams']) for k in ['units','buildings','starvation','combat_deaths','critical','damage','cleared']})
print(path)
