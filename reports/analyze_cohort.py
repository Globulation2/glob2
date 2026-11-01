"""Analyze only a completed frozen288-game three-arm cohort; stream verified logs.

Standard Python random seed-block bootstrap; no historical games are controls.
"""
import argparse,hashlib,json,math,random,shutil,statistics,sys
from pathlib import Path
from collections import Counter,defaultdict
ROOT=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT.parents[2]))
from tools.tournaments.results import Results
from tools.tournaments.probability import summary,stopping_threshold
from tools.tournaments.analysis import adjudicate,_block_mean_interval
from tools.tournaments.common import atomic_json,file_hash,canonical
ARMS=('baseline','single','adaptive');CONTRASTS=(('adaptive','baseline'),('single','baseline'),('adaptive','single'))
STATE={0:'Hold',1:'Prepare',2:'Invest',3:'Finish'};LIMIT=2*1024**2;RESERVE=2*1024**3;MAX_LINE=1024**2
ECON=('population','workers','trained_workers','technology_workers','schools','building_sites','food_security','unserved_food','critical_food')
CAPACITY_FACTS=('sampled','qualified_idle','root_assigned','training_reserve','ordinary_unmet','unobserved_crews','crew','worker_limit','material_limit','remaining_steps')
def typed(v):return v=='true' if v in ('true','false') else int(v)
def runtime(r):return {str(p['team']):{k:typed(v) for k,v in (x.split('=',1) for x in p['runtime_values'].split(','))} for p in r['players']}
def parse(raw):
 f=raw.decode('utf-8').rstrip('\n').split('\t');d={}
 if len(f)<5:raise ValueError('malformed Maxima telemetry')
 for field in f[4:]:
  if '=' not in field:raise ValueError('malformed field')
  k,v=field.split('=',1)
  if k in d:raise ValueError('duplicate field')
  try:d[k]=int(v)
  except ValueError:d[k]=v
 return int(f[2]),f[3],d

