#!/usr/bin/env python3
"""Bounded archived official score analysis, paired geometry-cluster CIs, explicit coverage."""
import argparse,collections,csv,datetime as dt,gzip,json,random,re,sqlite3,statistics,sys,fcntl
from pathlib import Path
LIMIT=256*1024**2;PRIMARY='inn8-swarm12';SEED=20261102

def main():
 p=argparse.ArgumentParser();p.add_argument('--directory',required=True);a=p.parse_args();root=Path(a.directory).resolve();analysis_lock=(root/'analysis.lock').open('a+b')
 try:fcntl.flock(analysis_lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except BlockingIOError:print(json.dumps({'analysis_busy':True,'directory':str(root)}));raise SystemExit(75)
 sys.path.insert(0,str(root/'worker.pyz'))
 from tournaments.analysis import observations,probability_scores,paired_effects,percentile,adjudicate
 from tournaments.common import read_json
 from tournaments.results import Results
 from tournaments.probability import SCORE_METRIC,SCORE_COEFFICIENT
 from tournaments.game_telemetry import parse
 from tournaments.maxima_food import parse as parse_maxima
 source=Results(root);manifest=source.manifest;variants=list(manifest['design']['variants']);expected_games=[j for j in manifest['jobs']if j['type']=='game'];expected_pairs=collections.defaultdict(dict)
 for j in expected_games:expected_pairs[j['labels']['pair']][j['labels']['variant']]=j
 expected_format_counts={fmt:sum('baseline'in arms and PRIMARY in arms and arms[PRIMARY]['labels']['format']==fmt for arms in expected_pairs.values())for fmt in ['1v1','2v2','ffa']}
 intended_cluster_count=len({(j['labels']['generator'],j['labels']['map_seed'])for j in expected_games})
 records=list(source);attempts=list(source.attempts());started=dt.datetime.now(dt.timezone.utc).isoformat();out=root/'reports'/'overnight';out.mkdir(parents=True,exist_ok=True)
 def write(name,value):
  data=(json.dumps(value,indent=2,allow_nan=False)+'\n').encode();used=sum(x.stat().st_size for x in out.rglob('*')if x.is_file()and x.name!=name)
  if used+len(data)>LIMIT:raise RuntimeError('derived analysis256MiB budget exceeded; no silent truncation')
  path=out/name;temp=path.with_suffix(path.suffix+'.tmp');temp.write_bytes(data);temp.replace(path)
 indexed={r['job']['id']:r for r in records};labels={j['id']:j['labels']for j in manifest['jobs']};rows=[];observation_errors=[]
 for record in records:
  try:rows.extend(observations([record],'win_probability'))
  except(ValueError,KeyError,TypeError)as e:observation_errors.append({'job':record['job']['id'],'error':str(e)})
 standard={fmt:{'probability_scores':probability_scores([r for r in rows if r['format']==fmt],2000,SEED),'paired_effects':paired_effects([r for r in rows if r['format']==fmt],2000,SEED)}for fmt in ['1v1','2v2','ffa']}
 # Expected inventory remains full even if a generation dependency failed or dispatch stopped.
 states={};attempt_snapshot=collections.defaultdict(list);worker_snapshot=read_json(root/'supervisor-status.json')if(root/'supervisor-status.json').exists()else{};worker_states={x['id']:x['state']for x in worker_snapshot.get('status',{}).get('worker',{}).get('attempts',[])}
 try:
  con=sqlite3.connect(f'file:{root/"state.sqlite"}?mode=ro',uri=True);states=dict(con.execute('select id,state from jobs'))
  for aid,jid,state,category in con.execute('select id,job_id,state,category from attempts'):attempt_snapshot[jid].append({'attempt_id':aid,'ledger_state':state,'category':category,'worker_state_at_last_supervisor_snapshot':worker_states.get(aid)})
  con.close()
 except sqlite3.Error:pass
 inventory=[];games=[];counter_errors=[]
 for j in manifest['jobs']:
  record=indexed.get(j['id']);inventory.append({'id':j['id'],'type':j['type'],'labels':j['labels'],'coordinator_state':states.get(j['id'],'unavailable'),'accepted_category':record['category']if record else None,'result_available_at_snapshot':record is not None,'attempts':attempt_snapshot.get(j['id'],[]),'last_worker_snapshot_time':worker_snapshot.get('time')})
  if j['type']!='game':continue
  g={'id':j['id'],'labels':j['labels'],'category':record['category']if record else'unavailable','unavailable':[]};games.append(g)
  if record is None or record['category']!='success':g['unavailable'].append('no successful accepted game');continue
  result=record.get('result')or{};teams=result.get('teams',[]);subject=next((t for t in teams if t['team']==0),None)
  if subject is None:g['unavailable'].append('missing subject team');continue
  official=next((r for r in rows if r['job_id']==j['id']),None)
  g.update(ticks=result['ticks'],termination=result['termination'],engine_declared_subject_outcome=subject['outcome'],engine_declared_team_outcomes={str(t['team']):t['outcome']for t in teams},outcome_source='predicted_probability_stop'if result['termination']=='win_probability'else'actual_engine_end'if result['termination']=='engine_end'else'unresolved_tick_cap',actual_competitive_outcome=subject['outcome']if result['termination']=='engine_end'else None,predicted_stop_outcome=subject['outcome']if result['termination']=='win_probability'else None,population_subject=subject['units'],subject_group=subject['alliance']if j['labels']['format']=='2v2'else 0)
  try:g['model_adjudication']=adjudicate(result,'win_probability',alliances=j['labels']['format']=='2v2')
  except(ValueError,KeyError,TypeError)as e:g['model_adjudication']=None;g['unavailable'].append('adjudication unavailable:'+str(e))
  if official:
   probability=official['probabilities'][official['groups'].index(official['subject_group'])];g['probability']=probability;g['trajectory_score']=official['trajectory_scores'][official['groups'].index(official['subject_group'])]
   if not g['trajectory_score'].get('available'):g['unavailable'].append('adjusted trajectory score unavailable:'+str(g['trajectory_score'].get('unavailable_reason')))
   if not probability or not probability['whole_game']:g['unavailable'].append('whole-game raw model coverage')
  else:g['unavailable'].append('official model observation unavailable')
  final=None;role=collections.defaultdict(list);logmeta=next((x for x in record['artifacts']if x['path']=='stdout.log'),None)
  if logmeta is None:g['unavailable'].append('missing measurement log');continue
  try:
   with source.open_artifact(record,'stdout.log')as stream:
    for line in stream:
     if line.startswith('GLOB2_MEASURE ')and' final=1 'in(' '+line.strip()+' '):
      prefix,x=parse(line)
      if x.get('team')==0 and(final is None or x['tick']>=final['tick']):final=x
     elif j['labels'].get('diagnostic')and line.startswith('MAXIMA_TELEMETRY')and'\tstaffing_sample\t'in line:
      x=parse_maxima(line)
      if x['team']==0 and x.get('game_tick',-1)>=result['ticks']*.8 and x.get('kind')in['inn','swarm']:role[x['kind']].append(x)
  except(ValueError,OSError,KeyError,TypeError)as e:counter_errors.append({'job':j['id'],'error':str(e)});g['unavailable'].append('measurement parse/hash failure')
  if final is None:g['unavailable'].append('final gameplay counters unavailable')
  else:
   births=[v for k,v in final.items()if re.fullmatch(r'births_\d+',k)];deaths=collections.Counter()
   for k,v in final.items():
    match=re.fullmatch(r'deaths_\d+_(\d+)',k)
    if match:deaths[{'0':'combat','1':'starvation','2':'clearing','3':'trapped','4':'unknown'}.get(match[1],'unknown')]+=v
   g['counters']={'start_tick':final.get('coverage_start'),'end_tick':final['tick'],'terminal_tick':result['ticks'],'unpublished_ticks':result['ticks']-final['tick'],'births':sum(births)if births else None,'deaths_by_cause':dict(deaths)or None,'wheat_meals':final.get('consumed_0_1'),'wheat_spawning':final.get('consumed_1_1'),'wheat_harvested':final.get('harvested_1'),'wheat_delivered':final.get('delivered_1'),'wheat_stock':final.get('stock_1'),'new_buildings':sum(v for k,v in final.items()if re.fullmatch(r'variant_\d+_completed_0',k)),'demolished_buildings':sum(v for k,v in final.items()if re.fullmatch(r'variant_\d+_removed_1',k))}
   if final.get('coverage_start')!=0:g['unavailable'].append('gameplay counter start notzero')
  g['late_periodic_role_samples']={kind:{'observations':len(xs),'mean_requested':statistics.mean(x['workers']for x in xs),'mean_enrolled':statistics.mean(x['enrolled']for x in xs),'mean_fill':statistics.mean(x['corn']/x['capacity']for x in xs if x['capacity']>0)}for kind,xs in role.items()}
  g['role_diagnostic_coverage']='bounded diagnostic subset only; observation-weighted last20% of game'if role else'unavailable outside diagnostic subset or no late samples'
 # A primary geometry/format cluster requires all four frozen paired scenarios.
 paired=collections.defaultdict(dict)
 for row in rows:paired[row['pair']][row['variant']]=row
 differences=[]
 for pair,variants in paired.items():
  base=variants.get('baseline')
  if base is None:continue
  for name,row in variants.items():
   if name=='baseline':continue
   def subject_score(r):return r['trajectory_scores'][r['groups'].index(r['subject_group'])]
   av,bv=subject_score(row),subject_score(base)
   if not av or not bv or not av.get('available')or not bv.get('available')or any(av.get(k)!=bv.get(k)for k in ['metric','coefficient','horizon_ticks','model'])or av.get('metric')!=SCORE_METRIC or av.get('coefficient')!=SCORE_COEFFICIENT or av.get('horizon_ticks')!=90000 or row['win_probability_permille']!=base['win_probability_permille']:continue
   lab=labels[row['job_id']];differences.append({'pair':pair,'variant':name,'format':row['format'],'generator':lab['generator'],'map_seed':lab['map_seed'],'geometry_seed_cluster':lab['geometry_seed_cluster'],'delta':av['score']-bv['score'],'baseline_score':bv['score'],'candidate_score':av['score'],'baseline_job':base['job_id'],'candidate_job':row['job_id']})
 def cluster_summary(entries,expected_per_cluster,intended_generators=None):
  intended_generators=set(manifest['design']['generators']if intended_generators is None else intended_generators)
  by=collections.defaultdict(list)
  for e in entries:by[e['generator'],e['map_seed']].append(e['delta'])
  complete={key:statistics.mean(values)for key,values in by.items()if len(values)==expected_per_cluster};gen=collections.defaultdict(list)
  for(g,seed),value in complete.items():gen[g].append(value)
  observed=statistics.mean(statistics.mean(v)for v in gen.values())if gen else None;rng=random.Random(SEED);est=[]
  conditional=[]
  if len(complete)>1:
   keys=sorted(gen)
   for draw in range(5000):
    conditional.append(statistics.mean(statistics.mean(rng.choices(values,k=len(values)))for g,values in sorted(gen.items())))
    selected=rng.choices(keys,k=len(keys));est.append(statistics.mean(statistics.mean(rng.choices(gen[g],k=len(gen[g])))for g in selected))
  return {'paired_scenarios':len(entries),'complete_geometry_seed_clusters':len(complete),'expected_pairs_per_cluster':expected_per_cluster,'available_generators':sorted(gen),'missing_generators':sorted(intended_generators-set(gen)),'single_seed_generators':[g for g,v in gen.items()if len(v)<2],'mean_delta_equal_generator_then_seed':observed,'95_percent_interval':[percentile(est,.025),percentile(est,.975)]if est else None,'95_percent_interval_conditional_fixed_generators':[percentile(conditional,.025),percentile(conditional,.975)]if conditional else None,'interval_method':'5000 hierarchical percentile draws: resample generators, then geometry-seed clusters within selected generator; all rotations/rosters/policies/formats stay together; fixed-generator seed-only sensitivity also retained','scope':'conditional on complete observed clusters; missing intended geometry is unavailable','complete_clusters':[{'generator':g,'map_seed':seed,'mean_delta':value}for(g,seed),value in sorted(complete.items())]}
 primary={fmt:cluster_summary([e for e in differences if e['variant']==PRIMARY and e['format']==fmt],4)for fmt in ['1v1','2v2','ffa']}
 aggregate=collections.defaultdict(dict)
 for fmt,report in primary.items():
  for x in report['complete_clusters']:aggregate[x['generator'],x['map_seed']][fmt]=x['mean_delta']
 aggregate_entries=[{'generator':g,'map_seed':seed,'delta':statistics.mean(v.values())}for(g,seed),v in aggregate.items()if len(v)==3]
 equal_format=cluster_summary(aggregate_entries,1)
 by_generator={str(g):{fmt:cluster_summary([e for e in differences if e['variant']==PRIMARY and e['format']==fmt and e['generator']==g],4,[g])for fmt in ['1v1','2v2','ffa']}for g in manifest['design']['generators']}
 control_contrasts={}
 for control in [v for v in variants if v not in ['baseline',PRIMARY]]:
  entries=[]
  for pair,variants in paired.items():
   if PRIMARY not in variants or control not in variants:continue
   ca,co=variants[PRIMARY],variants[control];av=ca['trajectory_scores'][ca['groups'].index(ca['subject_group'])];bv=co['trajectory_scores'][co['groups'].index(co['subject_group'])]
   if not av or not bv or not av.get('available')or not bv.get('available')or any(av.get(k)!=bv.get(k)for k in ['metric','coefficient','horizon_ticks','model'])or av.get('metric')!=SCORE_METRIC or av.get('coefficient')!=SCORE_COEFFICIENT or av.get('horizon_ticks')!=90000 or ca['win_probability_permille']!=co['win_probability_permille']:continue
   lab=labels[ca['job_id']];entries.append({'pair':pair,'generator':lab['generator'],'map_seed':lab['map_seed'],'format':lab['format'],'delta':av['score']-bv['score'],'candidate_job':ca['job_id'],'control_job':co['job_id']})
  expected_controls=[arms[PRIMARY]for arms in expected_pairs.values()if PRIMARY in arms and control in arms];control_counts=collections.Counter((j['labels']['generator'],j['labels']['map_seed'])for j in expected_controls);assert len(set(control_counts.values()))==1;all_expected=next(iter(control_counts.values()))
  per_format={fmt:collections.Counter((j['labels']['generator'],j['labels']['map_seed'])for j in expected_controls if j['labels']['format']==fmt)for fmt in ['1v1','2v2','ffa']};assert all(len(set(v.values()))==1 for v in per_format.values())
  control_contrasts[control]={'expected_scenarios':len(expected_controls),'observed_pairs':len(entries),'paired_entries':entries,'all_selected_scenarios':cluster_summary(entries,all_expected),'by_format':{fmt:cluster_summary([e for e in entries if e['format']==fmt],next(iter(per_format[fmt].values())),sorted({j['labels']['generator']for j in manifest['jobs']if j['type']=='game'and j['labels']['variant']==control and j['labels']['format']==fmt}))for fmt in ['1v1','2v2','ffa']},'scope':'candidate minus control on identical manifest-derived scenarios; no unmatched arm averages'}
 policy_contrasts={}
 for candidate,control in [('inn8-swarm8','baseline'),(PRIMARY,'baseline'),(PRIMARY,'inn8-swarm8')]:
  entries=[];planned=[]
  for pair,arms in expected_pairs.items():
   if candidate in arms and control in arms:planned.append(arms[candidate])
  if not planned:continue
  for pair,arms in paired.items():
   if candidate not in arms or control not in arms:continue
   ca,co=arms[candidate],arms[control];av=ca['trajectory_scores'][ca['groups'].index(ca['subject_group'])];bv=co['trajectory_scores'][co['groups'].index(co['subject_group'])]
   if not av or not bv or not av.get('available')or not bv.get('available')or any(av.get(k)!=bv.get(k)for k in ['metric','coefficient','horizon_ticks','model'])or av.get('metric')!=SCORE_METRIC or av.get('coefficient')!=SCORE_COEFFICIENT or av.get('horizon_ticks')!=90000 or ca['win_probability_permille']!=co['win_probability_permille']:continue
   lab=labels[ca['job_id']];entries.append({'pair':pair,'generator':lab['generator'],'map_seed':lab['map_seed'],'format':lab['format'],'delta':av['score']-bv['score'],'candidate_job':ca['job_id'],'control_job':co['job_id']})
  counts=collections.Counter((j['labels']['generator'],j['labels']['map_seed'])for j in planned);assert set(counts.values())=={12}
  byfmt={fmt:cluster_summary([e for e in entries if e['format']==fmt],4)for fmt in ['1v1','2v2','ffa']}
  policy_contrasts[candidate+' minus '+control]={'candidate':candidate,'control':control,'expected_scenarios':len(planned),'observed_pairs':len(entries),'complete':len(entries)==len(planned),'all_formats_equal_weight':cluster_summary(entries,12),'by_format':byfmt,'paired_entries':entries}
 strata=[]
 for fmt in ['1v1','2v2','ffa']:
  for variant in variants:
   gs=[g for g in games if g['labels']['format']==fmt and g['labels']['variant']==variant];ok=[g for g in gs if g['category']=='success'];scored=[g for g in ok if g.get('trajectory_score',{}).get('available')];measured=[g for g in ok if g.get('counters')]
   def avg(values):return statistics.mean(values)if values else None
   strata.append({'format':fmt,'variant':variant,'planned':len(gs),'successful':len(ok),'adjusted_score_available':len(scored),'raw_available_game_mean_score_descriptive':avg([g['trajectory_score']['score']for g in scored]),'counter_games':len(measured),'mean_terminal_tick':avg([g['ticks']for g in ok]),'mean_final_population':avg([g['population_subject']for g in ok]),'actual_engine_end_outcomes':dict(collections.Counter(g.get('actual_competitive_outcome')for g in ok if g.get('outcome_source')=='actual_engine_end')),'predicted_probability_stop_outcomes':dict(collections.Counter(g.get('predicted_stop_outcome')for g in ok if g.get('outcome_source')=='predicted_probability_stop')),'unresolved_tick_caps':sum(g.get('termination')=='tick_cap'for g in ok),'model_cap_subject_leaders':sum(g.get('termination')=='tick_cap'and g.get('model_adjudication')is not None and g['subject_group']in g['model_adjudication']['winners']for g in ok),'mean_starvation_deaths':avg([g['counters']['deaths_by_cause'].get('starvation',0)for g in measured if g['counters']['deaths_by_cause']is not None]),'mean_births':avg([g['counters']['births']for g in measured if g['counters']['births']is not None]),'mean_wheat_meals':avg([g['counters']['wheat_meals']for g in measured if g['counters']['wheat_meals']is not None]),'mean_wheat_spawning':avg([g['counters']['wheat_spawning']for g in measured if g['counters']['wheat_spawning']is not None]),'mean_stock':avg([g['counters']['wheat_stock']for g in measured if g['counters']['wheat_stock']is not None]),'unpublished_counter_endpoint_gaps':dict(collections.Counter(g['counters']['unpublished_ticks']for g in measured)),'scope':'descriptive available games and their actual exposure; primary inference uses matched paired clusters, control contrasts use identical manifest-selected scenarios'})
 report={'schema_version':1,'primary_policy_comparison_complete':all(v['paired_scenarios']==expected_format_counts[fmt] and v['complete_geometry_seed_clusters']==intended_cluster_count for fmt,v in primary.items()),'all_planned_games_successful':sum(g['category']=='success'for g in games)==sum(j['type']=='game'for j in manifest['jobs']),'all_planned_jobs_successful':len(records)==len(manifest['jobs'])and all(r['category']=='success'for r in records),'descriptive_strata':strata,'primary_metric':{'metric':SCORE_METRIC,'coefficient':SCORE_COEFFICIENT,'horizon_ticks':90000,'win_probability_thresholds_permille':sorted({j['config'].get('win_probability_permille')for j in expected_games})},'control_contrasts':control_contrasts,'policy_contrasts':policy_contrasts,'all_policy_comparisons_complete':all(c['complete']for c in policy_contrasts.values()),'snapshot_started':started,'snapshot_finished':dt.datetime.now(dt.timezone.utc).isoformat(),'planned_jobs':len(manifest['jobs']),'accepted_results_at_snapshot':len(records),'accepted_categories':dict(collections.Counter(r['category']for r in records)),'attempt_categories':dict(collections.Counter(x['category']for x in attempts)),'expected_game_count':sum(j['type']=='game'for j in manifest['jobs']),'successful_game_count':sum(g['category']=='success'for g in games),'unavailable_game_count':sum(g['category']!='success'for g in games),'primary_by_format':primary,'primary_equal_format_aggregate':equal_format,'primary_by_generator':by_generator,'official_score_functions':standard,'paired_differences':differences,'games':games,'observation_errors':observation_errors,'counter_errors':counter_errors,'method':['Archived tournaments.analysis observations/probability_scores/paired_effects; no unbounded reanalyze telemetry export.','Primary adjusted trajectory score log_time_outcome_v1,c=2,fixed90000horizon, from archived official group_score via observations; actual/probability-stop terminal credits labeled; tickcaps have no invented tail. Legacy linear area/mean is secondary only; metric,horizon,model,stopping threshold must match.','2v2 subject alliance scored once through archived group_summary; ally probabilities never summed.','Primary paired clusters require all4scenario pairs for each format/geometry-seed. Aggregate equalweights3formats and12generators; missing intended clusters/strata explicitly unavailable.','Primary intervals resample generators then geometry-seed clusters; fixed-generator seed-only sensitivity is also reported. Scope remains the declared maps/seeds; no original-cohort pooling.','Standard score-function W/L/D uses adjudicated placements. Raw engine outcomes and termination reasons remain separate in games.','Final-probability adjudicates tick-cap survivors; no economic-score adjudication.','Counter totals use actual publication window; lasttickgap is unavailable, not zero.','Late role means use only the manifest-selected diagnostic subset; no forward fill or simultaneous inventory claim.','Original compressed logs retained. Derivedreporthardbudget256MiB; no longform/perobjectCSV.']}
 write('expected-and-finished-inventory.json',inventory);write('summary.json',report)
 lines=['# Overnight Maxima policy tournament','',f'Snapshot: {started} through {report["snapshot_finished"]}. Successful games {report["successful_game_count"]}/{report['expected_game_count']}; accepted jobs {len(records)}/{len(manifest['jobs'])}.','',*report['method'],'','| Format | Complete geometry/seed clusters | Mean paired score change(pp) | 95% interval(pp) |','| --- | ---: | ---: | --- |']
 for fmt,value in list(primary.items())+[('equal-format aggregate',equal_format)]:
  effect=value['mean_delta_equal_generator_then_seed'];interval=value['95_percent_interval'];lines.append(f'| {fmt} | {value["complete_geometry_seed_clusters"]} | {100*effect:.3f}'+' | '+str([round(100*x,3)for x in interval]if interval else None)+' |'if effect is not None else f'| {fmt} | 0 | unavailable | unavailable |')
 lines+=['','## Manifest-matched direct control contrasts','', '| Candidate minus control | Matched pairs | Equal-generator paired score change(pp) |95% interval(pp)|','|---|---:|---:|---|']
 for control,c in control_contrasts.items():
  value=c['all_selected_scenarios'];effect=value['mean_delta_equal_generator_then_seed'];interval=value['95_percent_interval'];lines.append('| '+control+' | '+str(c['observed_pairs'])+' | '+(f'{100*effect:.3f}'if effect is not None else'unavailable')+' | '+str([round(100*x,3)for x in interval]if interval else None)+' |')
 lines+=['','## All-case direct policy contrasts','','| Candidate minus control | Matched / expected | Complete clusters | Mean change(pp) | 95% interval(pp) |','| --- | ---: | ---: | ---: | --- |']
 for name,c in policy_contrasts.items():
  v=c['all_formats_equal_weight'];effect=v['mean_delta_equal_generator_then_seed'];interval=v['95_percent_interval'];lines.append('| '+name+' | '+str(c['observed_pairs'])+'/'+str(c['expected_scenarios'])+' | '+str(v['complete_geometry_seed_clusters'])+' | '+(f'{100*effect:.3f}'if effect is not None else'unavailable')+' | '+str([round(100*x,3)for x in interval]if interval else None)+' |')
 lines+=['','## Completion and food measurements (descriptive available games)','', '|Format|Variant|Success/planned|Actual engine-end outcomes|Predicted probability-stop outcomes|Tick caps|Mean population|Mean starvation|Mean meals wheat|','|---|---|---:|---|---|---:|---:|---:|---:|']
 for st in strata:
  def number(v):return 'unavailable'if v is None else f'{v:.1f}'
  lines.append('| '+' | '.join([st['format'],st['variant'],str(st['successful'])+'/'+str(st['planned']),str(st['actual_engine_end_outcomes']),str(st['predicted_probability_stop_outcomes']),str(st['unresolved_tick_caps']),number(st['mean_final_population']),number(st['mean_starvation_deaths']),number(st['mean_wheat_meals'])])+' |')
 lines+=['','Counter totals cover actual published windows, which vary with completion time; endpoint gaps and unavailable measurements are explicit in summary.json. Counts are not adjusted for different exposure durations.']
 text='\n'.join(lines)+'\n';path=out/'SUMMARY.md'
 if sum(x.stat().st_size for x in out.rglob('*')if x.is_file()and x!=path)+len(text.encode())>LIMIT:raise RuntimeError('derivedreportbudget')
 path.write_text(text);print(json.dumps({'report':str(out),'successful_games':report['successful_game_count'],'snapshot':started,'derived_bytes':sum(x.stat().st_size for x in out.rglob('*')if x.is_file())}))
if __name__=='__main__':main()
