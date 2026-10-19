"""Frozen attribution summaries. Instrumented runs never enter CPU estimates."""
import argparse,collections,csv,json,pathlib,statistics
B=pathlib.Path(__file__).resolve().parent
A=argparse.ArgumentParser();A.add_argument('--cpu-only',action='store_true');args=A.parse_args()
P=json.loads((B/'PLAN.json').read_text());S=json.loads((B/'STATUS.json').read_text())
assert json.loads((B/'CPU-PARITY.json').read_text())['status']=='PASS'
cpu=[r for r in S['rows'] if r['phase']=='cpu'];assert len(cpu)==64

def dump(name,obj):
 p=B/name;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(obj,indent=2)+'\n');t.replace(p)
def stats(values):return dict(mean=statistics.mean(values),median=statistics.median(values),minimum=min(values),maximum=max(values),values=values)
def summarize(rows):
 measured=sum(r['measured_ticks'] for r in rows);advanced=sum(r['advanced_ticks'] for r in rows);wall=sum(r['wall_seconds'] for r in rows);process=sum(r['total_process_cpu_seconds'] for r in rows);sim=sum(r['total_simulation_cpu_seconds'] for r in rows)
 return dict(runs=len(rows),measured_ticks=measured,advanced_ticks=advanced,simulation_cpu_seconds=sim,simulation_cpu_ms_per_tick=sim*1000/measured,process_cpu_seconds=process,wall_seconds=wall,pooled_wall_tps=advanced/wall,cpu_ms_per_tick_repeats=stats([r['simulation_cpu_ms_per_measured_tick'] for r in rows]),process_cpu_repeats=stats([r['total_process_cpu_seconds'] for r in rows]),wall_seconds_repeats=stats([r['wall_seconds'] for r in rows]))
arms=[a['id'] for a in P['arms']];groups=[m['group'] for m in P['maps']]
summary=dict(status='CPU-PASS',scope='Two preselected surviving master175 warm states; 256 settling +8192 measured ticks per run, four balanced serial repeats, Mac release, all telemetry off. Same master assets and native dependencies. Engine run CPU excludes setup/settling; process CPU and wall include them. Pooled TPS is summed advanced ticks / summed wall.',arms={a:dict(pooled=summarize([r for r in cpu if r['arm']==a]),states={g:summarize([r for r in cpu if r['arm']==a and r['group']==g]) for g in groups}) for a in arms},contrasts=[])
contrasts=[('class_on','class_off'),('class_off','master_reference'),('adaptive16','gen005_reference'),('gen005_reference','master_reference'),('adaptive16','fixed8'),('adaptive16','fixed32'),('adaptive16','fixed128')]
for numerator,denominator in contrasts:
 def contrast(g=None):
  n=summary['arms'][numerator]['states'][g] if g else summary['arms'][numerator]['pooled'];d=summary['arms'][denominator]['states'][g] if g else summary['arms'][denominator]['pooled']
  ratios=[]
  for rep in range(4):
   nn=[r for r in cpu if r['arm']==numerator and r['repeat']==rep and (g is None or r['group']==g)];dd=[r for r in cpu if r['arm']==denominator and r['repeat']==rep and (g is None or r['group']==g)]
   ratios.append((sum(r['total_simulation_cpu_seconds'] for r in nn)/sum(r['measured_ticks'] for r in nn))/(sum(r['total_simulation_cpu_seconds'] for r in dd)/sum(r['measured_ticks'] for r in dd))-1)
  return dict(cpu_fraction_change=n['simulation_cpu_ms_per_tick']/d['simulation_cpu_ms_per_tick']-1,process_cpu_fraction_change=n['process_cpu_seconds']/d['process_cpu_seconds']-1,wall_fraction_change=n['wall_seconds']/d['wall_seconds']-1,pooled_tps_fraction_change=n['pooled_wall_tps']/d['pooled_wall_tps']-1,paired_repeat_cpu_fraction_changes=stats(ratios))
 summary['contrasts'].append(dict(numerator=numerator,denominator=denominator,pooled=contrast(),states={g:contrast(g) for g in groups}))
