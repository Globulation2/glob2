from pathlib import Path
import sys,json,collections,hashlib,os
P=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/cumulative-1280-research');F=P.parent.parent/'final1280-family-main-effects';sys.path.insert(0,str(P/'analysis-tool-source-A16'));os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1')
from tools import adaptive_gradient_spatial as collector,adaptive_gradient_dataset as d,resource_gradient_model as integer
from tools.resource_gradient_dataset import manifest_records
import numpy as np
inputs=json.loads((P/'terminal-inputs.json').read_text());rosters={}
for name in inputs['manifests']:
 records,_=manifest_records(P/name)
 for r in records:rosters['|'.join((r['group'],r['binary_sha256'],r['episode_sha256']))]=r['spec'].get('ais') or [r['spec']['ai']]*r['spec']['players']
allrows=d.eligible(collector.load([P/n for n in inputs['manifests']]));baseline=json.loads((P/'generic-research.json').read_text());aug=json.loads((F/'family17-research.json').read_text());held=set(baseline['provenance']['heldout_maps']);test=[r for r in allrows if r['group'] in held];w=d.balanced_weights(test);guard=[]
for r,weight in zip(test,w):
 if r['family']!='guard':continue
 r=dict(r);r['ai']=rosters[r['collection']][r['team']];r['weight']=float(weight)
 r['current_goals_inferred']=r['captured_goal_count']+r['current_target_added_epoch']-r['capture_target_added_epoch']-r['current_target_removed_epoch']+r['capture_target_removed_epoch']
 for f in d.FAMILIES:r['family_'+f]=int(r['family']==f)
 r['score12']=integer.predict(baseline,[r[k] for k in baseline['features']])/1000;r['score17']=integer.predict(aug,[r[k] for k in aug['features']])/1000;guard.append(r)
def report(sub):
 weight=sum(r['weight'] for r in sub);y=lambda r:r['defer_extra']-r['refresh_extra'];avg=lambda fn:sum(r['weight']*fn(r) for r in sub)/weight if weight else None
 out=dict(rows=len(sub),maps=len({r['group'] for r in sub}),episode_queries=sum(r['queries'] for r in sub),positive_benefit_rows=sum(y(r)>0 for r in sub),negative_benefit_rows=sum(y(r)<0 for r in sub),zero_benefit_rows=sum(y(r)==0 for r in sub),benefit_ge16_rows=sum(y(r)>=16 for r in sub),mean_benefit=avg(y),weighted_positive_pct=avg(lambda r:100*(y(r)>0)),weighted_benefit_ge16_pct=avg(lambda r:100*(y(r)>=16)),captured_goals_min=min((r['captured_goal_count'] for r in sub),default=None),captured_goals_max=max((r['captured_goal_count'] for r in sub),default=None),current_goal_inference_min=min((r['current_goals_inferred'] for r in sub),default=None))
 for name in ['12','17']:
  out['model'+name]=dict(selected_rows=sum(r['score'+name]>=16 for r in sub),selected_weighted_pct=avg(lambda r:100*(r['score'+name]>=16)),regret=avg(lambda r:max(y(r)-16,0)*(r['score'+name]<16)+max(16-y(r),0)*(r['score'+name]>=16)))
 for k in ['target_added','target_removed','route_changes','seed_cost_changes','guard_balance_queries','defer_false_unreachable','refresh_false_unreachable','defer_missed_moves','refresh_missed_moves','defer_stalls','refresh_stalls']:out[k+'_sum']=sum(r.get(k,0) for r in sub)
 return out
strata={}
for name,predicate in [('all',lambda r:True),('captured_goals_zero',lambda r:r['captured_goal_count']==0),('captured_goals_positive',lambda r:r['captured_goal_count']>0),('captured_zero_current_zero',lambda r:r['captured_goal_count']==0 and r['current_goals_inferred']==0),('captured_zero_current_positive',lambda r:r['captured_goal_count']==0 and r['current_goals_inferred']>0),('captured_positive_current_zero',lambda r:r['captured_goal_count']>0 and r['current_goals_inferred']==0),('captured_positive_current_positive',lambda r:r['captured_goal_count']>0 and r['current_goals_inferred']>0)]:strata[name]=report([r for r in guard if predicate(r)])
ai={name:{'all':report([r for r in guard if r['ai']==name]),'captured_zero':report([r for r in guard if r['ai']==name and not r['captured_goal_count']]),'captured_positive':report([r for r in guard if r['ai']==name and r['captured_goal_count']>0])} for name in sorted({r['ai'] for r in guard})}
out=dict(status='PASS-ARTIFACT-ONLY',input_sha256=hashlib.file_digest((P/'terminal-inputs.json').open('rb'),'sha256').hexdigest(),heldout_guard_rows=len(guard),same_globalheldoutweights=True,current_goals='DerivedcaptureGoalCount+rawaddedEpochDelta-rawremovedEpochDelta;notworldscan. Negativevaluesreportedasdiagnosticifany.',negative_inferred_goal_rows=sum(r['current_goals_inferred']<0 for r in guard),strata=strata,exact_seat_ai=ai,limitations=['Futureepisodequerycountspossiblyoverlapping,notuniqueactualmovementqueries','AIidentityattributeisfieldowningteamseat,notproofthatseatpaintedareaorownsallqueryingalliedunits','Capturegoalcountnotcurrentlyselectedfeature;analysisdoesnotaddfeature','Direction-costlabelstillmissescachedunreachablemissedmoves'])
Path(__file__).with_name('GUARD-GOAL-SUPPORT.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(strata))
