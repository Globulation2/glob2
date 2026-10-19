from pathlib import Path
import json,csv,hashlib,collections,itertools,math,datetime
R=Path(__file__).resolve().parent;C=R.parent/'collection/remote';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(name,v):(R/name).write_text(json.dumps(v,indent=2)+'\n')
plan=json.loads((R/'PLAN.json').read_text());lock=json.loads((R/'INPUTS-LOCK.json').read_text());assert sha(R/'PLAN.json')==lock['analysis_plan_sha256'];assert sha(C/'INPUTS.json')==lock['collector_inputs_sha256'];status=json.loads((C/'STATUS.json').read_text());assert status['status']=='COMPLETE' and status['completed']==3
seal=json.loads((C.parent/'SEALED.json').read_text());assert seal['remote_status_sha256']==sha(C/'STATUS.json') and seal['maps']==3 and seal['failures']==seal['caps']==0
# Verify per-run raw inventories and collection bindings before opening labels.
assert status['gate_sha256']==lock['runtime_gate_sha256'] and status['binary_sha256']==lock['binary_sha256'] and status['inputs_sha256']==lock['collector_inputs_sha256']
binding=json.loads((C/'ANALYSIS-LOCK-BINDING.json').read_text());assert status['analysis_binding_sha256']==sha(C/'ANALYSIS-LOCK-BINDING.json');assert binding['collection_inputs_sha256']==lock['collector_inputs_sha256'];assert set(binding['files'].values())=={sha(R/'PLAN.json'),sha(R/'INPUTS-LOCK.json')}
files={};allrows=[];census={};F=['materials','markets','guard','clear','forbidden'];mapmeta={m['group']:m for m in plan['input_scope']['maps']};joincols=['tick','family','team','movement','material','key','serving_capture_tick']
for run in status['rows']:
 group=run['group'];M=mapmeta[group];P=C/'runs'/group;assert run['returncode']==0 and not run['capped'] and run['actual_ticks_advanced']==1024 and run['start_tick']==16384 and run['end_tick']==17408 and run['threshold_milli']==128000;assert all(v is True for v in run['unchanged_H128_and_world'].values())
 for name,h in run['artifact_hashes'].items():assert sha(P/name)==h,(group,name);files[str((P/name).relative_to(C))]=h
 command=json.loads((P/'COMMAND.json').read_text());assert not any(k.startswith('GLOB2_') for k in command['environment']);assert command['warm_save_sha256']==M['warm_save_sha256'];assert command['gate_sha256']==lock['runtime_gate_sha256'];assert command['inputs_sha256']==lock['collector_inputs_sha256'];assert '--resource-gradient-exploration' not in command['argv'];assert run['result']['adaptive_gradient_policy']['threshold_milli_direction_cost']==128000
 originals=list(csv.DictReader((P/'gradient-episodes.csv').open()));original={tuple(int(x[k]) for k in joincols):x for x in originals};assert len(original)==len(originals)
 with (P/'paired-horizons.csv').open() as f:
  reader=csv.DictReader(f);assert reader.fieldnames==plan['raw_sidecar_columns'];pairs=list(reader)
 assert len(pairs)==len(originals)==run['paired_rows'];seen=set();counts=collections.Counter()
 for raw in pairs:
  x={k:int(v) for k,v in raw.items()};assert all(str(x[k])==v for k,v in raw.items());key=tuple(x[k] for k in joincols);assert key not in seen and key in original;seen.add(key);o=original[key]
  assert 0<=x['family']<5 and 0<=x['team']<32 and 0<=x['movement']<=8 and 0<=x['material']<=255;assert x['key']==(x['family']<<24|x['team']<<16|x['movement']<<8|x['material']);assert x['publication_delay']==int(o['publication_delay'])==8
  assert x['full_complete']==int(o['complete']) and x['full_queries']==int(o['queries']) and x['defer_extra_128']==int(o['defer_extra']) and x['refresh_extra_128']==int(o['refresh_extra'])
  age=x['observed_through_tick']-x['tick'];assert x['short_complete']==int(age>=32) and x['full_complete']==int(age>=128)
  for short,long in [('short_queries','full_queries'),('defer_extra_32','defer_extra_128'),('refresh_extra_32','refresh_extra_128')]:assert 0<=x[short]<=x[long]
  known=int(o['capture_known'])==1;movement=int(o['movement_observed'])>0;eligible=bool(x['short_complete'] and x['full_complete'] and known and movement)
  counts['raw']+=1;counts['both_complete']+=bool(x['full_complete']);counts['short_only_complete']+=bool(x['short_complete'] and not x['full_complete']);counts['both_censored']+=not x['short_complete'];counts['unknown_capture']+=not known;counts['no_movement_label']+=not movement;counts['eligible']+=eligible
  x.update(group=M['map_group'],run_group=group,format=M['format'],family_name=F[x['family']],movement_class='air' if x['movement']==8 else 'ground',cohort_role='diagnostic',cohort='paired-horizon-fixed128',collection=group,eligible=eligible,B32=x['defer_extra_32']-x['refresh_extra_32'],B128=x['defer_extra_128']-x['refresh_extra_128'],Q32=x['short_queries'],Q128=x['full_queries']);x['Blate']=x['B128']-x['B32'];x['Qlate']=x['Q128']-x['Q32'];allrows.append(x)
 census[group]=dict(counts)
