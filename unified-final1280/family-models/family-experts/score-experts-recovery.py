from pathlib import Path
import os,sys,json,hashlib,collections,datetime
ROOT=Path(__file__).resolve().parent;DATA=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/cumulative-1280-research');FAMILY=DATA.parent.parent/'final1280-family-main-effects'
sys.path.insert(0,str(DATA/'analysis-tool-source-A16'));os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1');os.sched_setaffinity(0,{1});os.nice(15)
from tools import adaptive_gradient_dataset as d,adaptive_gradient_spatial as collector,resource_gradient_model as integer
import numpy as np
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def write(name,value):
 p=ROOT/name;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(value,indent=2,default=lambda value:value.item())+'\n');t.replace(p)
def status(phase,**kw):write('STATUS.json',dict(status=phase,pid=os.getpid(),utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**kw))
assert not (ROOT/'REPORT.json').exists();status('LOADING')
inputs=json.loads((DATA/'terminal-inputs.json').read_text());assert sha(DATA/'terminal-inputs.json')=='a044f373dcedcec9f3f63d2b54768124e2a75193cc5f604a57f040a5bf486a25';baseline=json.loads((DATA/'generic-research.json').read_text());onehot=json.loads((FAMILY/'family17-research.json').read_text());assert sha(DATA/'generic-research.json')=='3ba00451aebd7b991fcc8e700f23d342c3a30ec66c4b2398df886734c8518b87';assert sha(FAMILY/'family17-research.json')=='cda26329e4882c7f0d1d88dd7b7429acb2ee6c3e11491f27e6012191721f0a61'
rows=d.eligible(collector.load([DATA/p for p in inputs['manifests']]));assert len(rows)==287045;reg=json.loads((DATA/'map-splits.json').read_text())['assignments'];held=set(baseline['provenance']['heldout_maps']);dev=[r for r in rows if r['group'] not in held];test=[r for r in rows if r['group'] in held];assert len(test)==67018
families=sorted({r['family'] for r in rows});assert 'markets' not in families
for r in rows:
 for f in d.FAMILIES:r['family_'+f]=int(r['family']==f)
def support(pop):
 return {f:dict(rows=len(sub),maps=len({r['group'] for r in sub}),positive=sum(r['defer_extra']>r['refresh_extra'] for r in sub),negative=sum(r['defer_extra']<r['refresh_extra'] for r in sub),zero=sum(r['defer_extra']==r['refresh_extra'] for r in sub)) for f in families for sub in [[r for r in pop if r['family']==f]]}
def fit_group(pop,stage):
 globalw=d.balanced_weights(pop);lookup={id(r):float(w) for r,w in zip(pop,globalw)};models={};mass={}
 for f in families:
  status('FITTING',stage=stage,family=f);sub=[r for r in pop if r['family']==f]
  def weights(actual):
   w=np.asarray([lookup[id(r)] for r in actual]);return w*len(w)/sum(w)
  models[f]=integer.fit_heads(sub,d.FEATURES,d.TRANSFORMS,weighting=weights,threshold=16000);integer.validate_heads(models[f],research=True,expected_threshold=16000);mass[f]=dict(original_global_weight=float(sum(lookup[id(r)] for r in sub)),normalization_scalar=len(sub)/sum(lookup[id(r)] for r in sub))
 return models,mass

