#!/usr/bin/env python3
"""Bounded, all-case joint-three-arm food analysis using the cohort's archived tools.
No engine execution. Counter/stopping cohorts remain separate; no forward fill.
"""
import argparse,collections,concurrent.futures as cf,csv,datetime,fcntl,hashlib,json,os,random,re,statistics,sys,math
from pathlib import Path
ARMS=['baseline','inn8-swarm8','inn8-swarm12']
CONTRASTS=[('inn8-swarm12','baseline'),('inn8-swarm8','baseline'),('inn8-swarm12','inn8-swarm8')]
CHECKPOINTS=[16384,32768,49152,65536,81920]
RX=re.compile(r'(?:^| )(tick|coverage_start|labour_coverage_start|births_[0-2]|deaths_[0-2]_[0-4]|conversionsIn_[0-2]|conversionsOut_[0-2]|consumed_0_1|consumed_1_1|harvested_1|delivered_1|stock_1|labour_(?:[0-9]|10)|filling_[0-3]_[0-3]|harvestDistance_[0-3]|harvestSamples_[0-3]|eatWalkDistance|eatWalkSamples)=(\d+)(?= |$)')
SX=re.compile(r'(?:^| )(tick|units|workers|warriors|explorers|foodCritical|needFood)=(\d+)(?= |$)')
METRICS=[('adjusted_score','score pp'),('population','units'),('births','births/10k ticks'),('worker_births','births/10k ticks'),('warrior_births','births/10k ticks'),('starvation','deaths/10k ticks'),('combat','deaths/10k ticks'),('net_conversions','units/10k ticks'),('meals','wheat/10k ticks'),('spawning','wheat/10k ticks'),('harvested','wheat/10k ticks'),('delivered','wheat/10k ticks'),('stock','wheat'),('worker_exposure','worker-ticks'),('mean_active_workers','workers'),('worker_starvation_rate','worker deaths/Mworker-ticks'),('inn_workers','active workers'),('swarm_workers','active workers'),('inn_share','ppworker-time'),('swarm_share','ppworker-time'),('site_share','ppworker-time'),('idle_share','ppworker-time'),('eat_walk_share','ppworker-time'),('eat_no_inn_share','ppworker-time')]
SRC=None

def init_worker(directory):
 global SRC
 root=Path(directory);sys.path.insert(0,str(root/'worker.pyz'))
 from tournaments.results import Results
 SRC=Results(root)

def read_game(record):
 need=[0,1]if record['job']['labels']['format']=='2v2'else[0];m={t:{}for t in need};s={t:{}for t in need};error=None
 try:
  with SRC.open_artifact(record,'stdout.log')as stream:
   for line in stream:
    if line.startswith(('GLOB2_MEASURE ','GLOB2_MEASURE_HISTORY ')):
     head=line.split(' ',2);team=int(head[1].split('=',1)[1])
     if team in m:
      v={k:int(x)for k,x in RX.findall(line.rstrip())};m[team][v['tick']]=v
    elif line.startswith(('GLOB2_TL ','GLOB2_ECON ')):
     head=line.split(' ',2);team=int(head[1].split('=',1)[1])
     if team in s:
      v={k:int(x)for k,x in SX.findall(line.rstrip())};s[team].setdefault(v['tick'],{}).update(v)
 except(Exception)as e:error=str(e)
 return record['job']['id'],{'measure':m,'state':s,'error':error}

def mean(values):return math.fsum(values)/len(values)if values else None

def combine(values):
 keys=set.intersection(*(set(v)for v in values));out={k:sum(v[k]for v in values)for k in keys}
 for k in['tick','coverage_start','labour_coverage_start']:
  if all(k in v for v in values):out[k]=max(v[k]for v in values)
 return out

def population(values):
 if any('units'not in v for v in values):return None
 return sum(v['units']for v in values)