write('RAW-INPUTS-LOCK.json',dict(status_sha256=sha(C/'STATUS.json'),collection_seal_sha256=sha(C.parent/'SEALED.json'),analysis_plan_sha256=sha(R/'PLAN.json'),analysis_lock_sha256=sha(R/'INPUTS-LOCK.json'),gate_sha256=status['gate_sha256'],binding_sha256=status['analysis_binding_sha256'],files=files,census=census,join='Exact one-to-one run/tick/family/team/movement/material/key/servingcapture; original H128 sums/query/completion unchanged'))
rows=[x for x in allrows if x['eligible']];primary=[x for x in rows if x['family']==0 and x['movement']<8]
def weights(pop):
 hierarchy=['format','cohort_role','cohort','group','family_name','collection'];children=collections.defaultdict(set);counts=collections.Counter();paths=[]
 for x in pop:
  path=()
  for k in hierarchy:children[path].add(x[k]);path=(*path,x[k])
  counts[path]+=1;paths.append(path)
 out=[]
 for path in paths:
  den=counts[path]
  for i in range(len(path)):den*=len(children[path[:i]])
  out.append(1/den)
 return out

def mean(v,w):return sum(a*b for a,b in zip(v,w))/sum(w) if sum(w)>0 else None
def dist(v,w):
 values=sorted((a,b) for a,b in zip(v,w) if b>0)
 if not values:return dict(n=0,mean=None,median=None,p10=None,p25=None,p75=None,p90=None,min=None,max=None)
 total=sum(b for a,b in values)
 def q(p):
  c=0
  for a,b in values:
   c+=b
   if c>=p*total-1e-14:return a
  return values[-1][0]
 return dict(n=len(values),mean=mean([a for a,b in values],[b for a,b in values]),median=q(.5),p10=q(.1),p25=q(.25),p75=q(.75),p90=q(.9),min=values[0][0],max=values[-1][0])
def decision(pop,w,cutoff):
 ideal=[x['B32']>=32 for x in pop];take=[x['B128']>=cutoff for x in pop];return dict(cutoff_units=cutoff,short_ideal_raw=sum(ideal),short_ideal_fraction=mean(ideal,w),long_raw=sum(take),long_fraction=mean(take,w),false_positive_fraction=mean([a and not b for a,b in zip(take,ideal)],w),false_negative_fraction=mean([b and not a for a,b in zip(take,ideal)],w),disagreement_fraction=mean([a!=b for a,b in zip(take,ideal)],w))