def predictions(pop,model=None,experts=None):return np.array([integer.predict(experts[r['family']] if experts is not None else model,[r[k] for k in (experts[r['family']] if experts is not None else model)['features']])/1000 for r in pop])
def score(pop,p,forced=False):
 w=d.balanced_weights(pop);y=np.array([r['defer_extra']-r['refresh_extra'] for r in pop],float);take=p>=16
 if forced:take|=np.array([r['age']>=1024 for r in pop])
 regret=np.maximum(y-16,0)*(~take)+np.maximum(16-y,0)*take;mean=lambda x:float(np.average(x,weights=w));positive=np.maximum(y,0)
 def quant(v,q):
  ix=np.argsort(v,kind='stable');cw=np.cumsum(w[ix]);return float(v[ix[min(np.searchsorted(cw,q*cw[-1]),len(v)-1)]])
 return dict(episodes=len(pop),maps=len({r['group'] for r in pop}),mae=mean(abs(p-y)),mse=mean((p-y)**2),decision_regret=mean(regret),decision_regret_p95=quant(regret,.95),decision_regret_p99=quant(regret,.99),selected_pct=mean(take)*100,positive_benefit_captured_fraction=float(sum(w*positive*take)/sum(w*positive)),selected_actual_benefit=float(np.average(y[take],weights=w[take])) if take.any() else None,false_refresh_pct=mean(take&(y<16))*100,missed_threshold_decision_pct=mean((~take)&(y>=16))*100)
saved=json.loads((ROOT/'family-experts-research.json').read_text());models=saved['experts'];cv=saved['cross_validation'];mass=saved['weight_normalization']
status('SCORING');w=d.balanced_weights(test);totalw=sum(w);y=np.array([r['defer_extra']-r['refresh_extra'] for r in test],float);pred={'baseline12':predictions(test,baseline),'onehot17':predictions(test,onehot),'experts12':predictions(test,experts=models)};cap=np.array([r['age']>=1024 for r in test]);take={n:p>=16 for n,p in pred.items()};policies={n:flags for n,flags in take.items()};policies.update({n+'_conditional_cap1024':flags|cap for n,flags in take.items()})
mean=lambda a,mask=None:float(np.average(a if mask is None else a[mask],weights=w if mask is None else w[mask])) if mask is None or mask.any() else None
subgroups={}
for key in ['family','format','generator','dimensions']:
 values=[str(r[key]) for r in test];subgroups[key]={}
 for v in sorted(set(values)):
  mask=np.array([x==v for x in values]);subgroups[key][v]=dict(rows=int(sum(mask)),weight_mass=float(sum(w[mask])/totalw),models={n:dict(regret=mean(np.maximum(y-16,0)*(~flags)+np.maximum(16-y,0)*flags,mask),selected_pct=mean(flags*100,mask)) for n,flags in policies.items()})
calibration={}
for n,p in pred.items():
 ix=np.argsort(p,kind='stable');cw=np.cumsum(w[ix]);bins=[];bucket=np.minimum((10*(cw-.5*w[ix])/totalw).astype(int),9)
 for k in range(10):
  q=ix[bucket==k]
  if len(q):bins.append(dict(rows=len(q),weight_mass=float(sum(w[q])/totalw),predicted=float(np.average(p[q],weights=w[q])),actual=float(np.average(y[q],weights=w[q]))))
 calibration[n]=dict(signed_bias=mean(p-y),decile_error=sum(x['weight_mass']*abs(x['predicted']-x['actual']) for x in bins),bins=bins)
def rank(p,rate,mandatory=None):
 mandatory=np.zeros(len(test),bool) if mandatory is None else mandatory;allocation=np.where(mandatory,w,0);need=totalw*rate-sum(allocation)
 if need<0:return dict(feasible=False,mandatory_weight_fraction=float(sum(allocation)/totalw))
 for i in np.argsort(-p,kind='stable'):
  if mandatory[i]:continue
  use=min(w[i],max(need,0));allocation[i]=use;need-=use
  if need<=0:break
 return dict(feasible=True,selected_weight_fraction=float(sum(allocation)/totalw),net_benefit_per_all_weight=float(sum(allocation*y)/totalw),positive_benefit_captured_fraction=float(sum(allocation*np.maximum(y,0))/sum(w*np.maximum(y,0))),negative_benefit_per_all_weight=float(sum(allocation*np.maximum(-y,0))/totalw))
