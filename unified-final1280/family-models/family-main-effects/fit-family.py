from pathlib import Path
import sys,os,json,hashlib,collections,datetime,time,subprocess
ROOT=Path(__file__).resolve().parent
DATA=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/cumulative-1280-research')
sys.path.insert(0,str(DATA/'analysis-tool-source-A16'))
os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1');os.sched_setaffinity(0,{1});os.nice(15)
from tools import adaptive_gradient_dataset as d,adaptive_gradient_spatial as collector,resource_gradient_model as integer
import numpy as np
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def write(name,value):
 p=ROOT/name;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(value,indent=2)+'\n');t.replace(p)
def status(phase,**kw):write('STATUS.json',dict(status=phase,pid=os.getpid(),utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**kw))
assert not (ROOT/'REPORT.json').exists(),'Never duplicate completed fit'
status('LOADING')
inputs=json.loads((DATA/'terminal-inputs.json').read_text());assert sha(DATA/'terminal-inputs.json')=='a044f373dcedcec9f3f63d2b54768124e2a75193cc5f604a57f040a5bf486a25'
assert json.loads((DATA/'fit-status.json').read_text())['status']=='COMPLETE'
baseline=json.loads((DATA/'generic-research.json').read_text());assert sha(DATA/'generic-research.json')=='3ba00451aebd7b991fcc8e700f23d342c3a30ec66c4b2398df886734c8518b87'
integer.validate_heads(baseline,research=True,expected_threshold=16000)
rows=d.eligible(collector.load([DATA/n for n in inputs['manifests']]));assert len(rows)==287045
registry=json.loads((DATA/'map-splits.json').read_text())['assignments'];held=set(baseline['provenance']['heldout_maps']);dev=[r for r in rows if r['group'] not in held];test=[r for r in rows if r['group'] in held];assert len(test)==67018 and len(held)==315
features=tuple(d.FEATURES)+tuple('family_'+n for n in d.FAMILIES);transforms=tuple(d.TRANSFORMS)+('identity_q12',)*5
for r in rows:
 for name in d.FAMILIES:r['family_'+name]=int(r['family']==name)
assert all(not r['family_markets'] for r in rows)
identity=d.canonical_hash(sorted((r['collection'],r['tick'],r['key']) for r in rows));cv=[]
for fold in range(3):
 status('FITTING-CV',fold=fold)
 train=[r for r in dev if registry[r['group']]!=fold];validation=[r for r in dev if registry[r['group']]==fold]
 model=integer.fit_heads(train,features,transforms,weighting=d.balanced_weights,threshold=16000);score=integer.metrics(validation,model,weighting=d.balanced_weights);prior=baseline['provenance']['cross_validation'][fold]['overall'];assert score['episodes']==prior['episodes'] and score['states']==prior['states'];cv.append(dict(fold=fold,baseline12=prior,augmented17=score,delta_regret=score['decision_regret']-prior['decision_regret'],model=model))
status('FITTING-FULL-DEVELOPMENT')
model=integer.fit_heads(dev,features,transforms,weighting=d.balanced_weights,threshold=16000);integer.validate_heads(model,research=True,expected_threshold=16000)
model['candidate_contract']=dict(base_contract=d.CONTRACT,extension='family-main-effects-v1',family_values=dict(enumerate(d.FAMILIES)),encoding='five raw0/1main effects; no interactions; identity integer transform',unmeasured_family='markets:constantzero,notlearned')
model['provenance']=dict(input_sha256=sha(DATA/'terminal-inputs.json'),baseline12_sha256=sha(DATA/'generic-research.json'),registry_sha256=sha(DATA/'map-splits.json'),eligible_rows=len(rows),row_identity_sha256=identity,heldout_maps=len(held),heldout_rows=len(test),development_maps=len({r['group'] for r in dev}),cross_validation=cv,scope='Researchonly;no native/deployment/sourcechanges;baseline12existingfitreused')
write('family17-research.json',model)
status('SCORING')
w=d.balanced_weights(test);y=np.array([r['defer_extra']-r['refresh_extra'] for r in test],float);pred={name:np.array([integer.predict(m,[r[k] for k in m['features']])/1000 for r in test]) for name,m in [('baseline12',baseline),('augmented17',model)]};take={name:p>=16 for name,p in pred.items()};loss={name:np.maximum(y-16,0)*(~take[name])+np.maximum(16-y,0)*take[name] for name in pred};totalw=sum(w)
def mean(x,mask=None):
 mask=np.ones(len(test),bool) if mask is None else mask
 return float(np.average(x[mask],weights=w[mask])) if mask.any() else None
def popscore(mask):
 out=dict(episodes=int(sum(mask)),maps=len({r['group'] for r,m in zip(test,mask) if m}),weight_mass=float(sum(w[mask])/totalw))
 for name,p in pred.items():out[name]=dict(regret=mean(loss[name],mask),mae=mean(abs(p-y),mask),mean_predicted=mean(p,mask),mean_actual=mean(y,mask),selected_pct=mean(take[name]*100,mask))
 out['delta_regret']=out['augmented17']['regret']-out['baseline12']['regret'];return out
subgroups={}
for key in ['family','format','generator','dimensions','age_bucket']:
 values=[('0-31' if r['age']<32 else '32-127' if r['age']<128 else '128-511' if r['age']<512 else '512-4095' if r['age']<4096 else '4096+') if key=='age_bucket' else str(r[key]) for r in test]
 subgroups[key]={v:popscore(np.array([x==v for x in values])) for v in sorted(set(values))}
