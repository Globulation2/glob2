"""Artifact-only predeclared DEV-CV research. No production model/header output."""
import os
os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1',NUMEXPR_NUM_THREADS='1')
import pathlib,json,hashlib,time,datetime,sys,fcntl,resource,signal,traceback,collections
B=pathlib.Path(__file__).resolve().parent;P=json.loads((B/'PLAN.json').read_text());D=pathlib.Path(P['remote_data_root']);S=D/'analysis-tool-source'
lock=(B/'research.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
os.sched_setaffinity(0,set(P['budget']['cpu_ids']));os.nice(max(0,19-os.getpriority(os.PRIO_PROCESS,0)))
resource.setrlimit(resource.RLIMIT_CPU,(P['budget']['cpu_seconds_limit'],P['budget']['cpu_seconds_limit']+5))
def budget_hit(*a):raise TimeoutError('Research CPU budget reached; no retry or heldout-driven tuning')
signal.signal(signal.SIGXCPU,budget_hit)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(name,v):
 p=B/name;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n');t.replace(p)
state={'status':'verifying','pid':os.getpid(),'started_utc':now(),'plan_sha256':sha(B/'PLAN.json'),'completed_cv':[],'heldout_scored':False};save('STATUS.json',state);started=time.monotonic();cpu_start=time.process_time()
try:
 assert sha(D/'terminal-inputs.json')==P['terminal_inputs_sha256']
 for f,h in P['analysis_source_sha256'].items():assert sha(S/'tools'/f)==h,f
 for f,h in P['reference_model_hashes'].items():assert sha(B/f)==h,f
 assert sha(D/'map-splits.json')==P['data']['split_registry_sha256']
 sys.path.insert(0,str(S))
 import numpy as np,sklearn,scipy
 from sklearn.linear_model import Ridge
 from sklearn.tree import DecisionTreeRegressor
 from sklearn.ensemble import RandomForestRegressor,GradientBoostingRegressor
 from tools import adaptive_gradient_dataset as data,adaptive_gradient_spatial as spatial,resource_gradient_model as integer
 inputs=json.loads((D/'terminal-inputs.json').read_text());manifests=[D/p for p in inputs['manifests']]
 for m in manifests:assert sha(m)==inputs['files'][str(m.relative_to(D))]
 raw=spatial.load(manifests);rows=data.eligible(raw)
 assert len(raw)==P['data']['expected_total_rows']and len(rows)==P['data']['expected_eligible_rows']
 assignments=json.loads((D/'map-splits.json').read_text())['assignments'];assert all(r['group']in assignments for r in rows)
 dev_idx=np.array([i for i,r in enumerate(rows)if assignments[r['group']]!=-1]);held_idx=np.array([i for i,r in enumerate(rows)if assignments[r['group']]==-1]);dev=[rows[i]for i in dev_idx]
 assert len({r['group']for r in dev})==193 and len({rows[i]['group']for i in held_idx})==63 and len(held_idx)==13915
 y=np.array([r['defer_extra']-r['refresh_extra']for r in rows],dtype=np.float64)
 x=np.array([[integer.log_integer(r[k])if t=='log1p_q12'else max(0,min(1000000,int(r[k])))for k,t in zip(data.FEATURES,data.TRANSFORMS)]for r in rows],dtype=np.int64)
 extra=np.column_stack([x[:,2]*x[:,3]//4096,x[:,2]*x[:,4]//4096,x[:,2]*x[:,10]//4096,x[:,2]*x[:,11]//4096]);extra=np.minimum(extra,1000000)
 extras=['ix_q128_added','ix_q128_removed','ix_q128_invalid','ix_q128_nomove']
 for r,values in zip(rows,extra):r.update(zip(extras,map(int,values)))
 allx=np.column_stack([x,extra]);names=list(data.FEATURES)+extras;transforms=list(data.TRANSFORMS)+['identity_q12']*4
 assert all(k not in names for k in ['family','material','map','format','family_id','group','cohort','generator'])
 def cols(c):return list(range(12))+({'none':[],'turnover':[12,13],'contradictions':[14,15],'both':[12,13,14,15]}[c.get('interaction','none')])
 def trunc(v,d):return np.where(v>=0,v//d,-((-v)//d))
 def headpred(m,X):
  heads={}
  for k in integer.HEADS:
   h=m[k];assert h['accumulator_bound']<2**61
   heads[k]=np.clip(trunc(X@np.asarray(h['coef'],dtype=np.int64)+h['bias'],1<<h['shift']),-10**9,10**9)
  table=np.asarray(integer.SIG,dtype=np.int64)
  def sig(v):
   n=np.clip(v,-12000,12000)+12000;i=np.minimum(n//50,479);return table[i]+(table[i+1]-table[i])*(n-i*50)//50
  v=sig(heads['positive_probability'])*np.maximum(0,heads['positive_mean'])-sig(heads['negative_probability'])*np.maximum(0,heads['negative_mean'])
  return np.clip(trunc(v,65536),-10**9,10**9)
 def tree_ir(tree):
  t=tree.tree_;leaf=np.rint(t.value.reshape(-1)*1000).astype(np.int64);assert max(abs(leaf))<2**60
  return dict(left=t.children_left,right=t.children_right,feature=t.feature,threshold=np.floor(t.threshold).astype(np.int64),leaf=leaf,nodes=t.node_count,depth=t.max_depth)
 def treepred(t,X):
  node=np.zeros(len(X),dtype=np.int64)
  for _ in range(t['depth']+1):
   active=t['left'][node]>=0
   if not active.any():break
   at=node[active];node[active]=np.where(X[active,t['feature'][at]]<=t['threshold'][at],t['left'][at],t['right'][at])
  assert not (t['left'][node]>=0).any();return t['leaf'][node]
 def fit(c,train):
  ix=cols(c);X=allx[train][:,ix];rr=[rows[i]for i in train];w=data.balanced_weights(rr);target=y[train];kind=c['class'];meta={'kind':kind,'features':len(ix),'training_rows':len(train),'training_maps':len({r['group']for r in rr})}
  if kind in ['hurdle','hurdle_interactions']:
   m=integer.fit_heads(rr,[names[i]for i in ix],[transforms[i]for i in ix],weighting=data.balanced_weights,threshold=128000)
   meta['accumulator_bound']=max(m[k]['accumulator_bound']for k in integer.HEADS)
   def predict(which):
    values=headpred(m,allx[which][:,ix]);sample=np.linspace(0,len(which)-1,min(64,len(which)),dtype=int)
    for j in sample:assert values[j]==integer.predict(m,[rows[which[j]][names[k]]for k in ix])
    return values
   return predict,None,meta
  if kind=='ridge':
   mu=np.average(X,axis=0,weights=w);sd=np.sqrt(np.average((X-mu)**2,axis=0,weights=w));sd[sd<1e-12]=1
   est=Ridge(alpha=c['alpha']).fit((X-mu)/sd,target,sample_weight=w);beta=est.coef_/sd;intercept=float(est.intercept_)-float(beta@mu)
   bounds=[integer.log_integer(1000000)if transforms[i]=='log1p_q12'else 1000000 for i in ix]
   for shift in [28,24,20,16,12,8]:
    scale=1000*(1<<shift);coef=[round(float(v)*scale)for v in beta];bias=round(intercept*scale);bound=abs(bias)+sum(abs(v)*b for v,b in zip(coef,bounds))
    if bound<2**61:break
   else:raise ValueError('No safe direct Ridge accumulator')
   meta.update(accumulator_bound=bound,shift=shift)
   return lambda which:np.clip(trunc(allx[which][:,ix]@np.asarray(coef,dtype=np.int64)+bias,1<<shift),-10**9,10**9),lambda which:np.clip(est.predict((allx[which][:,ix]-mu)/sd)*1000,-10**9,10**9),meta
  common=dict(max_depth=c['depth'],min_weight_fraction_leaf=c['leaf_weight'],random_state=713)
  if kind=='cart':est=DecisionTreeRegressor(**common)
  elif kind=='forest':est=RandomForestRegressor(n_estimators=c['trees'],n_jobs=1,max_features=1.0,bootstrap=True,**common)
  elif kind=='boost':est=GradientBoostingRegressor(n_estimators=c['trees'],learning_rate=.1,loss='squared_error',**common)
  else:raise ValueError(kind)
  est.fit(X,target,sample_weight=w)
  trees=[tree_ir(est)]if kind=='cart'else[tree_ir(t)for t in (est.estimators_.reshape(-1)if kind=='boost'else est.estimators_)]
  bound=sum(int(np.max(np.abs(t['leaf'])))for t in trees);assert bound<2**61
  meta.update(nodes=sum(t['nodes']for t in trees),trees=len(trees),integer_sum_bound=bound,maximum_depth=max(t['depth']for t in trees))
  init=round(float(est.init_.constant_.reshape(-1)[0])*1000)if kind=='boost'else 0
  def predict(which):
   Xv=allx[which][:,ix];values=sum((treepred(t,Xv)for t in trees),np.zeros(len(which),dtype=np.int64))
   if kind=='forest':values=trunc(values,len(trees))
   elif kind=='boost':values=init+trunc(values,10)
   return np.clip(values,-10**9,10**9)
  return predict,lambda which:np.clip(est.predict(allx[which][:,ix])*1000,-10**9,10**9),meta
 def metrics(ix,pred):
  rr=[rows[i]for i in ix];w=data.balanced_weights(rr);yy=y[ix];p=np.asarray(pred,dtype=float)/1000;take=p>=128;ideal=yy>=128;loss=np.maximum(yy-128,0)*(~take)+np.maximum(128-yy,0)*take
  mean=lambda v:float(np.average(v,weights=w));bins=np.array([-1e9,0,8,32,64,128,256,512,1e9]);cal=[];ece=0
  for lo,hi in zip(bins[:-1],bins[1:]):
   mask=(p>=lo)&(p<hi)
   if mask.any():
    mass=float(w[mask].sum()/w.sum());mp=float(np.average(p[mask],weights=w[mask]));my=float(np.average(yy[mask],weights=w[mask]));ece+=mass*abs(mp-my);cal.append({'lower':float(lo),'upper':float(hi),'rows':int(mask.sum()),'weighted_mass':mass,'predicted_mean':mp,'actual_mean':my})
  return {'rows':len(ix),'maps':len({r['group']for r in rr}),'decision_regret':mean(loss),'selected_pct':mean(take)*100,'actual_threshold_pct':mean(ideal)*100,'raw_actual_threshold_rows':int(ideal.sum()),'selected_actual_benefit':float(np.average(yy[take],weights=w[take]))if take.any()else None,'false_refresh_pct':mean(take&~ideal)*100,'missed_threshold_pct':mean(~take&ideal)*100,'mae':mean(abs(p-yy)),'rmse':mean((p-yy)**2)**.5,'calibration_ece':ece,'calibration_bins':cal}
 def complexity(c):
  if c['class']=='ridge':return 13
  if c['class']=='cart':return 2**(c['depth']+1)-1
  if c['class']=='forest':return c['trees']*(2**(c['depth']+1)-1)
  if c['class']=='boost':return c['trees']*3
  return 4*(len(cols(c))+1)
 def slices(ix,pred):
  output={}
  for key in ['family','format','age_bucket','demand_bucket']:
   groups=collections.defaultdict(list)
   for j,i in enumerate(ix):
    r=rows[i]
    value=('0-31'if r['age']<32 else '32-127'if r['age']<128 else '128-511'if r['age']<512 else '512-4095'if r['age']<4096 else '4096+')if key=='age_bucket'else ('0-7'if r['q128']<8 else '8-31'if r['q128']<32 else '32+')if key=='demand_bucket'else r[key]
    groups[value].append(j)
   output[key]={str(g):metrics(ix[np.asarray(js)],np.asarray(pred)[js])for g,js in groups.items()}
  return output
 save('INPUT-PROOF.json',{'plan_sha256':sha(B/'PLAN.json'),'terminal_inputs_sha256':sha(D/'terminal-inputs.json'),'manifests':{str(p):sha(p)for p in manifests},'registry_sha256':sha(D/'map-splits.json'),'rows':len(rows),'dev_rows':len(dev_idx),'heldout_rows':len(held_idx),'dev_maps':193,'heldout_maps':63,'dependencies':{'numpy':np.__version__,'sklearn':sklearn.__version__,'scipy':scipy.__version__},'python':sys.version,'affinity':sorted(os.sched_getaffinity(0)),'nice':os.getpriority(os.PRIO_PROCESS,0),'source_sha256':P['analysis_source_sha256']})
 state['status']='development_cv';save('STATUS.json',state);cv=[];predictions={}
 for c in P['candidates']:
  pred=np.zeros(len(dev_idx),dtype=np.int64);folds=[]
  for fold in range(3):
   train=np.array([i for i in dev_idx if assignments[rows[i]['group']]!=fold]);positions=np.array([j for j,i in enumerate(dev_idx)if assignments[rows[i]['group']]==fold]);validation=dev_idx[positions]
   predictor,floating,meta=fit(c,train);values=predictor(validation);pred[positions]=values
   if floating is not None:
    fp=floating(validation);meta['float_integer_decision_flips']=int(np.sum((fp>=128000)!=(values>=128000)));meta['float_integer_max_abs_milli']=float(np.max(np.abs(fp-values)))
   folds.append({'fold':fold,'metrics':metrics(validation,values),'representation':meta})
  score=metrics(dev_idx,pred);entry={'candidate':c,'oof':score,'folds':folds,'complexity':complexity(c)};cv.append(entry);predictions[c['id']]=pred
  state['completed_cv'].append({'id':c['id'],'decision_regret':score['decision_regret'],'selected_pct':score['selected_pct']});save('CV-PARTIAL.json',cv);save('STATUS.json',state);print(c['id'],score['decision_regret'],score['selected_pct'],flush=True)
 def rank(v):return(round(v['oof']['decision_regret'],12),v['oof']['calibration_ece'],v['complexity'],v['candidate']['id'])
 best=min(cv,key=rank);class_winners={kind:min([v for v in cv if v['candidate']['class']==kind],key=rank)['candidate']['id']for kind in sorted({c['class']for c in P['candidates']})}
 selected=sorted(set(class_winners.values())|{'hurdle12',best['candidate']['id']});selection={'global_winner':best['candidate']['id'],'class_winners':class_winners,'heldout_evaluation_set':selected,'ranking':'OOF weighted decision_regret rounded12decimals, calibration ECE, complexity,id','plan_sha256':sha(B/'PLAN.json'),'frozen_before_heldout_scoring':True,'cv':cv,'never_refresh_oof':metrics(dev_idx,np.zeros(len(dev_idx),dtype=np.int64))};save('DEVELOPMENT-SELECTION.json',selection);selection_sha=sha(B/'DEVELOPMENT-SELECTION.json');np.savez_compressed(B/'oof-predictions.npz',**predictions)
 state.update(status='heldout_once',development_selection_sha256=selection_sha);save('STATUS.json',state)
 held_reports={};held_predictions={}
 for id in selected:
  c=next(c for c in P['candidates']if c['id']==id);predictor,floating,meta=fit(c,dev_idx);pred=predictor(held_idx);held_predictions[id]=pred;held_reports[id]={'metrics':metrics(held_idx,pred),'slices':slices(held_idx,pred),'representation':meta}
 for id,f in [('frozen_incumbent','incumbent-gen001.json'),('frozen_gen002','reference-gen002.json')]:
  m=json.loads((B/f).read_text());pred=headpred(m,x[held_idx]);held_predictions[id]=pred;held_reports[id]={'metrics':metrics(held_idx,pred),'slices':slices(held_idx,pred),'model_sha256':sha(B/f),'not_used_for_cv_selection':True}
 assert np.array_equal(held_predictions['hurdle12'],held_predictions['frozen_gen002']),'fresh baseline differs from exact existing generic fit'
 assert abs(held_reports['frozen_gen002']['metrics']['decision_regret']-2.249757901424048)<1e-9
 assert abs(held_reports['frozen_incumbent']['metrics']['decision_regret']-2.083107)<1e-5
 zero=np.zeros(len(held_idx),dtype=np.int64);held_predictions['never_refresh']=zero;held_reports['never_refresh']={'metrics':metrics(held_idx,zero),'slices':slices(held_idx,zero)}
 # Map-cluster bootstrap, not independent-episode uncertainty. No reselection.
 hr=[rows[i]for i in held_idx];w=data.balanced_weights(hr);yy=y[held_idx];maps=sorted({r['group']for r in hr});mi=np.array([maps.index(r['group'])for r in hr]);wm=np.bincount(mi,weights=w);rng=np.random.default_rng(713);counts=rng.multinomial(len(maps),np.full(len(maps),1/len(maps)),size=1000);den=counts@wm
 def loss(pred):take=pred>=128000;return np.maximum(yy-128,0)*(~take)+np.maximum(128-yy,0)*take
 boot={}
 for id,pred in held_predictions.items():
  boot[id]={}
  for ref in ['never_refresh','frozen_incumbent','hurdle12']:
   dm=np.bincount(mi,weights=w*(loss(pred)-loss(held_predictions[ref])));v=(counts@dm)/den;boot[id][ref]={'regret_difference':float(np.sum(dm)/np.sum(w)),'map_cluster_95percent_interval':list(map(float,np.quantile(v,[.025,.975])))}
 # Exact-feature collisions diagnose missing context, not a Bayes-error proof.
 _,inverse=np.unique(x[dev_idx],axis=0,return_inverse=True);tail=y[dev_idx]>=128;positive=np.bincount(inverse,weights=tail);negative=np.bincount(inverse,weights=~tail);conflict=(positive>0)&(negative>0);dup=np.bincount(inverse)>1
 collision={'unique_current12_feature_vectors':len(positive),'development_rows':len(dev_idx),'rows_in_duplicate_feature_groups':int(np.sum(dup[inverse])),'rows_in_groups_with_both_sides_of128':int(np.sum(conflict[inverse])),'conflicting_feature_groups':int(np.sum(conflict)),'scope':'empirical identical transformed inputs with differing future outcomes; not proof of intrinsic irreducibility'}
 np.savez_compressed(B/'heldout-predictions.npz',**held_predictions)
 assert sha(B/'DEVELOPMENT-SELECTION.json')==selection_sha
 result={'status':'complete-research-only','plan_sha256':sha(B/'PLAN.json'),'selection_sha256':selection_sha,'global_development_winner':best['candidate']['id'],'class_winners':class_winners,'heldout_once':held_reports,'paired_map_bootstrap':boot,'development_feature_collision':collision,'scope':'all models selected on3DEVmapfolds only; heldout scored once after frozen selection. No model/header emission or replacement.','limitations':P['limitations'],'cpu_seconds':time.process_time()-cpu_start,'wall_seconds':time.monotonic()-started,'finished_utc':now()};save('RESULTS.json',result);state.update(status='complete',heldout_scored=True,cpu_seconds=result['cpu_seconds'],wall_seconds=result['wall_seconds'],finished_utc=now());save('STATUS.json',state)
except BaseException as e:
 state.update(status='failed',error=repr(e),traceback=traceback.format_exc(),cpu_seconds=time.process_time()-cpu_start,wall_seconds=time.monotonic()-started,finished_utc=now());save('STATUS.json',state);raise
