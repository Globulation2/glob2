#!/usr/bin/env python3
"""Read-only final optimization evidence report; never starts engine/browser/build tools."""
from pathlib import Path
import argparse,datetime,hashlib,html,importlib.util,json,math,os,statistics,xml.etree.ElementTree as ET
HERE=Path(__file__).resolve().parent;ART=HERE.parent;ROOT=ART.parents[1]
M={'cpu_s':'Whole process CPU','wall_s':'End-to-end wall','peak_rss_bytes':'Process peak RSS','simulation_cpu_s':'Engine interval CPU','simulation_wall_s':'Engine run wall','cpu_per_tick_s':'Whole process CPU per tick'}
def load(p):return json.loads(Path(p).read_text())
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for block in iter(lambda:f.read(1048576),b''):h.update(block)
 return h.hexdigest()
def write(p,d):Path(p).write_text(json.dumps(d,indent=2)+'\n')
def source(p):
 p=Path(p);return dict(path=str(p),exists=p.exists(),sha256=sha(p) if p.exists() else None)
def link(p,text=None):return '<a href="'+html.escape(str(Path(p)),quote=True)+'">'+html.escape(text or Path(p).name)+'</a>'
def helper():
 s=importlib.util.spec_from_file_location('final_paired_stats',ART/'analyze_performance.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def gpu(r):
 g=r.get('gradient_execution',{});requested=g.get('gpu_requested_fields');complete=g.get('gpu_complete_fields')
 return dict(requested=requested,gpu_complete=complete,cpu_complete=g.get('cpu_complete_fields'),gpu_fraction=complete/requested if requested and complete is not None else None,unavailable=g.get('cpu_reason_unavailable'),cancellations=g.get('fallback_reason_cancellation'),driver_fallbacks=g.get('fallback_reason_driver'),gpu_error=r.get('gpu_error'),host_waits=r.get('gpu_host_event_waits'),switch_requested=r.get('gpu_requested'),scope='Actual end-only counters; switch on/empty error does not prove sustained GPU service.')
def gpu_sum(rows):
 keys=['requested','gpu_complete','cpu_complete','unavailable','cancellations','driver_fallbacks'];out={}
 for k in keys:
  values=[r[k] for r in rows if r.get(k) is not None];out[k]=sum(values) if values else None
 out['fraction']=out['gpu_complete']/out['requested'] if out.get('requested') and out.get('gpu_complete') is not None else None
 out['records']=len(rows);return out

def final_window(A,p):
 p=Path(p).resolve();pp=p/'plan.json';cp=p/'performance-complete.json'
 result=dict(cohort_folder=str(p),state='pending',expected_jobs=21,completed_jobs=0,complete=False,accepted=False,provenance=[source(pp),source(cp)],groups={},missing=[])
 if not pp.exists() or not cp.exists():return result
 plan=load(pp);gate=load(cp);assert len(plan['performance_jobs'])==21 and plan['planned_jobs']==21
 assert plan['sealed'] and sha(plan['binary'])==plan['binary_sha256']
 for path,h in plan['seals'].items():assert sha(path)==h,('Combined cohort sealed input changed',path)
 if not gate.get('passed') or gate.get('selected_jobs')!=21 or gate.get('plan_sha256')!=sha(pp):result['state']='invalid completion gate';return result
 groups={};actual_gpu=[]
 for j in plan['performance_jobs']:
  d=Path(j['output']);fp=d/'final-passed.json';iv=d/'input-verification.json'
  if not fp.exists():result['missing'].append(j['id']);continue
  f=load(fp);assert f['passed'] and f['identity']==j['identity'] and f['plan_sha256']==sha(pp) and f['repeat_override']==j['identity']['repeats'] and f['adapter_sha256']==sha(p/'run.py') and j['final_wrapper_sha256']==sha(p/'run.py')
  assert sha(j['after'])==j['identity']['after_sha256'] and sha(j['before'])==j['identity']['before_sha256'] and sha(j['manifest'])==j['identity']['manifest_sha256']
  cell,reason=A.read_job(j);assert cell is not None,(j['id'],reason)
  rows=[json.loads(line) for line in (d/'measurements.jsonl').read_text().splitlines()];assert len(rows)==2*(j['identity']['repeats']+1)
  for r in rows:
   assert r['result']['compute_threads']==3 and r['peak_rss_bytes']>r['runner_peak_rss_bytes'] and not r.get('timed_out',False) and not r.get('rss_censored',False)
   if j.get('execute_helper_sha256'):
    command=r['command'];output=Path(command[command.index('--output-dir')+1]);proofpath=output/'controller-proof.json';proof=load(proofpath)
    assert sha(p/'execute-one.py')==j['execute_helper_sha256'] and proof['execute_helper_sha256']==j['execute_helper_sha256'] and proof['execute_sha256']==j['identity']['execute_sha256'] and proof['native_runner_peak_rss_bytes']==r['runner_peak_rss_bytes']
   if r['repeat']>=0:actual_gpu.append(dict(job=j['id'],variant=r['variant'],repeat=r['repeat'],**gpu(r['result'])))
  cell['metrics']['cpu_per_tick_s']=dict(paired_log_ratios=[math.log(a/b) for a,b in zip(cell['cpu_per_tick_s']['candidate'],cell['cpu_per_tick_s']['baseline'])],baseline_values=cell['cpu_per_tick_s']['baseline'],candidate_values=cell['cpu_per_tick_s']['candidate'])
  cell['provenance'].update(final_pass_sha256=sha(fp));cell['whole_cpu_per_tick_s']=cell['cpu_per_tick_s'];cell['rss_floor_guard']='All recorded native HWM above fresh wrapper HWM';cell['layout']=j['scenario']
  group=groups.setdefault(j['scenario'],dict(seeds={},expected_seeds=[]));group['seeds'][str(j['seed'])]=cell;group['expected_seeds'].append(j['seed']);result['completed_jobs']+=1
 assert result['completed_jobs']==21 and not result['missing'],'Completion marker with missing job data'
 for layout,g in groups.items():
  g['expected_seed_count']=3 if layout=='large-mixed' else 2;assert len(g['seeds'])==g['expected_seed_count'];g['metrics']={k:A.stats(g['seeds'],k) for k in M}
  g['means']={v:{k:statistics.mean(statistics.mean(c['metrics'][k][v+'_values']) for c in g['seeds'].values()) for k in M} for v in ('baseline','candidate')}
  g['mean_cpu_per_tick_s']={v:statistics.mean(statistics.mean(c['cpu_per_tick_s'][v]) for c in g['seeds'].values()) for v in ('baseline','candidate')}
  g['adverse_cpu_wall']=[dict(metric=k,**g['metrics'][k]) for k in ('cpu_s','wall_s','simulation_cpu_s','simulation_wall_s') if g['metrics'][k]['percent_ci95'][0]>0]
 aggregate={k:A.aggregate_stats(list(groups.values()),k) for k in M}
 result.update(state='complete measured final fixed3 cohort',complete=True,groups=groups,aggregate=aggregate,gpu_records=actual_gpu,gpu_by_variant={v:gpu_sum([r for r in actual_gpu if r['variant']==v]) for v in ('baseline','candidate')},native_children=plan['planned_native_children'],scope='Final combined vs untouched baseline;10listed layouts,2seeds each plus mixed holdout third;3paired repeats or6forlarge-mixed; discard one warmup pair. Both binaries explicit3/GPUoff, identical fixture/mask per job. NO auto32→6 attribution.')
 return result

def failed_attempts():
 attempts=[]
 for folder in ('final-combined-performance','final-combined-performance-v2'):
  p=ART/folder;records=[];floor=[]
  if not p.exists():continue
  for f in p.rglob('measurements.jsonl'):
   count=0
   for line in f.read_text().splitlines():
    row=json.loads(line);count+=1
    if row.get('rss_censored') or (row.get('peak_rss_bytes') is not None and row.get('runner_peak_rss_bytes') is not None and row['peak_rss_bytes']<=row['runner_peak_rss_bytes']):floor.append(dict(file=str(f),repeat=row.get('repeat'),variant=row.get('variant'),native_hwm=row.get('peak_rss_bytes'),wrapper_hwm=row.get('runner_peak_rss_bytes')))
   records.append(dict(**source(f),records=count))
  failures=[source(f) for f in p.rglob('*failure*.json')]
  attempts.append(dict(folder=str(p),plan=source(p/'plan.json'),measurement_records=records,rss_floor_rejections=floor,failures=failures,eligible_for_final_metrics=False,classification='Measurement artifact: large pre-exec ancestry/wrapper footprint invalidated strict process RSS-floor guard. Retained evidence, no floor waiver or production change.'))
 return attempts

def full_games(A):
 p=ART/'final-full-natural';pp=p/'plan.json';cp=p/'native-complete.json';result=dict(state='pending',expected_cases=18,complete=False,accepted=False,completed_pairs=0,capped_cases=[],rss_floor_cases=[],groups={},cases=[],provenance=[source(pp),source(cp)])
 if not pp.exists() or not cp.exists():return result
 plan=load(pp);gate=load(cp);assert len(plan['cases'])==18 and plan['native_children']==18
 if gate.get('attempted_cases')!=18 or gate.get('plan_sha256')!=sha(pp) or gate.get('state') not in ('complete','complete_with_censoring'):result['state']='invalid completion gate';return result
 pairs={}
 for c in plan['cases']:
  d=p/'native'/c['id'];mp=d/'measurement.json';assert mp.exists(),'18attempt gate missing actual native record';m=load(mp);j=m['job'];assert not m['diagnostic'] and not m['timing_excluded'] and not m['validation_pending']
  assert j['plan_sha256']==sha(pp) and j['binary']==c['binary'] and j['fixture']==c['fixture'] and j['affinity']==c['affinity'] and sha(c['binary']['path'])==c['binary']['sha256']
  audit=load(d/'input-verification.json');assert audit['inputs_unchanged'];r=m.get('result') or {};capped=bool(m['timed_out']);ticks=r.get('ticks')
  record=dict(id=c['id'],layout=c['scenario']['id'],seed=c['seed'],variant=c['variant'],pair_id=c['pair_id'],completed=not capped,capped=capped,ticks_per_e2e_second=ticks/m['wall_s'] if ticks and not capped else None,wall_s=m['wall_s'],cpu_s=m['cpu_s'],peak_rss_bytes=m['peak_rss_bytes'],runner_peak_rss_bytes=m['runner_peak_rss_bytes'],rss_floor=bool(m['rss_censored']),ticks=ticks,cpu_per_tick_s=m['cpu_s']/ticks if ticks and not capped else None,engine_cpu_s=r.get('benchmark_run_cpu_ns',0)/1e9 if 'benchmark_run_cpu_ns' in r else None,gpu=gpu(r),termination=r.get('termination'),winning_teams=r.get('winning_teams'),measurement=str(mp),measurement_sha256=sha(mp),audit_sha256=sha(d/'input-verification.json'),affinity=c['affinity'],compute_threads=3,gpu_setting='on')
  if capped:assert load(d/'censored.json')['completed'] is False;result['capped_cases'].append(record['id'])
  else:
   outcome=load(d/'validated-outcome.json');assert outcome['passed'] and outcome['natural_completed'] and m['completed_native_timing_eligible'] and m['returncode']==0 and r['termination']=='engine_end' and r['winning_teams'] and r['compute_threads']==3
   replay=load(d/'replay-validation.json');assert sha(d/'game.replay')==replay['replay_sha256'] and (d/'game.replay').stat().st_size==replay['replay_bytes'];record['replay_validation']=replay;record['outcome']={k:r.get(k) for k in ('ticks','termination','winning_teams','initialChecksum','finalChecksum')};record['completed_e2e_s']=m['wall_s']
  if record['rss_floor']:result['rss_floor_cases'].append(record['id'])
  result['cases'].append(record);pairs.setdefault(c['pair_id'],{})[c['variant']]=record
 groups={}
 for pair_id,pair in pairs.items():
  assert set(pair)=={'baseline','final'};b=pair['baseline'];f=pair['final'];g=groups.setdefault(b['layout'],dict(pairs={},excluded_completion_pairs=[],expected_pairs=3))
  if b['capped'] or f['capped']:g['excluded_completion_pairs'].append(pair_id);continue
  assert b['outcome']==f['outcome'] and b['replay_validation']==f['replay_validation'],'Completed natural pair outcome/replay divergence'
  metrics={}
  for k in ('cpu_s','wall_s','cpu_per_tick_s','peak_rss_bytes'):
   if k=='peak_rss_bytes' and (b['rss_floor'] or f['rss_floor']):continue
   metrics[k]=dict(paired_log_ratios=[math.log(f[k]/b[k])],baseline_values=[b[k]],candidate_values=[f[k]])
  g['pairs'][str(b['seed'])]=dict(pair_id=pair_id,metrics=metrics,baseline=b['id'],final=f['id']);result['completed_pairs']+=1
 for layout,g in groups.items():
  g['metrics']={k:A.stats({s:v for s,v in g['pairs'].items() if k in v['metrics']},k) for k in ('cpu_s','wall_s','cpu_per_tick_s','peak_rss_bytes')}
  g['gpu_by_variant']={v:gpu_sum([r['gpu'] for r in result['cases'] if r['layout']==layout and r['variant']==v and r['completed']]) for v in ('baseline','final')}
  g['completed_mean_counts']={v:sum(r['layout']==layout and r['variant']==v and r['completed'] for r in result['cases']) for v in ('baseline','final')}
  g['completed_means']={v:{k:statistics.mean([r[k] for r in result['cases'] if r['layout']==layout and r['variant']==v and r['completed']]) if any(r['layout']==layout and r['variant']==v and r['completed'] for r in result['cases']) else None for k in ('wall_s','cpu_s','cpu_per_tick_s')} for v in ('baseline','final')}
 result.update(state='complete attempts with one-hour censoring' if result['capped_cases'] else 'complete natural winner cohort',complete=True,groups=groups,scope='18fresh native attempts/9matched pairs;3layouts×3seeds×baseline/final,explicit3/GPUon,one run per variant/seed. Completed-game means exclude capped cases; paired comparisons require both completed and exact winner/ticks/nativeCRCs/replay/AIT1 agreement. RSS floor guard affects memory estimates only. No native per-tick observer.')
 return result

def check_status():
 paths={'release_build':ART/'final-validation/build-complete.json','focused_native':ART/'final-validation/focused-complete.json','hardware':ART/'final-validation/gpu-qualification/complete.json','browser':ART/'final-validation/runs/browser/execution.json','browser_equivalence':ART/'final-validation/runs/browser/equivalence.json','browser_lifecycle':ART/'final-validation/runs/browser/lifecycle-summary.json'};checks={}
 for k,p in paths.items():
  s=source(p);d=load(p) if p.exists() else {};s['status']='complete' if d.get('complete') is True or d.get('passed') is True or d.get('state')=='passed' else 'pending';s['receipt']=d;checks[k]=s
 ip=ART/'final-integration/plan.json';s=source(ip);passed=0;caseproofs=[]
 if ip.exists():
  plan=load(ip);assert len(plan['cases'])==25
  for c in plan['cases']:
   p=ip.parent/'runs'/(c['stage']+'-'+c['id'])/'passed.json';r=load(p) if p.exists() else {}
   valid=r.get('passed') is True and r.get('binary_sha256')==plan['binary_sha256'] and r.get('children')==c['children'] and r.get('job',{}).get('plan_sha256')==sha(ip)
   passed+=valid;caseproofs.append(dict(**source(p),valid=valid))
 s.update(status='complete' if passed==25 else 'pending',passed_cases=passed,expected_cases=25,planned_children=sum(c['children'] for c in plan['cases']) if ip.exists() else None,case_proofs=caseproofs,scope='Initial and loaded mature detailed GCS/world/replay/continuation diagnostics, including one separately labelled automatic-six control. These diagnostic timings are not performance measurements.');checks['integration']=s
 optional=ART/'final-full-natural/diagnostic-complete.json';checks['optional_full_GCS']=dict(**source(optional),status='complete' if optional.exists() and load(optional).get('passed') else 'pending or censored',required=False,scope='Optional3pairs/6games only; native timing results do not establish all-tick equivalence.')
 return checks

def bind_checks(checks):
 bp=ART/'final-integration/build.json'
 if not bp.exists():return None
 b=load(bp);bh=sha(bp);assert b['state']=='complete'
 for role in ('binary','unit_binary','engine_binary'):assert sha(b[role])==b[role+'_sha256']
 assert sha(b['source_freeze'])==b['source_freeze_sha256']
 for f,h in b['source_sha256'].items():assert sha(ROOT/f)==h,('Final built source changed',f)
 for f,h in b['dependency_hashes'].items():assert sha(f)==h,('Final dependency changed',f)
 def xml_ok(path,expected=None):
  attrs=ET.parse(path).getroot().attrib;assert int(attrs.get('tests',0))>0 and all(int(attrs.get(k,0))==0 for k in ('failures','errors','skipped'))
  if expected is not None:assert int(attrs['tests'])==expected
 for key in ('release_build','focused_native','hardware'):
  c=checks[key]
  if c['status']=='complete':assert c['receipt']['build_manifest_sha256']==bh,('stale final receipt',key)
 if checks['focused_native']['status']=='complete':
  f=checks['focused_native']['receipt'];assert set(f['focused_roles_passed'])=={'engine','unit'} and f['broader_compatibility_passed'] and f['known_four_retained_failures']==4 and f['known_first_messages_match']
  for role in ('unit','engine'):
   p=ART/'final-integration'/('focused-'+role+'.json');r=load(p);assert r['passed'] and r['binary_role']==role and r['binary_sha256']==b[role+'_binary_sha256'] and r['build_manifest_sha256']==bh and sha(r['junit'])==r['junit_sha256'];xml_ok(r['junit'],r['testsselected'])
  known=load(ART/'final-validation/runs/known-four/receipt.json');assert known['passed'] is False and known['known_failure_first_messages_match'] and known['counts']==dict(tests=4,failures=4,errors=0,skipped=0) and sha(known['baseline_junit'])==known['baseline_junit_sha256']
 if checks['hardware']['status']=='complete':
  h=checks['hardware']['receipt'];assert h['gpu_decision']==b['gpu_decision'] and h['source_freeze_sha256']==b['source_freeze_sha256'] and h['selected_cases']==4 and h['participant_counts']==[1,3,6] and h['material_participant_counts']==[1,2,3,4,6]
  for r in h['receipts']:assert sha(r['junit'])==r['junit_sha256'] and sha(r['proof_log'])==r['proof_log_sha256'];xml_ok(r['junit'],1)
 if checks['browser']['status']=='complete':
  folder=ART/'final-validation/runs/browser';plan=load(folder/'verification-plan.json');assert plan['seals'][str(bp)]==bh
  for f,h in plan['seals'].items():assert sha(f)==h,('browser proof input drift',f)
  ex=checks['browser']['receipt'];assert ex['plan_sha256']==sha(folder/'verification-plan.json') and ex['historical_fixed_outputs_restored'] and ex['historical_receipts_promoted'] is False
  assert ex['equivalence_sha256']==sha(folder/'equivalence.json') and ex['lifecycle_sha256']==sha(folder/'lifecycle-summary.json')
  eq=load(folder/'equivalence.json');assert eq['state']=='passed' and eq['plan_sha256']==ex['plan_sha256'] and eq['playwright_cases']==27 and eq['stock_trace_cases']==15 and eq['match_verify_trace_cases']==6 and eq['playwright_report_sha256']==sha(folder/'playwright.json')
  for r in eq['per_test_traces']:assert sha(r['file'])==r['sha256']
  life=load(folder/'lifecycle-summary.json');assert len(life['corpora'])==6
  for r in life['corpora']:assert r['passed']==r['selected_cases'] and r['failed']==0 and sha(r['log'])==r['log_sha256']
 commit=ART/'final-source-commit.json'
 if commit.exists():
  c=load(commit);assert c['build_manifest_sha256']==bh and c['binary_sha256']==b['binary_sha256'] and c['working_source_matches_built_inventory'];return dict(**source(commit),receipt=c)
 return None

def independent():
 paths={'isolated_initial':ART/'performance-analysis.json','mature_V1':ART/'extension-analysis.json','telemetry_selected24':ART/'adoption-review/telemetry-selected24-completed.json','snapshot_rejection':ART/'adoption-review/snapshot-v2-completed-analysis.json','GPU_sync_recovery':ART/'gpu-analysis/analysis.json','final_combined_v3_lineage':ART/'final-analysis/combined21-v3-lineage.json','final_commit_review':ART/'final-validation/final-commit-review.json'};result={};adverse=[]
 for name,p in paths.items():
  result[name]=source(p)
  if not p.exists():continue
  d=load(p)
  groups=d.get('groups',{})
  if name=='isolated_initial':groups={factor+'/'+layout:cell for factor,x in d.get('factors',{}).items() for layout,cell in x['layouts'].items()}
  if name=='mature_V1':groups={k:v for phase in ('mature','v2-followup') for k,v in d.get(phase,{}).get('groups',{}).items()}
  if name=='GPU_sync_recovery':groups={factor+'/'+key:g for factor in ('gpu-sync','gpu-recovery') for key,g in d.get(factor,{}).get('groups',{}).items()}
  for context,g in groups.items():
   for k in ('cpu_s','wall_s'):
    v=g.get('metrics',{}).get(k,{})
    if v.get('percent_change') is not None and v.get('percent_ci95') and v['percent_ci95'][0]>0:adverse.append(dict(evidence=name,context=context,metric=k,percent_change=v['percent_change'],percent_ci95=v['percent_ci95'],source=str(p)))
 return dict(sources=result,adverse_cells=adverse,scope='Earlier one-factor/driver/context evidence, not final integrated metrics. Retain all positive lower-bound CPU/wall adverse cells, even below5%. Auto32→6 contrasts are separated from final explicit3.')

def estimate(v):
 if v.get('ratio') is None:return 'pending / insufficient completed data'
 lo,hi=v['percent_ci95'];return f"{v['percent_change']:+.2f}% [{lo:+.2f}, {hi:+.2f}]"
def cell_table(groups,metrics):
 text='<table><tr><th>Layout</th><th>Seeds/pairs</th>'+''.join('<th>'+html.escape(M.get(k,'Process CPU per tick' if k=='cpu_per_tick_s' else k))+'</th>' for k in metrics)+'</tr>'
 for name,g in groups.items():text+='<tr><td>'+html.escape(name)+'</td><td>'+str(len(g.get('seeds',g.get('pairs',{}))))+'</td>'+''.join('<td>'+estimate(g['metrics'][k])+'</td>' for k in metrics)+'</tr>'
 return text+'</table>'
def shell(title,content):return '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+title+'</title><style>body{font:15px/1.5 system-ui;margin:30px;color:#20314b;max-width:1450px}table{border-collapse:collapse;font-size:13px}th,td{padding:8px;border-bottom:1px solid #ccc;text-align:left}.note{background:#fff1c9;padding:15px}.bad{color:#a22}a{overflow-wrap:anywhere}</style><h1>'+title+'</h1>'+content+'</html>'
def render(r):
 w=r['final_window'];f=r['full_natural'];status='ADOPTED: final finite evidence complete and acceptance bound to the tested source commit and binary.' if r['accepted'] else ('Finite final evidence complete; adoption status pending.' if r['required_evidence_complete'] else 'PENDING: final finite measurements or validation still incomplete. No earlier isolated numbers promoted to final proof.')
 core='<p class="note">'+status+'</p><p>Final selection retains all gameplay/engine features: affinity-aware automatic worker sizing, lossless compact replay telemetryV2 and mandatory OpenCL host synchronization. Snapshot ownership, conditional scheduler wake changes and cancellation recovery were rejected after their separate tradeoffs. The optional snapshot sidecar is a pooled capacity/reference census, not a memory optimization or peak RSS measurement.</p>'
 core+='<p>Worker sizing discovers eligible logical CPUs on Linux/Android with a portable fallback; explicit positive counts remain unchanged. No universal physical-core/SMT heuristic or benchmark-specific limit. Earlier auto32→6 savings are separate evidence; the final fixed-three-participant comparisons hold both binaries at three, so do not attribute their results to changed worker count.</p>'
 core+='<p>Telemetry shares immutable ordered metadata and retains every value/update stamp, duplicate/NUL/order/schema transition and original AIT1 bytes/budget/truncation behavior. Sync supplies the OpenCL host acquisition required before transfer data is consumed; it is a correctness prerequisite, not a promised speedup. Permanent-failure/exact CPU fallback remains.</p>'
 core+='<h2>Final fixed-window release metrics</h2><p>'+html.escape(w['state'])+'; verified '+str(w['completed_jobs'])+'/21 jobs.</p>'
 if w['complete']:
  core+='<p>Equal weight per listed layout, then per seed/repeat: '+', '.join(M[k]+' '+estimate(w['aggregate'][k]) for k in ('cpu_s','wall_s','peak_rss_bytes'))+'.</p>'+cell_table(w['groups'],('cpu_s','wall_s','peak_rss_bytes'))
 if r.get('plots',{}).get('state')=='complete':core+='<p>'+link(HERE/'final-layout-metrics.svg','Per-layout figure')+'</p><img style="max-width:100%" src="'+str(HERE/'final-layout-metrics.svg')+'" alt="Final per-layout CPU, wall and RSS ratios with descriptive intervals">'
 core+='<p>Measurement cohort '+link(w['cohort_folder'],'sealed final combined folder')+'. Failed earlier controller attempts are retained separately; their RSS-floor rejection is not a game performance regression and no RSS guard is waived.</p>'
 core+='<h2>Natural winner cohort</h2><p>'+html.escape(f['state'])+'; completed matched pairs '+str(f['completed_pairs'])+'/9, capped cases '+str(len(f['capped_cases']))+'.</p>'
 if f['complete']:core+=cell_table(f['groups'],('cpu_s','wall_s','cpu_per_tick_s','peak_rss_bytes'))
 core+='<p>'+link(HERE/'details.html','Detailed metrics, censoring, GPU counters and adverse cells')+' · '+link(HERE/'analysis.json','Exact data/provenance')+' · '+link(ART/'adoption-decisions.json','Adoption decisions')+'</p>'
 if r.get('source_commit'):core+='<p>Final source '+html.escape(r['source_commit']['receipt']['source_commit'])+' · '+link(r['source_commit']['path'],'source/build binding')+'</p>'
 core+='<p>Descriptive deterministic seed/repeat bootstrap, 4000 draws / RNG 7134; few seeds and multiple cells limit inference. Fixed windows differ from continuous full games. Native winner/replay checks do not establish every-tick equality; selected detailed GCS/world cohorts provide that scope. Linux x86_64 / GCC 13 and selected Emscripten 4.0.15 / software browsers are the claimed coverage; Windows/macOS/mobile and graphicsGPU/play feel remain unverified.</p>'
 details='<p>'+link(HERE/'summary.html','Summary')+'</p><h2>Final window scopes and native values</h2><p>'+html.escape(w.get('scope','Required 21-job gate pending; metrics withheld.'))+'</p>'
 if w['complete']:
  details+=cell_table(w['groups'],tuple(M))
  for layout,g in w['groups'].items():
   details+='<h3>'+html.escape(layout)+'</h3><p>Equal-seed arithmetic means: '+html.escape(str(g['means']))+'; whole CPU/tick seconds '+html.escape(str(g['mean_cpu_per_tick_s']))+'.</p>'
   for seed,s in g['seeds'].items():details+='<p>Seed '+seed+', ticks '+str(s['measured_ticks'])+',CPU/tick baseline '+str(s['whole_cpu_per_tick_s']['baseline'])+' → final '+str(s['whole_cpu_per_tick_s']['candidate'])+' seconds; '+link(Path(s['output'])/'measurements.jsonl','native paired records')+'.</p>'
 details+='<h2>Natural completed vs capped attempts</h2><p>'+html.escape(f.get('scope','Required 18-attempt gate pending; native metrics withheld.'))+'</p>'
 if f['complete']:
  details+='<table><tr><th>Layout/seed/variant</th><th>Outcome</th><th>E2E s</th><th>CPU s</th><th>Ticks</th><th>CPU/tick s</th><th>Peak RSS MiB</th><th>RSS floor</th><th>GPU completed/requested</th></tr>'
  for c in f['cases']:
   g=c['gpu'];fraction=f"{g['gpu_complete']}/{g['requested']} ({100*g['gpu_fraction']:.3f}%)" if g['gpu_fraction'] is not None else 'unavailable counter'
   details+='<tr><td>'+html.escape(c['layout']+'/'+str(c['seed'])+'/'+c['variant'])+'</td><td>'+('capped; excluded completion means' if c['capped'] else 'winner '+str(c['winning_teams']))+'</td><td>'+f"{c['wall_s']:.3f}"+'</td><td>'+f"{c['cpu_s']:.3f}"+'</td><td>'+str(c['ticks'])+'</td><td>'+str(c['cpu_per_tick_s'])+'</td><td>'+f"{c['peak_rss_bytes']/2**20:.2f}"+'</td><td>'+str(c['rss_floor'])+'</td><td>'+fraction+'</td></tr>'
  details+='</table><p>Capped elapsed/CPU observations remain visible, but no time-to-winner or CPU/tick completion estimate is assigned to them. RSS floor records are retained and excluded from paired memory estimates. Actual GPU fractions are end-only field counters; they do not measure GPU device time.</p>'
  for layout,g in f['groups'].items():details+='<p>'+html.escape(layout)+' completed E2E means '+html.escape(str(g['completed_means']))+' (completed counts '+html.escape(str(g['completed_mean_counts']))+')'+'; '+link(ART/'final-full-natural/native-complete.json','native completion/censors')+'.</p>'
 details+='<h2>Failed measurement attempts retained</h2><p>These records are excluded from all final metrics; production source/binaries were unchanged while the measurement controller ancestry was corrected.</p><ul>'
 for a in r['failed_measurement_attempts']:details+='<li>'+link(a['folder'])+': '+str(sum(x['records'] for x in a['measurement_records']))+' raw records, '+str(len(a['rss_floor_rejections']))+' observed RSS-floor rejects; '+link(a['plan']['path'],'original plan')+'</li>'
 details+='</ul>'
 details+='<h2>Validation gates and known failures</h2><ul>'
 for name,c in r['checks'].items():details+='<li>'+html.escape(name)+': '+html.escape(c['status'])+' · '+link(c['path'])+'</li>'
 details+='</ul><p>'+link(ART/'final-validation/final-commit-review.json','Independent final commit review')+' · '+link(ART/'final-analysis/combined21-v3-lineage.json','Independent final v3 measurement lineage')+' · '+link(ART/'final-analysis/combined21-v3.html','Independent completed21 results')+(' · '+link(ART/'final-analysis/natural9-review.json','Independent natural nine-pair review') if (ART/'final-analysis/natural9-review.json').exists() else '')+'</p><p>Four preexisting baseline failures are retained as failures in the separate known-four receipt, with exact first-failure comparison; they are not four passes or evidence of later trace equality.</p><p>'+link(ART/'final-validation/runs/known-four/receipt.json','Known-four evidence')+' · '+link(ART/'final-core-review/review.json','Independent core review')+' · '+link(ART/'final-helper-review/review.json','Independent helper review')+'</p>'
 details+='<h2>Independent experiments and adverse cells retained</h2><p>These are historical isolated comparisons, not the final integrated workload. V1 telemetry large-mixed regression motivated V2; V2 mature direct GPU-on adverse cells, snapshot tradeoffs, scheduler mixed results and recovery 39 CPU/wall costs remain in this record. No lower-bound 5% waiver.</p><table><tr><th>Evidence/context</th><th>Metric</th><th>Change/interval</th></tr>'
 for c in r['independent']['adverse_cells']:details+='<tr><td>'+html.escape(c['evidence']+'/'+c['context'])+' '+link(c['source'],'source')+'</td><td>'+c['metric']+'</td><td>'+f"{c['percent_change']:+.3f}% {c['percent_ci95']}"+'</td></tr>'
 details+='</table><h2>Implementation and durable guides</h2><p>'+ ' · '.join(link(ROOT/p) for p in ['src/common/ComputeThreads.cpp','src/replay/ReplayTelemetry.cpp','src/field/OpenCLGradient.cpp','src/engine/sim/snapshot/SnapshotStorage.cpp','docs/ai/telemetry.md','docs/development/headless-replays.md'])+'</p>'
 return shell('Final core optimization review',core),shell('Final core optimization: detailed evidence',details)

def plots(r):
 if not r['final_window']['complete']:return dict(state='pending; final21 gate required',outputs=[])
 try:
  import matplotlib
  matplotlib.use('Agg')
  import matplotlib.pyplot as plt
 except ImportError:return dict(state='Plotting dependency unavailable; metrics still exact. Run analyzer with existing analysis Python environment.',outputs=[])
 groups=r['final_window']['groups'];names=list(groups);fig,axes=plt.subplots(1,3,figsize=(15,6),sharey=True)
 for ax,metric in zip(axes,('cpu_s','wall_s','peak_rss_bytes')):
  vals=[groups[n]['metrics'][metric]['percent_change'] for n in names]
  lo=[v-groups[n]['metrics'][metric]['percent_ci95'][0] for n,v in zip(names,vals)]
  hi=[groups[n]['metrics'][metric]['percent_ci95'][1]-v for n,v in zip(names,vals)]
  ax.errorbar(vals,list(range(len(names))),xerr=[lo,hi],fmt='o',color='#245790',capsize=3)
  ax.axvline(0,color='#888',linewidth=1);ax.set_title(M[metric]);ax.set_xlabel('Final / baseline change (%)');ax.grid(axis='x',alpha=.2)
 axes[0].set_yticks(range(len(names)),names);axes[0].invert_yaxis()
 fig.suptitle('Final combined: explicit 3 participants, GPU off; descriptive 95% seed/repeat intervals')
 fig.tight_layout();files=[]
 for suffix in ('svg','png'):
  path=HERE/('final-layout-metrics.'+suffix);fig.savefig(path,dpi=160);files.append(str(path))
 plt.close(fig);return dict(state='complete',outputs=files)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--plan-only',action='store_true');ap.add_argument('--combined-folder',type=Path,default=ART/'final-combined-performance-v3',help='Explicit complete final cohort folder; never falls back to failed earlier attempts.');args=ap.parse_args()
 if args.plan_only:print(json.dumps(dict(read_only=True,launches=0,combined_folder=str(args.combined_folder.resolve()),required_final_jobs=21,required_native_attempts=18,old_evidence_promoted=False,outputs=['analysis.json','summary.html','details.html']),indent=2));return
 os.sched_setaffinity(0,{15,31});A=helper();window=final_window(A,args.combined_folder);full=full_games(A);checks=check_status();commit=bind_checks(checks);required=all(c['status']=='complete' for c in checks.values() if c.get('required',True));decision=ART/'adoption-decisions.json'
 r=dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),accepted=False,required_evidence_complete=window['complete'] and full['complete'] and required,final_window=window,failed_measurement_attempts=failed_attempts(),full_natural=full,checks=checks,source_commit=commit,adoption_decisions=load(decision),adoption_source=source(decision),independent=independent(),analyzer_sha256=sha(Path(__file__)),stats_helper_sha256=sha(ART/'analyze_performance.py'),bootstrap_seed=7134,bootstrap_draws=4000,native_engine_browser_launches=0,limitations=['No performance or completion metrics copied from earlier isolated cohorts into final sections.','Few seeds/one full run per variant/seed; descriptive intervals, no universal inference.','Full-native replay/outcome checks are distinct from optional full GCS/world diagnostics.','Peak RSS is process high-water, not retained heap or census capacity.'])
 package=r['adoption_decisions'].get('final_package',{})
 final_source=commit['receipt']['source_commit'] if commit else None
 built_binary=load(ART/'final-integration/build.json')['binary_sha256'] if (ART/'final-integration/build.json').exists() else None
 accepted=bool(r['required_evidence_complete'] and package.get('accepted') is True and final_source and package.get('source_commit')==final_source and package.get('binary_sha256')==built_binary)
 r['accepted']=accepted;r['final_window']['accepted']=accepted;r['full_natural']['accepted']=accepted
 r['acceptance_binding']=dict(accepted=accepted,required_evidence_complete=r['required_evidence_complete'],required_source_commit=final_source,required_binary_sha256=built_binary,final_package=package,scope='Adoption is valid only for the complete evidence and exact final tested source/binary binding.')
 r['plots']=plots(r);write(HERE/'analysis.json',r);summary,details=render(r);(HERE/'summary.html').write_text(summary);(HERE/'details.html').write_text(details);print(json.dumps(dict(required_evidence_complete=r['required_evidence_complete'],accepted=accepted,window_state=window['state'],full_state=full['state'],launches=0)))
if __name__=='__main__':main()
