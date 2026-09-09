#!/usr/bin/env python3
"""Analyze a frozen FourSquares FFA audit, treating each seed as one cluster."""
from __future__ import annotations
import argparse
from bisect import bisect_right
from collections import Counter,defaultdict
import csv,gzip,json,math,random
from pathlib import Path
from statistics import mean,median

CHECKPOINTS=(5000,10000,15000,20000,25000,30000,40000)
METRICS=('population','workers','warriors','combat_ready_warriors','swim_upgraded_warriors','attack_power','warriors_flagged','warriors_attacking','swarm_working','swarms','critical_food','unserved_food','schools','barracks')
ACTIVE={'transit','engage'}
def num(row,key):return float(row.get(key,0) or 0)
def avg(values):return mean(values) if values else None
def pct(n,d):return 100*n/d if d else None
def wilson(w,n):
 if not n:return [None,None]
 z=1.959963984540054;p=w/n;d=1+z*z/n;c=(p+z*z/(2*n))/d;h=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))/d
 return [max(0,c-h),min(1,c+h)]
def quantile(values,p):
 s=sorted(values);v=(len(s)-1)*p;i=int(v);return s[i]+(s[min(i+1,len(s)-1)]-s[i])*(v-i)
def cluster_interval(values):
 if not values:return [None,None]
 if max(values)==0:return [0,1-0.025**(1/len(values))]
 if min(values)==1:return [0.025**(1/len(values)),1]
 rng=random.Random(532);return [quantile([mean(rng.choices(values,k=len(values))) for _ in range(12000)],p) for p in (.025,.975)]