def oracle(pop,w):
 if not pop:return dict(status='unmeasured')
 target=mean([x['B32']>=32 for x in pop],w)
 if target==0:return dict(status='NO_SHORT_THRESHOLD_POSITIVES-NO-POINT-CONVERSION',integer_cutoff_bound_at_least=max(x['B128'] for x in pop)+1,target_fraction=0,fixed_cutoffs={str(c):decision(pop,w,c) for c in [128,160]})
 if target==1:return dict(status='ALL_SHORT_POSITIVES-NO-POINT-CONVERSION',integer_cutoff_bound_at_most=min(x['B128'] for x in pop),target_fraction=1,fixed_cutoffs={str(c):decision(pop,w,c) for c in [128,160]})
 candidates={x['B128'] for x in pop}|{max(x['B128'] for x in pop)+1};best=min(candidates,key=lambda c:(abs(mean([x['B128']>=c for x in pop],w)-target),-c));return dict(status='DESCRIPTIVE-ORACLE-LABEL-RATE-MATCH-ONLY',rate_matched=decision(pop,w,best),absolute_fraction_error=abs(mean([x['B128']>=best for x in pop],w)-target),fixed_cutoffs={str(c):decision(pop,w,c) for c in [128,160]})
def summary(pop,override=None):
 if not pop:return dict(status='unmeasured',rows=0,maps=0)
 w=weights(pop) if override is None else override;active=[i for i,v in enumerate(w) if v>0];p=[pop[i] for i in active];w=[w[i] for i in active];n=len(p)
 sign=lambda v:'negative' if v<0 else 'positive' if v>0 else 'zero';signs={a+'->'+b:dict(raw=sum(sign(x['B32'])==a and sign(x['B128'])==b for x in p),fraction=mean([sign(x['B32'])==a and sign(x['B128'])==b for x in p],w)) for a in ['negative','zero','positive'] for b in ['negative','zero','positive']};rat=[(x['B128']/x['B32'],ww) for x,ww in zip(p,w) if x['B32']>0];bands={}
 for lo,hi in [(24,40),(16,48)]:
  band=[(x,ww) for x,ww in zip(p,w) if lo<=x['B32']<=hi];maps=len({x['group'] for x,ww in band});bands[f'{lo}_{hi}']=dict(rows=len(band),maps=maps,status='sufficient-for-tentative-description' if len(band)>=20 and maps>=2 else 'INSUFFICIENT-SUPPORT',B128=dist([x['B128'] for x,ww in band],[ww for x,ww in band]))
 rates={}
 for horizon,ticks in [('32',24),('128',120)]:
  eligible=[(x,ww) for x,ww in zip(p,w) if x['Q'+horizon]>0];rates[horizon]=dict(per_usable_tick=dist([x['B'+horizon]/ticks for x in p],w),per_query=dist([x['B'+horizon]/x['Q'+horizon] for x,ww in eligible],[ww for x,ww in eligible]),zero_query_rows=sum(x['Q'+horizon]==0 for x in p))
 both=[(x,ww) for x,ww in zip(p,w) if x['Q32']>0 and x['Q128']>0]
 rates['paired_positive_query_denominators']=dict(rows=len(both),short=dist([x['B32']/x['Q32'] for x,ww in both],[ww for x,ww in both]),full=dist([x['B128']/x['Q128'] for x,ww in both],[ww for x,ww in both]),difference=dist([x['B128']/x['Q128']-x['B32']/x['Q32'] for x,ww in both],[ww for x,ww in both]))
 rates['weighted_sum_benefit_per_weighted_sum_queries']={h:(sum(ww*x['B'+h] for x,ww in zip(p,w))/sum(ww*x['Q'+h] for x,ww in zip(p,w))) if sum(ww*x['Q'+h] for x,ww in zip(p,w)) else None for h in ['32','128']}
 return dict(status='observed',rows=n,maps=len({x['group'] for x in p}),distributions={k:dist([x[k] for x in p],w) for k in ['B32','B128','Blate','Q32','Q128','Qlate']},short_threshold_raw=sum(x['B32']>=32 for x in p),short_threshold_fraction=mean([x['B32']>=32 for x in p],w),ratios_positive_B32_only=dist([v for v,ww in rat],[ww for v,ww in rat]),undefined_ratio_rows=sum(x['B32']<=0 for x in p),late_gain_raw=sum(x['B32']<=0 and x['B128']>0 for x in p),positive_to_nonpositive_raw=sum(x['B32']>0 and x['B128']<=0 for x in p),late_cancellation_raw=sum(x['Blate']<0 for x in p),signs=signs,rates=rates,bands=bands,oracle_cutoff=oracle(p,w))