def metrics(v,end,pop,score):
 if v.get('coverage_start')!=0:return {'population':pop,'adjusted_score':score*100 if score is not None else None}
 def total(keys):return sum(v[k]for k in keys)if all(k in v for k in keys)else None
 ci=total([f'conversionsIn_{i}'for i in range(3)]);co=total([f'conversionsOut_{i}'for i in range(3)])
 rates={
  'births':total([f'births_{i}'for i in range(3)]),
  'worker_births':v.get('births_0'),'warrior_births':v.get('births_2'),
  'starvation':total([f'deaths_{i}_1'for i in range(3)]),
  'combat':total([f'deaths_{i}_0'for i in range(3)]),
  'net_conversions':ci-co if ci is not None and co is not None else None,
  'meals':v.get('consumed_0_1'),'spawning':v.get('consumed_1_1'),'harvested':v.get('harvested_1'),'delivered':v.get('delivered_1')}
 out={k:x*10000/end if x is not None else None for k,x in rates.items()};out.update(population=pop,adjusted_score=score*100 if score is not None else None,stock=v.get('stock_1'))
 keys=[f'labour_{i}'for i in range(11)]+[f'filling_{j}_{p}'for j in range(4)for p in range(4)]
 if v.get('labour_coverage_start')==0 and all(k in v for k in keys):
  exposure=sum(v[k]for k in keys)
  if exposure>0:
   inn=sum(v[f'filling_1_{p}']for p in range(4));swarm=sum(v[f'filling_0_{p}']for p in range(4));site=sum(v[f'filling_2_{p}']for p in range(4));out.update(worker_exposure=exposure,mean_active_workers=exposure/end,worker_starvation_rate=v['deaths_0_1']*1e6/exposure if v.get('deaths_0_1')is not None else None,inn_workers=inn/end,swarm_workers=swarm/end,inn_share=100*inn/exposure,swarm_share=100*swarm/exposure,site_share=100*site/exposure,idle_share=100*v['labour_0']/exposure,eat_walk_share=100*v['labour_1']/exposure,eat_no_inn_share=100*v['labour_3']/exposure)
 return out

def quantile(values,p):
 v=sorted(values)
 if not v:return None
 x=p*(len(v)-1);lo=int(x);hi=min(lo+1,len(v)-1);return v[lo]+(v[hi]-v[lo])*(x-lo)

def vector_mean(vectors,K):
 if not vectors:return[None]*K
 return[mean([v[k]for v in vectors if v[k]is not None])for k in range(K)]