summary['limitations']=['Already examined two-state panel, not fresh generalization or whole-game outcomes. Third planned state ended before common warm checkpoint and was excluded before attribution outcomes.','Four repeats characterize local spread, not independent gameplay samples or population confidence intervals. Mac was checked for competing builds/games and sampled load/thermal/power; it is not an isolated physical core or continuously controlled thermal/load environment.','Class gate uses unchanged master round-robin one-job admission; freed scans may fill the same quota. Same-binary off includes class bookkeeping; immutable reference quantifies its overhead.','Modern fixed timers retain modern demand, cold/reactive admission, history initialization, minimum age and batching. Their differences from adaptive identify model versus simple demanded timers, not class elimination alone. Whole-branch minus class differences are not additive/model-only causal attribution.']
dump('CPU-SUMMARY.json',summary)
lines=['# CPU attribution','',summary['scope'],'','| Arm | CPU ms/measured tick | Process CPU s | Wall s | Pooled ticks/s |','|---|---:|---:|---:|---:|']
for a in arms:
 s=summary['arms'][a]['pooled'];lines.append(f"| {a} | {s['simulation_cpu_ms_per_tick']:.6f} | {s['process_cpu_seconds']:.3f} | {s['wall_seconds']:.3f} | {s['pooled_wall_tps']:.1f} |")
lines+=['','Contrasts use ratios of pooled equal-tick means; negative CPU change saves compute.','']
for c in summary['contrasts']:
 q=c['pooled'];v=q['paired_repeat_cpu_fraction_changes'];lines.append(f"- {c['numerator']} / {c['denominator']}: CPU {q['cpu_fraction_change']:+.2%}; paired repeat range {v['minimum']:+.2%} to {v['maximum']:+.2%}.")
lines+=['']+summary['limitations'];(B/'CPU-README.md').write_text('\n'.join(lines)+'\n')
if args.cpu_only:raise SystemExit(0)
assert S['status']=='COMPLETE'
family={'0':'materials','1':'markets','2':'guard','3':'clear','4':'forbidden'}
def rows_csv(path):
 with path.open() as f:return list(csv.DictReader(f))
def aggregate_quality(rows):
 by={}
 for label,data in [('all',rows)]+[(f,[r for r in rows if family.get(r['family'],r['family'])==f]) for f in sorted({family.get(r['family'],r['family']) for r in rows})]:
  n=len(data);out=dict(queries=n)
  for field in ['extra_cost','false_unreachable','false_reachable','invalid_goal','missed_move','escape_failure','stall']:
   if n and field in data[0]:
    total=sum(float(r[field]) for r in data);out[field]=dict(total=total,per_query=total/n)
  by[label]=out
 return by
quality=[]
for r in [r for r in S['rows'] if r['phase']=='quality']:
 out=pathlib.Path(r['output']);lo=next(m['start_tick'] for m in P['maps'] if m['group']==r['group'])+P['benchmark_warmup'];hi=lo+P['cpu_measured_ticks']
 selected=lambda name:[x for x in rows_csv(out/name) if lo<=int(x['tick'])<hi]
 movement=selected('gradient-quality.csv');planning=selected('gradient-planning-quality.csv');work=selected('gradient-work.csv');w={}
 for f in sorted({x['family'] for x in work}):
  data=[x for x in work if x['family']==f];events=collections.Counter(x['event'] for x in data);w[f]=dict(events=dict(events),field_cells_sum=sum(int(x['field_cells']) for x in data),settled_cost_advancement=sum(max(0,int(x['settled_cost_after'])-int(x['settled_cost_before'])) for x in data if x['event'] in ['prepared_search','search_resume']))
 quality.append(dict(arm=r['arm'],group=r['group'],format=r['format'],tick_window=[lo,hi],movement=aggregate_quality(movement),planning=aggregate_quality(planning),work_by_family=w))
classdiag=[]
full=json.loads((B/'CLASS-FULL-STATUS.json').read_text()) if (B/'CLASS-FULL-STATUS.json').exists() else None
if full:assert full['status']=='COMPLETE' and full['diagnostic_vs_cpu_final_checksums_exact']
for r in [r for r in S['rows'] if r['phase']=='class_diagnostic']+(full['rows'] if full else []):
 path=pathlib.Path(r['output'])/'class-diagnostic.json';report=json.loads(path.read_text());totals=collections.Counter();by_family={}
 for t in report['teams']:
  for f in t['families']:
   name=family.get(str(f['family']),str(f['family']));entry=by_family.setdefault(name,collections.Counter())
   for m in f['movement']:
    for k in ['periodic_candidates','known_zero_candidates','skipped','admissions']:entry[k]+=m[k];totals[k]+=m[k]
 admissions=report['cold_admissions']+report['warm_admissions']
 classdiag.append(dict(arm=r['arm'],group=r['group'],format=r['format'],measured_ticks=r['measured_ticks'],totals=dict(totals),by_family={k:dict(v) for k,v in by_family.items()},cold_admissions=report['cold_admissions'],warm_admissions=report['warm_admissions'],consumer_admissions=report['consumer_admissions'],all_admissions_per_measured_tick=admissions/r['measured_ticks'],report=report))