budgets=[dict(rate=rate,models={n:rank(p,rate) for n,p in pred.items()},conditional_cap1024_models={n:rank(p,rate,cap) for n,p in pred.items()}) for rate in sorted(set([.01,.02,.05,.1,mean(take['baseline12'])]))]
separate={}
for name,flags in policies.items():
 separate[name]={}
 for f in ['all',*d.FAMILIES]:
  select=flags&(np.ones(len(test),bool) if f=='all' else np.array([r['family']==f for r in test]));out=dict(selected_rows=int(sum(select)),selected_weight_fraction=float(sum(w[select])/totalw),measured_family=f!='markets',scope='overlappingcounterfactualepisodequeryrates;notuniqueliveoutcomes')
  for denominator,columns in [('queries',['defer_stalls','refresh_stalls','defer_missed_moves','refresh_missed_moves','defer_false_unreachable','refresh_false_unreachable','defer_false_reachable','refresh_false_reachable']),('planning_queries',['defer_planning_false_negative','refresh_planning_false_negative','defer_planning_false_positive','refresh_planning_false_positive'])]:
   den=float(sum(wt*r.get(denominator,0) for r,wt,chosen in zip(test,w,select) if chosen));out[denominator+'_weighted_denominator']=den
   for col in columns:out[col]=float(sum(wt*r.get(col,0) for r,wt,chosen in zip(test,w,select) if chosen)/den) if den else None
  separate[name][f]=out
capreport=dict(age_threshold=1024,minimum8_publication8_unchanged=True,age1024_rows=int(sum(cap)),age1024_weighted_pct=mean(cap)*100,age1024_future_query_rows=sum(bool(c) and r['queries']>0 for r,c in zip(test,cap)),models={n:dict(newly_forced_rows=int(sum(cap&~flags)),newly_forced_weighted_pct=mean(cap&~flags)*100) for n,flags in take.items()},actual_current_tick_demand_column_available=False,interpretation='Allcapmetricsconditionalonactualdemand. CSVhasrecentq32/q128andfuturequeriesbutnotcurrenttickdemand,soexactactualadmissioncountcannotbeinferred. Forcedtakesassumeademandeddecisiononthesameagedrow,notidlefiller.',by_family={f:dict(rows=sum(r['family']==f and c for r,c in zip(test,cap)),weighted_mass=float(sum(wt for r,c,wt in zip(test,cap,w) if r['family']==f and c)/totalw)) for f in d.FAMILIES})
heldout={n:score(test,p) for n,p in pred.items()};heldout_cap={n:score(test,p,True) for n,p in pred.items()};assert abs(heldout['baseline12']['decision_regret']-baseline['provenance']['holdout']['decision_regret'])<1e-10
report=dict(status='PASS-RESEARCH-ONLY',input_sha256=sha(DATA/'terminal-inputs.json'),registry_sha256=sha(DATA/'map-splits.json'),baseline12_sha256=sha(DATA/'generic-research.json'),onehot17_sha256=sha(FAMILY/'family17-research.json'),experts_sha256=sha(ROOT/'family-experts-research.json'),row_identity_sha256=d.canonical_hash(sorted((r['collection'],r['tick'],r['key']) for r in rows)),eligible_rows=len(rows),heldout_rows=len(test),heldout_maps=len(held),same_rows_globalweights_splits_target=True,normalization='Preserveoriginalglobaltrainingrelativeweightswithineachfamily,thenmultiplysamescalarsofamilymeanweight1;neverrecomputeformatcohortmapweightswithinfamily',class_support=support(rows),markets=dict(rows=0,expert=None,learned=False,no_imputation=True),cross_validation=cv,heldout=heldout,heldout_conditional_cap1024=heldout_cap,cap1024=capreport,calibration=calibration,subgroups=subgroups,matched_weighted_budgets=budgets,separate_selected_outcomes=separate,source_script_sha256=sha(__file__),research_only=True,native_changes=False,baseline_and_onehot_refit=False)
write('REPORT.json',report);status('COMPLETE',report_sha256=sha(ROOT/'REPORT.json'),experts_sha256=sha(ROOT/'family-experts-research.json'));print(json.dumps(dict(heldout=heldout,cap=heldout_cap,cap1024=capreport)))