def telemetry(source,record):
 result=record['result'];job=record['job'];cfg=job['config']['ai_params'];streams={};examples=[]
 for t in result['teams']:
  end=min(result['ticks'],t['eliminated_tick']) if t['eliminated_tick']>=0 else result['ticks']
  streams[t['team']]={'team':t['team'],'enabled':cfg[str(t['team'])]['prestige.enabled'],'maximum':cfg[str(t['team'])]['prestige.max_pipelines'],'end':end,'counts':Counter(),'chosen_capacity_observations':Counter(),'inferred_capacity_director_observations':Counter(),'state_observations':Counter(),'last_race':None,'last_capacity':None,'last_director':None,'first_director_tick':None,'last_director_tick':None,'economic_area':Counter(),'economic_coverage_ticks':0,'pending_cancel':{},'helper_reconstructed':0,'helper_unavailable':0,'max_inferred':0}
 def violation(s,kind,tick,**fields):
  s['counts'][kind]+=1
  if len(examples)<3:examples.append({'team':s['team'],'kind':kind,'tick':tick,**fields})
 def economic(s,next_tick):
  d=s['last_director']
  if d is None:return
  duration=max(0,min(next_tick,s['end'])-min(d['game_tick'],s['end']))
  for k in ECON:s['economic_area'][k]+=d[k]*duration
  for k in ('unserved_food','critical_food'):s['economic_area'][k+'_active_fraction']+=int(d[k]>0)*duration
  s['economic_coverage_ticks']+=duration
 meta=next((a for a in record['artifacts'] if a['path']=='stdout.log'),None)
 if meta is None:raise ValueError('missing stdout')
 if meta['raw_bytes']>job['limits']['log_bytes']:raise ValueError('log bound exceeded')
 h=hashlib.sha256();size=0
 with source.open_artifact(record,'stdout.log','rb',verify=True) as f:
  while True:
   raw=f.readline(MAX_LINE+1)
   if not raw:break
   if len(raw)>MAX_LINE:raise ValueError('line parser allowance exceeded')
   h.update(raw);size+=len(raw)
   if size>meta['raw_bytes']:raise ValueError('decoded log bound exceeded')
   if not raw.startswith(b'MAXIMA_TELEMETRY\t'):continue
   team,event,d=parse(raw);s=streams[team];tick=d['game_tick'];c=s['counts'];c['telemetry_records']+=1
   if event.startswith('prestige_') and not s['enabled']:violation(s,'disabled_prestige_event_violations',tick,event=event)
   if event=='prestige_race':
    if d['state'] not in STATE:raise ValueError('unknown state')
    s['last_race']=d;s['state_observations'][STATE[d['state']]]+=1;c['race_records']+=1
   elif event=='prestige_capacity':
    c['capacity_records']+=1;s['last_capacity']=d;chosen=d.get('chosen',d.get('pipelines'))
    if not isinstance(chosen,int):raise ValueError('chosen capacity missing')
    s['chosen_capacity_observations'][str(chosen)]+=1
    if d['configured']!=s['maximum']:violation(s,'configured_capacity_violations',tick,configured=d['configured'],expected=s['maximum'])
    if not 1<=chosen<=s['maximum']:violation(s,'chosen_capacity_bound_violations',tick,chosen=chosen,maximum=s['maximum'])
    r=s['last_race']
    if not r or r['game_tick']!=tick:c['capacity_unaligned_race']+=1
    else:
     if r['state']==0:violation(s,'capacity_on_Hold_violations',tick)
     if r['state']==3 and chosen>1:violation(s,'Finish_capacity_violations',tick,chosen=chosen)
     steps=max(1,(r['remaining']+49)//50)
     if chosen>steps:violation(s,'remaining_steps_bound_violations',tick,chosen=chosen,remaining_steps=steps)
    # Exact capacity-input contract uses emitted helper facts, never inferred unit totals.
    if not all(k in d for k in CAPACITY_FACTS):raise ValueError('required final capacity helper fields missing')
    if d['sampled']:
     s['helper_reconstructed']+=1
     spare=d['qualified_idle']+d['root_assigned']-d['training_reserve']-d['ordinary_unmet']-d['unobserved_crews']
     crew=max(1,d['crew']);worker_limit=max(1,min(s['maximum'],spare//crew))
     if d['worker_limit']!=worker_limit:violation(s,'worker_limit_reconstruction_violations',tick,expected=worker_limit,actual=d['worker_limit'],spare=spare)
     expected=min(worker_limit,max(1,d['material_limit']),max(1,d['remaining_steps']))
     if s['maximum']==1 or r and r['state']==3:expected=1
     if chosen!=expected:violation(s,'helper_capacity_reconstruction_violations',tick,expected=expected,chosen=chosen,spare=spare)
    else:
     c['capacity_helper_base1_bypass_records']+=1
     if chosen!=1:violation(s,'helper_unsampled_capacity_violations',tick,chosen=chosen)
   elif event=='director_snapshot':
    required=set(ECON)|{'ordinary_school_request','bid_technology','prestige_target_level','prestige_pipeline_request','desired_schools','school_level3','game_tick'}
    if not required<=d.keys():raise ValueError('missing director fields: '+str(required-d.keys()))
    c['director_records']+=1;economic(s,tick);s['last_director']=d;s['last_director_tick']=tick
    if s['first_director_tick'] is None:s['first_director_tick']=tick
    r=s['last_race'];active=s['enabled'] and r is not None and r['state']!=0
    if s['enabled'] and (r is None or r['game_tick']!=tick):c['director_school_checks_unaligned_race']+=1;continue
    inferred=d['prestige_pipeline_request']-d['school_level3'] if active else 0
    s['inferred_capacity_director_observations'][str(inferred)]+=1;s['max_inferred']=max(s['max_inferred'],inferred)
    if active:
     c['active_director_records']+=1
     if d['prestige_target_level']!=3:violation(s,'active_target_violations',tick,target=d['prestige_target_level'])
     if not 1<=inferred<=s['maximum']:violation(s,'inferred_capacity_bound_violations',tick,inferred=inferred,maximum=s['maximum'])
     if r['state']==3 and inferred>1:violation(s,'Finish_request_violations',tick,inferred=inferred)
     if inferred>max(1,(r['remaining']+49)//50):violation(s,'remaining_steps_request_violations',tick,inferred=inferred)
     cap=s['last_capacity']
     if cap and cap['game_tick']==tick and (cap['desired']!=d['prestige_pipeline_request'] or cap.get('chosen',cap.get('pipelines'))!=inferred):violation(s,'capacity_director_handoff_violations',tick)
    elif d['prestige_pipeline_request']!=0:violation(s,'inactive_pipeline_violations',tick,request=d['prestige_pipeline_request'])
    if not s['enabled'] and d['prestige_target_level']!=0:violation(s,'disabled_target_violations',tick)
    ordinary=d['ordinary_school_request'];normal=ordinary if d['bid_technology']>=cfg[str(team)]['economy.school_bid_utility_min'] else 0
    expected=max(normal,max(ordinary,d['prestige_pipeline_request']) if active else 0)
    if d['desired_schools']!=expected:violation(s,'desired_school_max_violations',tick,expected=expected,actual=d['desired_schools'])
   elif event=='prestige_cancel_issued':
    c['cancel_issued']+=1;c['cancel_reason_'+str(d.get('reason','missing'))]+=1
    if d['gid'] in s['pending_cancel']:c['cancel_reissued_same_gid']+=1
    s['pending_cancel'][d['gid']]=tick
   elif event=='prestige_cancel_resolved':
    c['cancel_resolved']+=1
    if s['pending_cancel'].pop(d['gid'],None) is None:c['cancel_resolution_without_observed_issue']+=1
 if size!=meta['raw_bytes'] or h.hexdigest()!=meta['raw_sha256']:raise ValueError('decoded log identity mismatch')
 own=None;allstreams=[]
 for team,s in streams.items():
  if s['counts']['director_records']==0:raise ValueError('missing director coverage')
  if s['enabled'] and s['counts']['race_records']==0:raise ValueError('missing enabled race coverage')
  economic(s,s['end']);coverage=s['economic_coverage_ticks'];s['economic_sample_hold_means']={k:v/coverage for k,v in s.pop('economic_area').items()} if coverage else None
  s['director_start_gap_ticks']=min(s['end'],s['first_director_tick']);s['director_tail_gap_ticks']=max(0,s['end']-s['last_director_tick'])
  s['unresolved_cancellation_gids']=len(s['pending_cancel']);s['unresolved_cancellation_gids_older_1024_ticks']=sum(s['end']-t>1024 for t in s['pending_cancel'].values())
  t=next(t for t in result['teams'] if t['team']==team);race=s['last_race'];s['terminal_own_gain_since_last_race']=t['prestige']-race['own'] if race else None
  s['potential_terminal_unsafe_own_gain']=bool(s['enabled'] and result['termination']=='engine_end' and sum(t['alive'] for t in result['teams'])>1 and t['outcome']=='lost' and race and t['prestige']>race['own'])
  for key in ('last_director','last_race','last_capacity','pending_cancel'):s.pop(key,None)
  allstreams.append({k:s[k] for k in ('team','enabled','maximum','counts','economic_coverage_ticks','director_start_gap_ticks','director_tail_gap_ticks','helper_reconstructed','helper_unavailable','potential_terminal_unsafe_own_gain')})
  if team==0:own=s
 return {'subject':own,'all_teams':allstreams,'examples':examples}

def block_ci(rows,key):
 blocks=defaultdict(list)
 for r in rows:blocks[r['block']].append(r[key])
 if len(blocks)!=16:raise ValueError('16 seed blocks required per format')
 return {'mean':statistics.mean(r[key] for r in rows),'interval95':_block_mean_interval(blocks,10000,random.Random(19)),'pairs':len(rows),'seed_blocks':16,'draws':10000,'seed':19,'resampling':'seed_block_standard_Python_random'}

def summarize_game(r,source):
 result=r['result'];own=next(t for t in result['teams'] if t['team']==0);p=summary(own,result['ticks']);w=result['winning_teams'];a=adjudicate(result)['winners'];prestige=result['termination']=='engine_end' and sum(t['alive'] for t in result['teams'])>1
 return {'job':r['job']['id'],'result_file':str((source.root/'results'/(r['job']['id']+'.json')).resolve()),'mean':p['mean'],'final_probability':p['final'],'ticks':result['ticks'],'termination':result['termination'],'engine_outcome':own['outcome'],'engine_winners':w,'adjudicated_outcome':'win' if a==[0] else 'draw' if 0 in a else 'loss','sole_engine_win':w==[0],'shared_engine_win':0 in w and len(w)>1,'natural_prestige_finish':prestige,'sole_natural_prestige_win':prestige and w==[0],'shared_natural_prestige_win':prestige and 0 in w and len(w)>1}

def analyze(source):
 manifest=source.manifest;records=list(source);games=[r for r in records if r['job']['type']=='game']
 if len(manifest['jobs'])!=288 or len(games)!=288 or any(r['category']!='success' for r in games):raise ValueError('all288 successful games required before efficacy analysis')
 expected={(fmt,arm):n for fmt,n in [('1v1',32),('ffa',64)] for arm in ARMS}
 if Counter((r['job']['labels']['format'],r['job']['labels']['variant']) for r in games)!=Counter(expected):raise ValueError('format/arm support changed')
 builds={r['job']['build'] for r in games}
 if len(builds)!=1:raise ValueError('all arms must share one new engine')
 model=manifest['design']['frozen_model'];index={};history_count=0;telemetry_rows=[];counts=Counter()
 for r in games:
  j,result=r['job'],r['result'];labels=j['labels'];key=(labels['pair'],labels['variant'])
  if key in index:raise ValueError('duplicate pair/arm')
  index[key]=r
  params=runtime(result)
  if params!=j['config']['ai_params']:raise ValueError('runtime defaults/weights differ from explicitly frozen parameters')
  for team,p in params.items():
   want_enabled=labels['variant']!='baseline' and team=='0';want_max=4 if labels['variant']=='adaptive' and team=='0' else 1
   if p['prestige.enabled']!=want_enabled or p['prestige.max_pipelines']!=want_max:raise ValueError('arm/opponent configuration differs')
  if stopping_threshold(result,j['config'])!=0 or result['resolved']['tick_limit']!=90000 or result['resolved']['save_version']!=193:raise ValueError('stopping/save configuration changed')
  if result['resolved']['rules']['noUpgrades']!=0:raise ValueError('stock upgrades-enabled audit only')
  for team in result['teams']:
   p=summary(team,result['ticks']);pts=team['win_probability_history'];history_count+=1
   if not p['whole_game'] or p['model']!=model or pts[0][0]!=0 or pts[-1][0]!=result['ticks']:raise ValueError('probability coverage/model mismatch')
   if sum((a[1]+b[1])*(b[0]-a[0]) for a,b in zip(pts,pts[1:]))!=team['win_probability']['area_twice_permille_ticks']:raise ValueError('history integration mismatch')
  tel=telemetry(source,r);telemetry_rows.append({'job':j['id'],'pair':labels['pair'],'format':labels['format'],'arm':labels['variant'],**tel})
  for s in tel['all_teams']:counts.update(s['counts'])
 if history_count!=960:raise ValueError('all960 complete team histories required')
 pairs=[]
 for pair in dict.fromkeys(p for p,a in index):
  if any((pair,a) not in index for a in ARMS):raise ValueError('incomplete triple')
  labels=index[(pair,'baseline')]['job']['labels'];row={'pair':pair,'block':labels['block'],'format':labels['format'],'generator':labels['generator'],'rotation':labels['rotation']}
  base=index[(pair,'baseline')]['job'];baseparams=base['config']['ai_params']
  for arm in ARMS:
   r=index[(pair,arm)];j=r['job']
   if any(j[k]!=base[k] for k in ('inputs','seeds','limits','outputs')):raise ValueError('paired setup changed')
   if {k:v for k,v in j['config'].items() if k!='ai_params'}!={k:v for k,v in base['config'].items() if k!='ai_params'}:raise ValueError('nonpolicy game configuration differs')
   stripped={p:{k:v for k,v in cfg.items() if k not in ('prestige.enabled','prestige.max_pipelines')} for p,cfg in j['config']['ai_params'].items()}
   if stripped!={p:{k:v for k,v in cfg.items() if k not in ('prestige.enabled','prestige.max_pipelines')} for p,cfg in baseparams.items()}:raise ValueError('nonpolicy weights differ by arm')
   row[arm]=summarize_game(r,source)
  for a,b in CONTRASTS:row[a+'_minus_'+b]=row[a]['mean']-row[b]['mean']
  pairs.append(row)
 if len(pairs)!=96:raise ValueError('all96 pairs required')
 byformat={};telindex={r['job']:r for r in telemetry_rows}
 for fmt in ('1v1','ffa'):
  rr=[r for r in pairs if r['format']==fmt];f={'contrasts':{a+'_minus_'+b:block_ci(rr,a+'_minus_'+b) for a,b in CONTRASTS},'arms':{},'contrasts_raw_transitions':{},'contrasts_adjudicated_transitions':{}}
  for a,b in CONTRASTS:
   f['contrasts_raw_transitions'][a+'_minus_'+b]=dict(Counter(r[b]['engine_outcome']+'→'+r[a]['engine_outcome'] for r in rr));f['contrasts_adjudicated_transitions'][a+'_minus_'+b]=dict(Counter(r[b]['adjudicated_outcome']+'→'+r[a]['adjudicated_outcome'] for r in rr))
  for arm in ARMS:
   c=Counter();economic=defaultdict(list);finish=defaultdict(list);capacity=Counter();games_by_max=Counter();cancel=Counter()
   for r in rr:
    s=r[arm];t=telindex[s['job']]['subject'];c['games']+=1;c['engine_'+s['engine_outcome']]+=1;c['adjudicated_'+s['adjudicated_outcome']]+=1;c[s['termination']]+=1
    for k in ('sole_engine_win','shared_engine_win','natural_prestige_finish','sole_natural_prestige_win','shared_natural_prestige_win'):c[k]+=s[k]
    if s['termination']=='engine_end':finish['natural_game_end_ticks'].append(s['ticks'])
    if s['sole_natural_prestige_win'] or s['shared_natural_prestige_win']:finish['natural_prestige_win_ticks'].append(s['ticks'])
    if t['economic_sample_hold_means']:
     for k,v in t['economic_sample_hold_means'].items():economic[k].append(v)
    capacity.update(t['inferred_capacity_director_observations']);games_by_max[str(t['max_inferred'])]+=1
    cancel.update({k:v for k,v in t['counts'].items() if k.startswith('cancel_')});cancel['unresolved_gids_at_end']+=t['unresolved_cancellation_gids'];cancel['potential_terminal_unsafe_own_gain_games']+=t['potential_terminal_unsafe_own_gain']
   f['arms'][arm]={'outcomes':dict(c),'mean_integrated_relative_score':statistics.mean(r[arm]['mean'] for r in rr),'economic_sample_hold_equal_game_means':{k:statistics.mean(v) for k,v in economic.items()},'completion_ticks':{k:{'observed_completed_games':len(v),'mean':statistics.mean(v),'median':statistics.median(v)} for k,v in finish.items()},'inferred_capacity_director_observations':dict(capacity),'games_by_maximum_inferred_capacity':dict(games_by_max),'cancellation_diagnostics':dict(cancel)}
  byformat[fmt]=f
 return {'schema':1,'games':288,'pairs':96,'complete_team_histories_integrals_exact':960,'model':model,'bundle':next(iter(builds)),'manifest':str((source.root/'experiment.json').resolve()),'manifest_sha256':file_hash(source.root/'experiment.json'),'all96_pairs_retained':True,'by_format':byformat,'telemetry_counts':dict(counts),'telemetry_contract_violations':{k:v for k,v in counts.items() if k.endswith('_violations')},'helper_facts_available_records':sum(t['helper_reconstructed'] for r in telemetry_rows for t in r['all_teams']),'helper_facts_unavailable_records':sum(t['helper_unavailable'] for r in telemetry_rows for t in r['all_teams']),'pairs_detail':pairs,'telemetry':telemetry_rows,'limitations':['All controls are newly played on this same SIM117 engine; old records supply exact map bytes and frozen parameters only.','Model score is the time-normalized observed-game integral of a frozen relative softmax model, not generally a marginal probability of any victory with co-wins.','Model-adjudicated capped outcomes are not independent completed victories. Natural completion means exclude capped games; capped outcomes remain explicitly censored.','16 seed blocks per format; all rotations retained, dependent seats resampled as whole blocks. No efficacy early stopping or activation-conditioned selection.','Capacity audit assumes the frozen stock catalogue: school level3 gives50 prestige; request minus completed prestige schools measures selected unfinished capacity, not actual funded simultaneous spending. Snapshot maxima and counts are observations, not continuous proof.','Helper reconstruction uses emitted qualified_idle+root_assigned-training_reserve-ordinary_unmet-unobserved_crews; clamp(division by crew,1,configured), then remaining/material gates; Finish and configured1 retain1. No-headroom still permits base1.','No claim that telemetry diagnostic flags prove an unsafe engine finish; cancellation telemetry lacks explicit generation/receipt lifecycle.','Legacy save193 strategy serialization and older-save default1 are implementation-validation evidence, not identifiable from fresh-game efficacy results.']}

def markdown(out,path):
 lines=['# Adaptive prestige frozen three-arm assessment','','All288 games succeeded on one new engine. Each of the96 original map/rotation pairs has newly played off, single-cap1 and adaptive-cap4 arms; opponents stay off/cap1. Historical game outcomes are not controls.','','| Format | Contrast | Integrated relative model change, pp | 95% seed-block interval, pp |','| --- | --- | ---: | --- |']
 for fmt,f in out['by_format'].items():
  for name,e in f['contrasts'].items():lines.append(f"| {fmt} | {name} | {e['mean']*100:+.3f} | [{e['interval95'][0]*100:+.3f}, {e['interval95'][1]*100:+.3f}] |")
 lines+=['','Standard Python random bootstrap:10,000 draws,seed19,16 seed blocks per format. All96 pairs remain in every contrast; observed whole-game integer integrals are primary.','','| Format/arm | Engine W/L/unresolved | Caps | Sole/shared natural prestige wins | Adjudicated W/L/D |','| --- | --- | ---: | --- | --- |']
 for fmt,f in out['by_format'].items():
  for arm,a in f['arms'].items():
   c=a['outcomes'];lines.append(f"| {fmt}/{arm} | {c.get('engine_won',0)}/{c.get('engine_lost',0)}/{c.get('engine_unresolved',0)} | {c.get('tick_cap',0)} | {c.get('sole_natural_prestige_win',0)}/{c.get('shared_natural_prestige_win',0)} | {c.get('adjudicated_win',0)}/{c.get('adjudicated_loss',0)}/{c.get('adjudicated_draw',0)} |")
 lines+=['','## Capacity and coverage','',f"All960 team histories integrate exactly. Helper facts reconstructed in {out['helper_facts_available_records']} observations; unavailable in {out['helper_facts_unavailable_records']}. Contract violations: `{out['telemetry_contract_violations']}`.",'','[Complete JSON: outcomes,completion times,economic proxies,cancellations,all pairs and capacity distributions]('+str(path.resolve())+')','[Frozen manifest]('+out['manifest']+')','','## Limits','']
 lines.extend('- '+s for s in out['limitations']);return '\n'.join(lines)+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--results',type=Path,default=ROOT/'results');p.add_argument('--output-json',type=Path,default=ROOT/'assessment.json');p.add_argument('--output-md',type=Path,default=ROOT/'assessment.md');p.add_argument('--completed-cohort',action='store_true',required=True);a=p.parse_args()
 if shutil.disk_usage(a.output_json.parent).free<RESERVE+LIMIT:raise ValueError('2GiB reserve unavailable')
 out=analyze(Results(a.results));js=canonical(out)+b'\n';md=markdown(out,a.output_json)
 if len(js)+len(md.encode())>LIMIT:raise ValueError('2MiB derived output allowance exceeded; refusing truncation')
 atomic_json(a.output_json,out);a.output_md.write_text(md);print(json.dumps({'games':288,'pairs':96,'output':str(a.output_json),'violations':out['telemetry_contract_violations'],'effects':{f:v['contrasts'] for f,v in out['by_format'].items()}},indent=2))
if __name__=='__main__':main()