def features(match):
 candidate=next(p for p in match['players'] if p['role']=='candidate');team=candidate['team'];end=match['steps']
 obs=sorted((e for e in match['observer'] if e['team']==team),key=lambda e:e['tick'])
 life_end=next((e['tick'] for e in obs if num(e,'alive')==0),end)
 tele=match['telemetry'];snap=sorted((e for e in tele if e['event']=='director_snapshot'),key=lambda e:int(e['game_tick']))
 phase_changes=[(0,'idle')];launches=[];selections=[];finishes=Counter();withdraws=Counter()
 for e in tele:
  event=e['event'];tick=int(e.get('game_tick',0))
  if event=='mission_phase_changed':
   phase_changes.append((tick,e['phase']))
   if e['phase']=='transit':launches.append(e)
   if e['phase']=='withdraw':withdraws[e['reason']]+=1
  if event=='mission_selected':selections.append(e)
  if event=='mission_finished':
   finishes[e['reason']]+=1
   if e['reason']!='no_safe_rally':phase_changes.append((tick,'idle'))
 phase_changes.sort();phase_times=[t for t,_ in phase_changes]
 def phase_at(t):return phase_changes[bisect_right(phase_times,t)-1][1]
 phase_ticks=Counter()
 for i,(t,phase) in enumerate(phase_changes):
  nxt=phase_changes[i+1][0] if i+1<len(phase_changes) else life_end
  if t<life_end:phase_ticks[phase]+=max(0,min(life_end,nxt)-t)
 gate_ticks=Counter();posture_ticks=Counter();opportunity=Counter();eligible=0;first_surplus=None
 for i,e in enumerate(snap):
  t=int(e['game_tick']);nxt=int(snap[i+1]['game_tick']) if i+1<len(snap) else life_end;dt=max(0,min(nxt,life_end)-t)
  if not dt:continue
  gate=e.get('siege_gate','unknown');auth=e.get('tactical_authorization','unknown');phase=e.get('mission_phase','idle')
  category=phase if phase in ('muster','transit','engage','withdraw','cooldown') else ('colony emergency' if 'colony emergency' in e.get('tactical_decision','') else gate)
  gate_ticks[category]+=dt;posture_ticks[e.get('posture','unknown')]+=dt
  if num(e,'trained_warriors')-num(e,'defense_reserve')>=11:
   eligible+=dt;opportunity[category]+=dt
   if first_surplus is None:first_surplus=t
 bytick=defaultdict(dict)
 for e in match['observer']:bytick[e['tick']][e['team']]=e
 superiority=Counter();flag_units=0;attack_units=0;warrior_units=0;alive_ticks=0;any_fighting=0;first_flag=None;first_invasion=None;peak=0
 for i,e in enumerate(obs):
  t=e['tick'];nxt=obs[i+1]['tick'] if i+1<len(obs) else life_end;dt=max(0,min(nxt,life_end)-t)
  if not dt or not num(e,'alive'):continue
  alive_ticks+=dt;w=num(e,'warriors');peak=max(peak,w)
  flag_units+=num(e,'warriors_flagged')*dt;attack_units+=num(e,'warriors_attacking')*dt;warrior_units+=w*dt
  any_fighting+=dt*(num(e,'warriors_attacking')>0)
  if first_flag is None and num(e,'warriors_flagged')>0:first_flag=t
  if first_invasion is None and num(e,'enemy_warriors_near_colony')>=3:first_invasion=t
  enemies=[x for tid,x in bytick[t].items() if tid!=team and num(x,'alive')]
  strongest=max([num(x,'attack_power') for x in enemies],default=0)
  if enemies and num(e,'combat_ready_warriors')>=11 and num(e,'attack_power')>=1.5*max(1,strongest):
   superiority['total']+=dt;superiority['active' if phase_at(t) in ACTIVE else 'inactive']+=dt
 checkpoints=[]
 for t in CHECKPOINTS:
  allrows=bytick.get(t,{})
  if team not in allrows:continue
  own=allrows[team];others=[e for tid,e in allrows.items() if tid!=team]
  if len(others)!=3:continue
  checkpoints.append({'tick':t,'maxima_alive':num(own,'alive'),**{f'maxima_{k}':num(own,k) for k in METRICS},**{f'nicowar_{k}':mean(num(e,k) for e in others) for k in METRICS},'strongest_enemy_power':max(num(e,'attack_power') for e in others)})
 outcome='win' if candidate['won'] else 'loss' if candidate['lost'] else 'unresolved'
 return {'id':match['id'],'block':match['block'],'seed':match['seed'],'seat':match['candidate_seat'],'status':match['status'],'engine_status':match['engine_status'],'outcome':outcome,'steps':end,'life_end':life_end,'wall_seconds':match['wall_seconds'],'missions':len(selections),'launched_flags':len({e['flag'] for e in launches}),'first_launch':min((int(e['game_tick']) for e in launches),default=None),'first_flag':first_flag,'first_invasion':first_invasion,'first_trained_surplus':first_surplus,'peak_warriors':peak,'phase_ticks':dict(phase_ticks),'gate_ticks':dict(gate_ticks),'posture_ticks':dict(posture_ticks),'opportunity_ticks':dict(opportunity),'trained_surplus_ticks':eligible,'superiority':dict(superiority),'mission_finishes':dict(finishes),'withdraw_reasons':dict(withdraws),'requests':[int(e['requested']) for e in selections],'warrior_ticks':warrior_units,'flagged_warrior_ticks':flag_units,'attacking_warrior_ticks':attack_units,'any_fighting_ticks':any_fighting,'alive_observer_ticks':alive_ticks,'checkpoints':checkpoints}

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);args=parser.parse_args();out=args.directory.resolve();cache=out/'analysis-cache';cache.mkdir(exist_ok=True)
 for file in sorted((out/'match-telemetry').glob('match-*.json.gz')):
  dest=cache/(file.name.replace('.json.gz','.json'))
  if dest.exists():continue
  try:
   with gzip.open(file,'rt') as h:m=json.load(h)
  except (EOFError,json.JSONDecodeError):continue
  if m['status']=='completed':dest.write_text(json.dumps(features(m),separators=(',',':')))
 rows=[json.loads(p.read_text()) for p in sorted(cache.glob('match-*.json'))]
 if not rows:print('No complete matches yet');return
 blocks=defaultdict(list)
 for r in rows:blocks[r['block']].append(r)
 complete_blocks={b:rs for b,rs in blocks.items() if len(rs)==4}
 scored=[r for rs in complete_blocks.values() for r in rs]
 wins=sum(r['outcome']=='win' for r in scored);blockmeans=[mean(r['outcome']=='win' for r in rs) for rs in complete_blocks.values()]
 pooled=defaultdict(Counter)
 for r in scored:
  for key in ('phase_ticks','gate_ticks','posture_ticks','opportunity_ticks','superiority','mission_finishes','withdraw_reasons'):pooled[key].update(r[key])
 checkpoints=[]
 for t in CHECKPOINTS:
  cp=[(r,next((c for c in r['checkpoints'] if c['tick']==t),None)) for r in scored];cp=[(r,c) for r,c in cp if c]
  if not cp:continue
  x={'tick':t,'games':len(cp),'maxima_alive_rate':mean(c['maxima_alive'] for r,c in cp)}
  for key in METRICS:
   x['maxima_'+key]=mean(c['maxima_'+key] for r,c in cp);x['nicowar_'+key]=mean(c['nicowar_'+key] for r,c in cp)
  x['maxima_strongest_power_rate']=mean(c['maxima_attack_power']>c['strongest_enemy_power'] for r,c in cp)
  checkpoints.append(x)
 seats=[]
 for seat in range(4):
  rs=[r for r in scored if r['seat']==seat];w=sum(r['outcome']=='win' for r in rs)
  seats.append({'seat':seat,'games':len(rs),'wins':w,'win_rate':w/len(rs) if rs else None,'wilson95':wilson(w,len(rs)),'median_life_ticks':median([r['life_end'] for r in rs]) if rs else None,'launch_rate':mean(r['launched_flags']>0 for r in rs) if rs else None})
 totals={key:sum(r[key] for r in scored) for key in ('life_end','missions','launched_flags','trained_surplus_ticks','warrior_ticks','flagged_warrior_ticks','attacking_warrior_ticks','any_fighting_ticks','alive_observer_ticks')}
 summary={'completed_games':len(rows),'analyzed_balanced_games':len(scored),'complete_seed_blocks':len(complete_blocks),'expected_games':json.loads((out/'manifest.json').read_text())['matches'],'outcomes':dict(Counter(r['outcome'] for r in scored)),'win_rate':wins/len(scored) if scored else None,'win_rate_cluster95':cluster_interval(blockmeans),'interval_method':'seed-cluster percentile bootstrap; if all-zero/all-one, conservative exact bound using whether a seed has any win/loss','seats':seats,'totals':totals,**{k:dict(v) for k,v in pooled.items()},'checkpoints':checkpoints,'games_with_launch':sum(r['launched_flags']>0 for r in scored),'games_with_trained_surplus':sum(r['trained_surplus_ticks']>0 for r in scored),'games_with_superiority':sum(r['superiority'].get('total',0)>=1000 for r in scored),'games_with_superiority_and_no_launch':sum(r['superiority'].get('total',0)>=1000 and not r['launched_flags'] for r in scored),'median_first_launch':median([r['first_launch'] for r in scored if r['first_launch'] is not None]) if any(r['first_launch'] is not None for r in scored) else None,'median_life_end':median([r['life_end'] for r in scored]) if scored else None,'requests':dict(Counter(q for r in scored for q in r['requests']))}
 (out/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n')
 fields=['id','block','seed','seat','outcome','steps','life_end','missions','launched_flags','first_launch','first_flag','first_invasion','first_trained_surplus','peak_warriors','trained_surplus_ticks']
 with (out/'game-features.csv').open('w',newline='') as h:
  w=csv.DictWriter(h,fieldnames=fields,extrasaction='ignore');w.writeheader();w.writerows(scored)
 with (out/'checkpoints.csv').open('w',newline='') as h:
  if checkpoints:w=csv.DictWriter(h,fieldnames=list(checkpoints[0]));w.writeheader();w.writerows(checkpoints)
 print(json.dumps({k:v for k,v in summary.items() if k not in ('checkpoints','seats')},indent=2))
if __name__=='__main__':main()