def estimate(rows,formats,draws):
 K=len(METRICS);cells=collections.defaultdict(list)
 for r in rows:cells[r['generator'],r['map_seed'],r['format']].append([r.get('delta_'+k)for k,u in METRICS])
 cell={key:vector_mean(v,K)for key,v in cells.items()};refcells=collections.defaultdict(list);cancells=collections.defaultdict(list)
 for r in rows:
  key=(r['generator'],r['map_seed'],r['format']);refcells[key].append([r.get('reference_'+m)if r.get('delta_'+m)is not None else None for m,u in METRICS]);cancells[key].append([r.get('candidate_'+m)if r.get('delta_'+m)is not None else None for m,u in METRICS])
 refcell={key:vector_mean(v,K)for key,v in refcells.items()};cancell={key:vector_mean(v,K)for key,v in cancells.items()};gens=sorted({g for g,s,f in cell});seeds={g:sorted({s for gg,s,f in cell if gg==g})for g in gens}
 def evaluate(choices,values=None):
  values=cell if values is None else values
  fm={}
  for fmt in formats:
   gg=[]
   for g,ss in choices:gg.append(vector_mean([values[g,s,fmt]for s in ss if(g,s,fmt)in values],K))
   fm[fmt]=vector_mean(gg,K)
  fm['equal_format']=[mean([fm[f][k]for f in formats])if all(fm[f][k]is not None for f in formats)else None for k in range(K)];return fm
 point=evaluate([(g,seeds[g])for g in gens]);refpoint=evaluate([(g,seeds[g])for g in gens],refcell);canpoint=evaluate([(g,seeds[g])for g in gens],cancell);boot={fmt:[[]for _ in range(K)]for fmt in formats+['equal_format']};rng=random.Random(20261103)
 if len(gens)>1 or sum(len(ss)for ss in seeds.values())>1:
  for d in range(draws):
   choices=[(g,rng.choices(seeds[g],k=len(seeds[g])))for g in rng.choices(gens,k=len(gens))];sample=evaluate(choices)
   for fmt,values in sample.items():
    for k,v in enumerate(values):
     if v is not None:boot[fmt][k].append(v)
 result=[]
 for fmt in formats+['equal_format']:
  selected=rows if fmt=='equal_format'else[r for r in rows if r['format']==fmt]
  for k,(name,unit)in enumerate(METRICS):
   values=[r['delta_'+name]for r in selected if r.get('delta_'+name)is not None];sample=boot[fmt][k];result.append({'format':fmt,'metric':name,'unit':unit,'pairs':len(selected),'metric_available_pairs':len(values),'generators':len(gens),'generator_seed_blocks':len({(r['generator'],r['map_seed'])for r in selected}),'reference_balanced_mean':refpoint[fmt][k],'candidate_balanced_mean':canpoint[fmt][k],'balanced_mean_delta':point[fmt][k],'hierarchical_95_low':quantile(sample,.025),'hierarchical_95_high':quantile(sample,.975),'valid_bootstrap_draws':len(sample),'median_pair_delta':quantile(values,.5),'p25_pair_delta':quantile(values,.25),'p75_pair_delta':quantile(values,.75),'p05_pair_delta':quantile(values,.05),'p95_pair_delta':quantile(values,.95),'zero_delta_pairs':sum(abs(v)<1e-12 for v in values)})
 return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--directory',required=True);p.add_argument('--output');p.add_argument('--workers',type=int,default=8);p.add_argument('--draws',type=int,default=2000);a=p.parse_args();root=Path(a.directory).resolve();out=Path(a.output).resolve()if a.output else root/'reports/food-common-window';assert out.is_relative_to(root/'reports');out.mkdir(parents=True,exist_ok=True)
 lock=(root/'food-analysis.lock').open('a+b')
 try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except BlockingIOError:print(json.dumps({'food_analysis_busy':True}));raise SystemExit(75)
 assert 1<=a.workers<=8 and 100<=a.draws<=10000;os.setpriority(os.PRIO_PROCESS,0,10);sys.path.insert(0,str(root/'worker.pyz'))
 from tournaments.results import Results
 from tournaments.probability import group_score
 source=Results(root);manifest=source.manifest;records={r['job']['id']:r for r in source};designed=collections.defaultdict(dict)
 for j in manifest['jobs']:
  if j['type']!='game'or j['labels'].get('variant')not in ARMS:continue
  key=(j['labels']['pair'],j['config'].get('win_probability_permille'),j['config']['ticks'],bool(j['labels'].get('full_horizon_secondary',False)),j['build']);designed[key][j['labels']['variant']]=j
 designed={k:v for k,v in designed.items()if set(v)==set(ARMS)};complete={k:v for k,v in designed.items()if all(j['id']in records and records[j['id']]['category']=='success'for j in v.values())};needed={j['id']:records[j['id']]for v in complete.values()for j in v.values()};parsed={}
 with cf.ProcessPoolExecutor(max_workers=a.workers,initializer=init_worker,initargs=(str(root),))as pool:
  futures=[pool.submit(read_game,r)for r in needed.values()]
  for f in cf.as_completed(futures):
   key,data=f.result();parsed[key]=data
   if len(parsed)%300==0:print(json.dumps({'food_logs_parsed':len(parsed),'expected':len(needed)}),flush=True)
 inventory=[];paired=[];checkpoint_rows=[]
 for key,jobs in designed.items():
  pair,threshold,horizon,secondary,build=key;lab=jobs['baseline']['labels'];cohort=f'p{threshold}-h{horizon}-secondary{int(secondary)}-b{build[:12]}';meta={'cohort':cohort,'pair':pair,'format':lab['format'],'generator':lab['generator'],'map_seed':lab['map_seed']};inv=meta|{'expected_arms':ARMS,'successful_arms':[arm for arm,j in jobs.items()if j['id']in records and records[j['id']]['category']=='success'],'available':False,'alliance_available':False if lab['format']=='2v2'else None,'reason':None};inventory.append(inv)
  if key not in complete:inv['reason']='missing or unsuccessful frozen game';continue
  data={arm:parsed[j['id']]for arm,j in jobs.items()}
  if any(d['error']for d in data.values()):inv['reason']='log parse/hash error';continue
  if any(not d['measure'][0]for d in data.values()):inv['reason']='missing subject gameplay history';continue
  terminal=min(records[j['id']]['result']['ticks']for j in jobs.values());counter_ticks=set.intersection(*(set(d['measure'][0])for d in data.values()));counter_ticks={t for t in counter_ticks if 0<t<=terminal};scopes=['subject','alliance']if lab['format']=='2v2'else['subject']
  if not counter_ticks:inv['reason']='no positive joint3arm published counter timestamp';continue
  inv['available']=True;inv['reason']=None;inv['joint_subject_counter_end']=max(counter_ticks)
  scores={};score_cohorts=[]
  for arm,j in jobs.items():
   record=records[j['id']];teams=[t for t in record['result']['teams']if t['team']in([0,1]if lab['format']=='2v2'else[0])]
   try:
    score=group_score(teams,record['result'],j['config']);valid=score.get('available')and score.get('metric')=='log_time_outcome_v1'and score.get('coefficient')==2 and score.get('horizon_ticks')==horizon;scores[arm]=score.get('score')if valid else None
    if valid:score_cohorts.append(tuple(score.get(k)for k in['metric','coefficient','horizon_ticks','model']))
   except(ValueError,KeyError,TypeError):scores[arm]=None
  if len(set(score_cohorts))>1:
   scores={arm:None for arm in ARMS};inv['score_unavailable_reason']='incompatible metric/horizon/model acrossarms'
  for scope in scopes:
   teams=[0,1]if scope=='alliance'else[0];ticks=set(counter_ticks)
   for d in data.values():
    for team in teams:ticks&=set(d['measure'][team])
   if not ticks:continue
   end=max(ticks)
   if scope=='alliance':inv['alliance_available']=True;inv['joint_alliance_counter_end']=end
   state_ticks=set.intersection(*(set(d['state'][team])for d in data.values()for team in teams));state_ticks={t for t in state_ticks if 0<t<=end};population_tick=max(state_ticks)if state_ticks else None
   windows=[('joint_end',end)]+[(str(t),t)for t in CHECKPOINTS if t in ticks]
   for window,tick in windows:
    pst=population_tick if window=='joint_end'else tick if tick in state_ticks else None;levels={}
    for arm,d in data.items():
     values=combine([d['measure'][team][tick]for team in teams]);pop=population([d['state'][team][pst]for team in teams])if pst is not None else None;levels[arm]=metrics(values,tick,pop,scores[arm])
    for candidate,reference in CONTRASTS:
     row=meta|{'scope':scope,'window':window,'common_counter_end':tick,'population_tick':pst,'candidate':candidate,'reference':reference}
     for metric,unit in METRICS:
      x,z=levels[reference].get(metric),levels[candidate].get(metric);row['reference_'+metric]=x;row['candidate_'+metric]=z;row['delta_'+metric]=z-x if x is not None and z is not None else None
     if window=='joint_end':paired.append(row)
     else:row['delta_adjusted_score']=None;checkpoint_rows.append(row)
 estimates=[]
 cohorts=sorted({r['cohort']for r in paired})
 for cohort in cohorts:
  for scope in['subject','alliance']:
   for candidate,reference in CONTRASTS:
    selected=[r for r in paired if r['cohort']==cohort and r['scope']==scope and r['candidate']==candidate and r['reference']==reference]
    if not selected:continue
    formats=['2v2']if scope=='alliance'else sorted({r['format']for r in inventory if r['cohort']==cohort});stats=estimate(selected,formats,a.draws)
    for r in stats:estimates.append({'cohort':cohort,'scope':scope,'candidate':candidate,'reference':reference,**r})
 fixed=[]
 for cohort in cohorts:
  for scope in['subject','alliance']:
   for candidate,reference in CONTRASTS:
    for tick in CHECKPOINTS:
     for fmt in['1v1','2v2','ffa']:
      rows=[r for r in checkpoint_rows if r['cohort']==cohort and r['scope']==scope and r['candidate']==candidate and r['reference']==reference and r['window']==str(tick)and r['format']==fmt]
      if not rows:continue
      fixed.append({'cohort':cohort,'scope':scope,'candidate':candidate,'reference':reference,'checkpoint':tick,'format':fmt,'joint_observed_cases':len(rows),**{'mean_delta_'+m:mean([r['delta_'+m]for r in rows if r.get('delta_'+m)is not None])for m,u in METRICS},**{'available_pairs_'+m:sum(r.get('delta_'+m)is not None for r in rows)for m,u in METRICS}})
 report={'schema_version':1,'created':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cohort':str(root),'expected_designed3arm_scenarios':len(designed),'successful3arm_scenarios':len(complete),'joint_subject_history_available_scenarios':sum(x['available']for x in inventory),'expected_games_in_designed_scenarios':len(designed)*3,'coverage_by_stopping_cohort':{cohort:{'expected':sum(x['cohort']==cohort for x in inventory),'available':sum(x['cohort']==cohort and x['available']for x in inventory)}for cohort in sorted({x['cohort']for x in inventory})},'checkpoint_coverage':fixed,'estimates':estimates,'parse_errors':{k:d['error']for k,d in parsed.items()if d['error']},'method':['All frozen designed three-arm scenarios enter the expected inventory; no hunger,changed-case,outcome or anecdote selection. Missing measurements unavailable, never zero.','Each scenario uses latest exact published timestamp shared by all three arms, at/before their shortest game. The two allies are additionally intersected for2v2 alliance totals. No forwardfill/interpolation of stocks/cumulativecounters.','All three contrasts use identical joint observation window within each scope/scenario. Counter durations vary acrossscenarios due earlystopping; rates per10k observed ticks and worker deaths/Mexactworker-ticks are secondary, not full-horizon effects.','Exact fixed checkpoints16384/32768/49152/65536/81920 retain joint-observed coverage; changing risksets and earlystops prevent unconditional late causalinterpretation. No fabricated telemetry outcome tails.','Missing counter-family fields stay unavailable, not zero; allied sums require each component to exist for both allies. Whole-match adjustedscore is not repeated as a fixed-checkpoint score.','Point estimates:scenario means withinformat/generator-seed, seeds within generator,generators equally; formats equally inaggregate. All available jointcases remain, including incompletegeometricblocks; percell coverage isexplicit.','Generator weights are equal among available generators for each planned format; missing generator/format cells remain unavailable. Aggregate requires every planned format, without silently reweighting missing formats.','Hierarchicalpercentilebootstrap resamples generator thenmapseed, retainingallformats,rosters/rotations andpairedarms jointly. Missingformatcells remainunavailable. Medians/quantiles describepairedobserved distribution.','Fillingworkerticks describeactualservicing activity, not requested/assigned staffing. Denominator=sum11labouractivities+16fillingphases. Vanilla WORKER0 death/exposure rate isexplicit.','2v2allied0+1rawcounters summedbefore ratios. Internalnetconversions cancel; subjectpopulation/food isnot alliance survival. Adjustedscore remains official log_time_outcome_v1,c2,absoluteconfiguredhorizon; scorealwaysalliance-level on2v2.','Saved/partial counter coverage notstartingat0 isunavailable for0-to-commonend rates; missingfields neverzero.','950 and0stopping cohorts,horizons andbuildidentities separated; no pooling. Primaryeffectiveness remains the frozenmainanalyzer; this is separatelyprovenanced secondarytelemetry.','Manytelemetrymetrics have unadjusted intervals; no multiplicity-corrected confirmatorymechanismclaims.']}
 def write(name,value):
  encoded=(json.dumps(value,indent=2,allow_nan=False)+'\n').encode();used=sum(p.stat().st_size for p in out.rglob('*')if p.is_file()and p.name!=name)
  if used+len(encoded)>256*1024**2:raise RuntimeError('food report256MiB budget exceeded')
  temp=out/(name+'.tmp');temp.write_bytes(encoded);temp.replace(out/name)
 def csvwrite(name,rows):
  if not rows:return
  path=out/name
  with path.open('w',newline='')as stream:
   writer=csv.DictWriter(stream,list(rows[0]));writer.writeheader();writer.writerows(rows)
  if sum(p.stat().st_size for p in out.rglob('*')if p.is_file())>256*1024**2:raise RuntimeError('food report256MiB budget exceeded')
 write('summary.json',report);write('expected-common-window-inventory.json',inventory);csvwrite('joint-common-case-deltas.csv',paired);csvwrite('balanced-estimates.csv',estimates);csvwrite('fixed-checkpoint-coverage-and-means.csv',fixed)
 lines=['# All-case joint-three-arm food telemetry','',f'Designed scenarios:{len(designed)}; successfulthree-arm gamesets:{len(complete)}; jointlyobservedfoodhistories:{sum(x["available"]for x in inventory)}.','',*report['method'],'','|Stopping cohort|Scope|Contrast|Pairs|Metric|Balanced delta|Hierarchical95% interval|Median|','|---|---|---|---:|---|---:|---|---:|']
 for r in estimates:
  if r['format']!='equal_format'or r['metric']not in['adjusted_score','starvation','worker_starvation_rate','meals','delivered','births','population','inn_workers','swarm_workers','inn_share','swarm_share','eat_no_inn_share']:continue
  fmt=lambda v:'unavailable'if v is None else f'{v:.4f}';lines.append('| '+' | '.join([r['cohort'],r['scope'],r['candidate']+' minus '+r['reference'],str(r['pairs']),r['metric'],fmt(r['balanced_mean_delta']),'['+fmt(r['hierarchical_95_low'])+', '+fmt(r['hierarchical_95_high'])+']',fmt(r['median_pair_delta'])])+' |')
 (out/'SUMMARY.md').write_text('\n'.join(lines)+'\n');write('analysis-provenance.json',{'helper_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'worker_archive_sha256':hashlib.sha256((root/'worker.pyz').read_bytes()).hexdigest(),'manifest_sha256':hashlib.sha256((root/'experiment.json').read_bytes()).hexdigest(),'package_id':manifest.get('package_id'),'build_ids':sorted({j['build']for j in manifest['jobs']}),'draws':a.draws,'bootstrap_seed':20261103,'workers':a.workers,'output_budget_bytes':256*1024**2,'source':'separate post-freeze analysis helper; frozenmanifest/analyzer unchanged'})
 print(json.dumps({'food_report':str(out),'expected_scenarios':len(designed),'complete_scenarios':len(complete),'observed_scenarios':sum(x['available']for x in inventory),'derived_bytes':sum(p.stat().st_size for p in out.rglob('*')if p.is_file())}))
if __name__=='__main__':main()
