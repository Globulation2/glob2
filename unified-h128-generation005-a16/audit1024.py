import pathlib,json,hashlib,sys,collections,datetime,os
P=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-004/cumulative-1024-research');sys.path.insert(0,str(P/'analysis-tool-source-A16'))
os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1');os.sched_setaffinity(0,{0});os.nice(15)
from tools import adaptive_gradient_dataset as d,adaptive_gradient_spatial as spatial,adaptive_gradient_model as model,resource_gradient_model as integer
import numpy as np
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
inputs=json.loads((P/'terminal-inputs.json').read_text());assert sha(P/'terminal-inputs.json')=='26a086c1aea2d541ec8ab89f58eabebc1dbcbb94ef9f1f530c120002e1441763'
for rel,h in inputs['files'].items():assert sha(P/rel)==h,rel
new=json.loads((P/'generic-research.json').read_text());old=json.loads((P/'incumbent768-a16.json').read_text());assert sha(P/'incumbent768-a16.json')=='495713960e3e614040e874e181995e6f5970ae7a99488d47b6465252f00db2d1'
integer.validate_heads(new,research=True,expected_threshold=16000);model.validate(old);assert new['threshold']==old['threshold']==16000
assert tuple(new['features'])==tuple(old['features'])==d.FEATURES
rows=spatial.load([P/p for p in inputs['manifests']]);eligible=d.eligible(rows);reg=json.loads((P/'map-splits.json').read_text())['assignments'];held=set(new['provenance']['heldout_maps']);dev=set(new['provenance']['development_maps']);assert not held&dev;assert all(r['group'] in reg for r in eligible)
test=[r for r in eligible if r['group'] in held];assert {r['group'] for r in eligible}==held|dev
# Registry -1 means heldout. Full historical assignments are immutable inputs.
assert all(reg[g]==-1 for g in held)
w=d.balanced_weights(test);mass=lambda key:{str(k):float(sum(v for r,v in zip(test,w) if r[key]==k)/sum(w)) for k in sorted({r[key] for r in test})}
assert all(abs(v-1/len(mass('format')))<1e-10 for v in mass('format').values())
a=integer.metrics(test,old,weighting=d.balanced_weights);b=integer.metrics(test,new,weighting=d.balanced_weights)
assert abs(b['decision_regret']-new['provenance']['holdout']['decision_regret'])<1e-10
# For subgroup paired deltas retain globally frozen weights, rather than rebalance within subgroup.
y=np.array([r['defer_extra']-r['refresh_extra'] for r in test],float)
pred={n:np.array([integer.predict(m,[r[k] for k in d.FEATURES])/1000 for r in test]) for n,m in [('incumbent',old),('new',new)]}
regret={n:np.maximum(y-16,0)*(p<16)+np.maximum(16-y,0)*(p>=16) for n,p in pred.items()}
sub={}
for key in ['format','generator','dimensions','family','age_bucket','cohort','trajectory_status']:
 groups=collections.defaultdict(list)
 for i,r in enumerate(test):
  val=('0-31' if r['age']<32 else '32-127' if r['age']<128 else '128-511' if r['age']<512 else '512-4095' if r['age']<4096 else '4096+') if key=='age_bucket' else r.get(key,'unknown');groups[str(val)].append(i)
 sub[key]={}
 for name,ids in sorted(groups.items()):
  ix=np.asarray(ids);ww=w[ix];entry=dict(episodes=len(ids),maps=len({test[i]['group'] for i in ids}),weight_mass=float(sum(ww)/sum(w)))
  for n in pred:entry[n]=dict(regret=float(np.average(regret[n][ix],weights=ww)),selected_pct=float(np.average(pred[n][ix]>=16,weights=ww)*100))
  entry['delta_regret']=entry['new']['regret']-entry['incumbent']['regret'];sub[key][name]=entry
out=dict(status='PASS-TECHNICAL-RESEARCH-ONLY',utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),input_sha256=sha(P/'terminal-inputs.json'),model_sha256=sha(P/'generic-research.json'),incumbent_sha256=sha(P/'incumbent768-a16.json'),slots=inputs['expected_slots'],terminal_counts=inputs['slot_counts'],immutable_files_verified=len(inputs['files']),rows=len(rows),eligible=len(eligible),heldout_rows=len(test),heldout_maps=len(held),development_maps=len(dev),same_rows_weights_threshold=True,threshold_milli=16000,weighting='equal format/role/cohort/map/family/collection/episode',weight_mass={k:mass(k) for k in ['format','cohort_role','family']},collection_thresholds=sorted({r['collection_threshold_milli'] for r in rows if r['collection_threshold_milli'] is not None}),incumbent=a,new=b,delta_regret=b['decision_regret']-a['decision_regret'],relative_regret_change_pct=100*(b['decision_regret']/a['decision_regret']-1),subgroups=sub,separate_outcomes=model.outcome_metrics([r for r in rows if r['group'] in held]),excluded_population=dict(unknown_capture=sum(not r['capture_known'] for r in rows),no_movement_label=sum(not r['movement_label_available'] for r in rows),cold_proxy_unknown_capture=True,planning_not_direction_target=True,stall_rates_scope='possibly-overlapping-episode-query-outcomes;not-unique-live-game-rate'),bounds_validated=True,stable_promotion=False)
(P/'PAIRED-1024-VS768-A16.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:out[k] for k in ['status','rows','eligible','heldout_rows','delta_regret','relative_regret_change_pct','incumbent','new']}))