def uncertainty(pop):
 if not pop:return dict(status='unmeasured')
 groups=sorted({x['group'] for x in pop});n=len(groups)
 if n==1:return dict(status='NO-CI-ONE-MAP')
 w=weights(pop);records=[]
 for counts in itertools.product(range(n+1),repeat=n):
  if sum(counts)!=n:continue
  prob=math.factorial(n)/(n**n)
  for c in counts:prob/=math.factorial(c)
  bw=[ww*counts[groups.index(x['group'])] for x,ww in zip(pop,w)];s=summary(pop,bw);records.append(dict(counts=counts,probability=prob,mean_B128_minus_B32=s['distributions']['Blate']['mean'],conditional_B128_median_24_40=s['bands']['24_40']['B128']['median'],oracle_matched_cutoff=s['oracle_cutoff'].get('rate_matched',{}).get('cutoff_units')))
 ci={}
 for k in ['mean_B128_minus_B32','conditional_B128_median_24_40','oracle_matched_cutoff']:
  valid=[(x[k],x['probability']) for x in records if x[k] is not None];ci[k]=dict(status='defined' if valid else 'UNDEFINED-NO-SUPPORT',probability_defined=sum(p for v,p in valid),distribution=dist([v for v,p in valid],[p for v,p in valid]))
 return dict(status='EXACT-DISCRETE-ONLY3CLUSTERS',groups=groups,count_vectors=records,intervals={k:dict(status=v['status'],probability_defined=v['probability_defined'],percentile95=None if not v['distribution']['n'] else [sorted([(x[k],x['probability']) for x in records if x[k] is not None],key=lambda v:v[0])[0][0],sorted([(x[k],x['probability']) for x in records if x[k] is not None],key=lambda v:v[0])[-1][0]]) for k,v in ci.items()},note='For3maps all endpoint count vectors have probability1/27>2.5%; equal-tailed95% percentiles equal minimum/maximum of the exact weighted bootstrap distribution. Not a universal inference.')
result=dict(analysis_plan_sha256=sha(R/'PLAN.json'),analysis_lock_sha256=sha(R/'INPUTS-LOCK.json'),raw_lock_sha256=sha(R/'RAW-INPUTS-LOCK.json'),census=census,all_raw_rows=len(allrows),eligible_rows=len(rows),primary=summary(primary),primary_per_map={g:summary([x for x in primary if x['run_group']==g]) for g in mapmeta},families={name:summary([x for x in rows if x['family']==i]) for i,name in enumerate(F)},family_movement={name+'_'+mode:summary([x for x in rows if x['family']==i and x['movement_class']==mode]) for i,name in enumerate(F) for mode in ['ground','air']},primary_uncertainty=uncertainty(primary),leave_one_map_out={g:summary([x for x in primary if x['run_group']!=g]) for g in mapmeta},conclusion='Primary near32 support absent or inadequate means no empirical32-to128 conversion;160 is only the steady-rate reference. Labels do not identify old EXPECTED-model cutoff or old policy performance.')
#95% quantiles above use exact multinomial masses; also record full draw distribution.
write('RESULTS.json',result)
with (R/'paired-rows.csv').open('w') as f:
 writer=csv.DictWriter(f,fieldnames=list(allrows[0]));writer.writeheader();writer.writerows(allrows)
write('ANALYSIS-STATUS.json',dict(status='COMPLETE',rows=len(allrows),eligible_rows=len(rows),results_sha256=sha(R/'RESULTS.json'),no_fit=True,no_games=True,utc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
print(json.dumps(result['primary'],indent=2))