calibration={}
for name,p in pred.items():
 order=np.argsort(p,kind='stable');cumulative=np.cumsum(w[order]);bins=[]
 for i in range(10):
  ix=order[(cumulative>(i/10)*totalw)&(cumulative<=((i+1)/10)*totalw)]
  if len(ix):bins.append(dict(episodes=len(ix),weight_mass=float(sum(w[ix])/totalw),prediction=float(np.average(p[ix],weights=w[ix])),actual=float(np.average(y[ix],weights=w[ix])),min_prediction=float(min(p[ix])),max_prediction=float(max(p[ix]))))
 calibration[name]=dict(weighted_signed_bias=mean(p-y),weighted_calibration_error=sum(x['weight_mass']*abs(x['prediction']-x['actual']) for x in bins),prediction_deciles=bins)
def budgetscore(p,rate):
 # Descriptive heldout ranking at an exactly equal weighted budget. Fractional
 # boundary inclusion gives equal mass; this is not a fitted deploy threshold.
 order=np.argsort(-p,kind='stable');allocated=np.zeros(len(test));remaining=totalw*rate
 for i in order:
  use=min(float(w[i]),max(remaining,0));allocated[i]=use;remaining-=use
  if remaining<=0:break
 mass=sum(allocated);return dict(selected_weight_fraction=mass/totalw,actual_benefit_per_selected_weight=float(np.sum(allocated*y)/mass) if mass else None,total_net_benefit_per_all_weight=float(np.sum(allocated*y)/totalw),positive_benefit_captured_fraction=float(np.sum(allocated*np.maximum(y,0))/np.sum(w*np.maximum(y,0))),negative_benefit_per_all_weight=float(np.sum(allocated*np.maximum(-y,0))/totalw))
rates=sorted(set([.01,.02,.05,.1,mean(take['baseline12'])]));budgets=[dict(rate=rate,**{n:budgetscore(p,rate) for n,p in pred.items()}) for rate in rates]
separate={}
for name,mask in take.items():
 separate[name]={}
 for fam in ['all',*d.FAMILIES]:
  chosen=mask & (np.ones(len(test),bool) if fam=='all' else np.array([r['family']==fam for r in test]));entry=dict(selected_episodes=int(sum(chosen)),selected_weight_mass=float(sum(w[chosen])/totalw),eligible_episodes=sum(r['family']==fam for r in test) if fam!='all' else len(test),scope='overlappingcounterfactualepisodequeryrates;notuniquelivegameoutcomes;nottrainingtarget')
  for denominator,columns in [('queries',['defer_stalls','refresh_stalls','defer_missed_moves','refresh_missed_moves','defer_false_unreachable','refresh_false_unreachable','defer_false_reachable','refresh_false_reachable']),('planning_queries',['defer_planning_false_negative','refresh_planning_false_negative','defer_planning_false_positive','refresh_planning_false_positive'])]:
   den=float(sum(weight*r.get(denominator,0) for r,weight,select in zip(test,w,chosen) if select));entry[denominator+'_weighted_denominator']=den
   for col in columns:entry[col]=float(sum(weight*r.get(col,0) for r,weight,select in zip(test,w,chosen) if select)/den) if den else None
  separate[name][fam]=entry
report=dict(status='PASS-RESEARCH-ONLY',input_sha256=sha(DATA/'terminal-inputs.json'),split_sha256=sha(DATA/'map-splits.json'),baseline12_sha256=sha(DATA/'generic-research.json'),augmented17_sha256=sha(ROOT/'family17-research.json'),eligible_rows=len(rows),heldout_rows=len(test),heldout_maps=len(held),same_rows_weights_splits_target_threshold_modelclass=True,threshold_milli=16000,feature_encoding='12generic+5raw0/1familymain-effects,nolog,nointeractions',families=dict(collections.Counter(r['family'] for r in rows)),unmeasured_market=dict(training_column_constant=0,learned=False,rows_invented=0),baseline_refit=False,cross_validation=cv,cv_mean_delta_regret=sum(x['delta_regret'] for x in cv)/3,heldout={n:integer.metrics(test,m,weighting=d.balanced_weights) for n,m in [('baseline12',baseline),('augmented17',model)]},subgroups=subgroups,calibration=calibration,matched_weighted_selection_budgets=budgets,separate_selected_outcomes=separate,weighting='equalformat/role/cohort/map/family/collection/episode',row_identity_sha256=identity,limitations=['Offlineonly,noCPUorprospectivequalityclaim','Familymain-effectcoefficientsnotportableacrossunmeasuredfamilies;Marketszero/notlearned','Noextraoutcomesfoldedintodirection-costlabel','Causalindividualfeaturevalueandgeneralizationneedprospectivequalification'],environment=dict(python=sys.version,numpy=np.__version__,affinity=sorted(os.sched_getaffinity(0))),script_sha256=sha(Path(__file__)))
write('REPORT.json',report);status('COMPLETE',report_sha256=sha(ROOT/'REPORT.json'),model_sha256=sha(ROOT/'family17-research.json'));print(json.dumps(dict(cv_mean_delta_regret=report['cv_mean_delta_regret'],heldout=report['heldout'])))