summary.update(status='COMPLETE',quality=quality,class_diagnostics=classdiag,diagnostic_notes=['Quality and work runs are separate observer-ON runs; no diagnostic CPU measurements enter CPU comparisons. Movement and planning have separate observed-query denominators. Rates are actual policy-specific exposure, not identical counterfactual queries.','Per-family work event counts and lazy prepared/resume advancement are retained separately. Class diagnostic counters cover the measured window only; final population bins are derived runtime observations.'])
dump('SUMMARY.json',summary)
lines+=['','## Separate pathfinding diagnostics','','All rates use observed queries during ticks16640–24831, excluding256 settling ticks. Query exposure differs by policy. These ON runs supply no CPU estimates.','', '| State | Policy | Movement n | Extra direction cost/query | Stalls | False unreachable | False reachable | Planning n | Planning false unreachable | Planning false reachable |','|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
for r in sorted(quality,key=lambda r:(r['format'],['adaptive16','fixed8','fixed32','fixed128'].index(r['arm']))):
 m=r['movement']['all'];p=r['planning']['all'];lines.append(f"| {r['format']} | {r['arm']} | {m['queries']} | {m['extra_cost']['per_query']:.3f} | {m['stall']['per_query']:.3%} | {m['false_unreachable']['per_query']:.3%} | {m['false_reachable']['per_query']:.3%} | {p['queries']} | {p['false_unreachable']['per_query']:.3%} | {p['false_reachable']['per_query']:.3%} |")
lines+=['','Adaptive16 saves9.39% CPU versus demanded fixed32 on this panel, while extra direction cost/query and stall rate are worse on both states. Fixed8 costs more and improves several errors, with mixed direction quality across states. Fixed128 is near CPU parity with Adaptive16 (Adaptive16 +0.77%) and direction quality is mixed. This is a tradeoff, not evidence of adaptive dominance or an assignable learned share of the whole-branch CPU gain.','', '## Class-gate diagnostics','','Separate8192-tick measured windows aligned with CPU runs, after256 settling; counters ON here and OFF in CPU runs. Initial/final checksums match corresponding CPU arms. The original shorter1024-tick diagnostics are retained separately in SUMMARY.json.','', '| State | Gate | Candidates | Known-zero candidates | Skipped | Admissions | Admissions/tick |','|---|---|---:|---:|---:|---:|---:|']
for r in sorted([r for r in classdiag if r['measured_ticks']==8192],key=lambda r:(r['format'],r['arm'])):
 t=r['totals'];lines.append(f"| {r['format']} | {r['arm']} | {t['periodic_candidates']} | {t['known_zero_candidates']} | {t['skipped']} | {t['admissions']} | {r['all_admissions_per_measured_tick']:.3f} |")
lines+=['','Across the aligned full windows, 4FFA skipped1,946 candidates and8FFA skipped140,264, yet every arm admitted8192 jobs in8192 measured ticks. Freed slots were filled under the old one-job policy. Candidate pruning therefore shortened/reordered the eligible schedule without lowering the job count here. Field cost mix and changed gameplay can still affect CPU; this experiment does not establish zero benefit on other maps/epochs. OFF/ON candidate populations differ after gameplay diverges and must not be treated as identical opportunities. The retained first1024-tick4FFA diagnostic had no zero-class candidates, but that does not describe its full8192-tick window.','', 'Per-family movement/planning denominators, work admissions/publications, lazy prepared/resume work and final per-team8-bin counts are retained in SUMMARY.json. Immutable reference controls quantify research bookkeeping/selector overhead; negative or small differences may reflect local noise/order/load. No additive causal percentage is inferred.','', 'All64 CPU arm/state repeats are deterministic; off/default references match immutable binaries. Eight observer-ON runs match corresponding OFF final checksums. Asset tree and dependency hashes were unchanged before/after. Qualification and commands are retained alongside this report.']
(B/'README.md').write_text('\n'.join(lines)+'\n')
dump('ANALYSIS-COMPLETE.json',dict(status='PASS',cpu_runs=len(cpu),quality_runs=len(quality),class_diagnostic_runs=len(classdiag),full_window_class_runs=sum(r['measured_ticks']==8192 for r in classdiag)))
