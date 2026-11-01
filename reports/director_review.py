"""Director subagent's independent audit; 20k seed-block bootstrap, seed51891.
Reproduces the audit whose original differently named files collided with another
agent. Reads Results and verified stdout directly; no canonical analyzer imports.
"""
import json,random,statistics,collections,shutil
from pathlib import Path
from tools.tournaments.results import Results
ROOT=Path(__file__).resolve().parent
assert shutil.disk_usage(ROOT).free>2*1024**3
src=Results(ROOT/'results');records=list(src);assert len(records)==288
ARMS=('baseline','single','adaptive');ECON=('population','workers','trained_workers','technology_workers','schools','building_sites','food_security','unserved_food','critical_food')
index={};integrals=0
for r in records:
 assert r['category']=='success'
 j=r['job'];res=r['result'];lab=j['labels'];team=res['teams'][0];assert team['team']==0
 for t in res['teams']:
  w=t['win_probability'];h=t['win_probability_history'];assert w['start_tick']==0 and w['end_tick']==res['ticks']
  assert w['area_twice_permille_ticks']==sum((a[1]+b[1])*(b[0]-a[0]) for a,b in zip(h,h[1:]));integrals+=1
 w=team['win_probability'];natural=res['termination']=='engine_end' and sum(t['alive'] for t in res['teams'])>1
 d={'pair':lab['pair'],'block':lab['block'],'format':lab['format'],'arm':lab['arm'],'job':j['id'],'mean':w['area_twice_permille_ticks']/(2000*res['ticks']),'outcome':team['outcome'],'ticks':res['ticks'],'prestige':team['prestige'],'termination':res['termination'],'winners':res['winning_teams'],'natural_prestige_finish':natural,'natural_subject_win':natural and res['winning_teams']==[0],'natural_subject_shared_win':natural and 0 in res['winning_teams'] and len(res['winning_teams'])>1,'checksum':res['finalChecksum']}
 capacity=collections.Counter();violations=collections.Counter();cancellations=collections.Counter();examples={};pending={};last_races={};race=None;director=None;areas=collections.Counter();covered=0
 end=min(res['ticks'],team['eliminated_tick']) if team['eliminated_tick']>=0 else res['ticks']
 def integrate(next_tick):
  global covered
  if director is None:return
  dt=max(0,min(next_tick,end)-min(director['game_tick'],end))
  for k in ECON:areas[k]+=director[k]*dt
  for k in ('critical_food','unserved_food'):areas[k+'_fraction']+=int(director[k]>0)*dt
  covered+=dt
 with src.open_artifact(r,'stdout.log','rt',verify=True) as stream:
  for line in stream:
   if not line.startswith('MAXIMA_TELEMETRY\t'):continue
   fields=line.rstrip().split('\t');tid=int(fields[2]);event=fields[3];v={}
   for field in fields[4:]:
    key,value=field.split('=',1)
    try:value=int(value)
    except ValueError:pass
    v[key]=value
   if event=='prestige_race':last_races[tid]=v
   if (lab['arm']=='baseline' or tid!=0) and event.startswith('prestige_'):violations['disabled_prestige_event']+=1
   if tid!=0:continue
   if event=='prestige_race':race=v
   if event=='prestige_capacity':
    n=v.get('chosen',v.get('pipelines'));capacity[n]+=1;examples.setdefault(n,v)
    if not 1<=n<=v['configured']<=4:violations['capacity_bound']+=1
    if v['sampled']:
     spare=v['qualified_idle']+v['root_assigned']-v['training_reserve']-v['ordinary_unmet']-v['unobserved_crews'];worker=max(1,min(v['configured'],int(spare/v['crew'])))
     if worker!=v['worker_limit'] or n!=min(worker,max(1,v['material_limit']),max(1,v['remaining_steps'])):violations['capacity_formula']+=1
    elif n!=1:violations['unsampled_capacity']+=1
    if race and race['state']==3 and n!=1:violations['Finish_parallel']+=1
   if event=='director_snapshot':
    integrate(v['game_tick']);director=v;active=race and race['state']!=0 and lab['arm']!='baseline';req=v['prestige_pipeline_request'];extra=req-v['school_level3']
    if active and not 1<=extra<=(4 if lab['arm']=='adaptive' else 1):violations['request_bound']+=1
    if not active and req:violations['inactive_request']+=1
    if active and extra>max(1,(race['remaining']+49)//50):violations['remaining_bound']+=1
   if event=='prestige_cancel_issued':cancellations['issued']+=1;cancellations[v.get('reason','unknown')]+=1;pending[v['gid']]=v['game_tick']
   if event=='prestige_cancel_resolved':cancellations['resolved']+=1;pending.pop(v['gid'],None)
 integrate(end)
 assert covered==end
 d.update(capacity=dict(capacity),violations=dict(violations),cancel=dict(cancellations),capacity_examples=examples,max_capacity=max(capacity,default=0),economic_coverage=covered,economic={k:v/covered for k,v in areas.items()} if covered else {},unresolved_cancels=len(pending),old_unresolved_cancels=sum(end-t>1024 for t in pending.values()),terminal_loss_gain_after_race=[t['team'] for t in res['teams'] if natural and t['outcome']=='lost' and t['team'] in last_races and t['prestige']>last_races[t['team']]['own']])
 index[lab['pair'],lab['arm']]=d
 print('.',end='',flush=True)
print()
pairs=list(dict.fromkeys(p for p,a in index));assert len(pairs)==96
ri={(r['job']['labels']['pair'],r['job']['labels']['arm']):r for r in records}
def nonpolicy(c):return {k:({team:{key:value for key,value in pars.items() if key not in ('prestige.enabled','prestige.max_pipelines')} for team,pars in v.items()} if k=='ai_params' else v) for k,v in c.items()}
for p in pairs:
 base=ri[p,'baseline']['job']
 for arm in ('single','adaptive'):
  j=ri[p,arm]['job']
  for k in ('inputs','seeds','limits','outputs'):assert j[k]==base[k]
  assert nonpolicy(j['config'])==nonpolicy(base['config'])
def interval(values):
 blocks=collections.defaultdict(list)
 for block,value in values:blocks[block].append(value)
 vals=[statistics.mean(v) for v in blocks.values()];assert len(vals)==16
 rng=random.Random(51891);draws=sorted(statistics.mean(rng.choices(vals,k=len(vals))) for _ in range(20000))
 return {'mean':statistics.mean(v for block,v in values),'ci95':[draws[500],draws[19499]],'seed_blocks':16,'pairs':len(values)}
output={'reviewer':'independent_director','coverage':{'successful_games':288,'triples':96,'all_team_integer_integrals_verified':integrals,'paired_nonpolicy_setup_identical':True,'economic_active_lifetime_coverage':1.0},'builds':sorted({r['job']['build'] for r in records}),'bootstrap':{'draws':20000,'seed':51891,'unit':'seed block, all rotations retained'},'formats':{},'games':list(index.values()),'default_recommendation':'Keep prestige.enabled=false; guarded max4 remains opt-in capability, not demonstrated superior default.'}
for fmt in ('1v1','ffa'):
 pp=[p for p in pairs if index[p,'baseline']['format']==fmt];f={'arms':{},'contrasts':{},'adverse':{}}
 for arm in ARMS:
  rows=[index[p,arm] for p in pp];caps=collections.Counter();viol=collections.Counter();canc=collections.Counter()
  for d in rows:caps.update(d['capacity']);viol.update(d['violations']);canc.update(d['cancel'])
  f['arms'][arm]={'mean':statistics.mean(d['mean'] for d in rows),'outcomes':dict(collections.Counter(d['outcome'] for d in rows)),'terminations':dict(collections.Counter(d['termination'] for d in rows)),'natural_subject_wins':sum(d['natural_subject_win'] for d in rows),'natural_subject_shared_wins':sum(d['natural_subject_shared_win'] for d in rows),'capacity_observations':dict(caps),'games_max_capacity':dict(collections.Counter(d['max_capacity'] for d in rows)),'violations':dict(viol),'cancellations':dict(canc),'economic_means':{k:statistics.mean(d['economic'][k] for d in rows if k in d['economic']) for k in rows[0]['economic']},'old_unresolved_cancel_games':sum(d['old_unresolved_cancels']>0 for d in rows),'terminal_loss_gain_games':sum(bool(d['terminal_loss_gain_after_race']) for d in rows)}
 for a,b in [('adaptive','baseline'),('single','baseline'),('adaptive','single')]:
  name=a+'-'+b;f['contrasts'][name]=interval([(index[p,a]['block'],index[p,a]['mean']-index[p,b]['mean']) for p in pp]);f['contrasts'][name]['raw_transitions']=dict(collections.Counter(index[p,b]['outcome']+'->'+index[p,a]['outcome'] for p in pp))
  worst=sorted(pp,key=lambda p:index[p,a]['mean']-index[p,b]['mean'])[:3];f['adverse'][name]=[{'pair':p,'difference':index[p,a]['mean']-index[p,b]['mean'],**{arm:{k:index[p,arm][k] for k in ('mean','outcome','ticks','prestige','max_capacity','economic')} for arm in ARMS}} for p in worst]
 output['formats'][fmt]=f
output['adaptive_single_checksum_differences']=[p for p in pairs if index[p,'adaptive']['checksum']!=index[p,'single']['checksum']]
encoded=json.dumps(output,indent=2);assert len(encoded)<1800000
(ROOT/'director-review.json').write_text(encoded)
print(json.dumps({k:v for k,v in output.items() if k not in ('games','formats')},indent=2))
