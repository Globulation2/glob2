import collections,csv,hashlib,json,pathlib,statistics
D=pathlib.Path(__file__).resolve().parent;state=json.loads((D/'STATUS.json').read_text());assert state['status']=='COMPLETE';P=json.loads((D/'PLAN.json').read_text());assert json.loads((D/'PARITY.json').read_text())['status']=='PASS'
quality_keys=['extra_cost','false_unreachable','false_reachable','invalid_goal','missed_move','escape_failure','stall','guard_balance']
def load(out,name):
 with (out/name).open() as f:rows=list(csv.DictReader(f))
 families={'0':'materials','1':'markets','2':'guard','3':'clear','4':'forbidden'}
 for row in rows:
  if 'family' in row:row['family']=families.get(row['family'],row['family'])
 return rows
def movement(rows):
 totals={k:sum(int(r[k]) for r in rows) for k in quality_keys};n=len(rows)
 return dict(observations=n,totals=totals,per_observation={k:v/n if n else None for k,v in totals.items()})
def quality(row):
 out=pathlib.Path(row['output']);moves=load(out,'gradient-quality.csv');planning=load(out,'gradient-planning-quality.csv');work=load(out,'gradient-work.csv');families=sorted({r['family'] for r in moves+planning+work})
 return dict(group=row['group'],arm=row['arm'],movement=movement(moves),planning=dict(observations=len(planning),false_unreachable=sum(int(r['false_unreachable']) for r in planning),false_reachable=sum(int(r['false_reachable']) for r in planning)),work=dict(events=dict(collections.Counter(r['event'] for r in work)),event_reasons=dict(collections.Counter(r['event']+':'+r['reason'] for r in work))),by_family={f:dict(movement=movement([r for r in moves if r['family']==f]),planning_observations=sum(r['family']==f for r in planning),work_events=dict(collections.Counter(r['event'] for r in work if r['family']==f)),admitted_field_cells=sum(int(r['field_cells']) for r in work if r['family']==f and r['event']=='admission'),lazy_settled_cost_delta=sum(int(r['settled_cost_after'])-int(r['settled_cost_before']) for r in work if r['family']==f and r['event'] in {'prepared_search','search_resume'})) for f in families},gradient_jobs=row['result']['gradient_jobs'],building_gradient_jobs=row['result']['building_gradient_jobs'])
metrics=['benchmark_cpu_ms_per_tick','process_cpu_ms_per_tick','total_cpu_seconds','wall_seconds','wall_tps'];timing=[]
for m in P['maps']:
 arms={}
 for arm in P['arms']:
  rows=[r for r in state['rows'] if r['group']==m['group'] and r['arm']==arm and r['kind']=='off'];assert len(rows)==4
  arms[arm]={k:dict(mean=statistics.mean(r[k] for r in rows),median=statistics.median(r[k] for r in rows),minimum=min(r[k] for r in rows),maximum=max(r[k] for r in rows),repeats=[r[k] for r in rows]) for k in metrics}
 timing.append(dict(group=m['group'],format=m['format'],arms=arms,change_pct_gen005_vs_gen004={k:(arms['gen005'][k]['mean']/arms['gen004'][k]['mean']-1)*100 for k in metrics}))
off=[r for r in state['rows'] if r['kind']=='off'];aggregate={}
for arm in P['arms']:
 rr=[r for r in off if r['arm']==arm];n=sum(r['ticks'] for r in rr);wall=sum(r['wall_seconds'] for r in rr);cpu=sum(r['result']['benchmark_run_cpu_ns']/1e9 for r in rr)
 aggregate[arm]=dict(ticks=n,total_wall_seconds=wall,total_process_cpu_seconds=sum(r['total_cpu_seconds'] for r in rr),total_simulation_cpu_seconds=cpu,pooled_wall_tps=n/wall,simulation_cpu_ms_per_tick=cpu*1000/n)
aggregate['change_pct']={k:(aggregate['gen005'][k]/aggregate['gen004'][k]-1)*100 for k in ['total_wall_seconds','total_process_cpu_seconds','total_simulation_cpu_seconds','pooled_wall_tps','simulation_cpu_ms_per_tick']}
aggregate['paired_repeat_cpu_change_pct']=[]
for rep in range(4):
 values={a:sum(r['result']['benchmark_run_cpu_ns'] for r in off if r['arm']==a and r['repeat']==rep) for a in P['arms']};aggregate['paired_repeat_cpu_change_pct'].append((values['gen005']/values['gen004']-1)*100)
quality_rows=[quality(r) for r in state['rows'] if r['kind']=='on'];quality_pooled={}
for arm in P['arms']:
 rr=[r for r in quality_rows if r['arm']==arm];n=sum(r['movement']['observations'] for r in rr);tot={k:sum(r['movement']['totals'][k] for r in rr) for k in quality_keys};pn=sum(r['planning']['observations'] for r in rr)
 quality_pooled[arm]=dict(movement_observations=n,movement_totals=tot,movement_per_observation={k:v/n if n else None for k,v in tot.items()},planning_observations=pn,planning_false_unreachable=sum(r['planning']['false_unreachable'] for r in rr),planning_false_reachable=sum(r['planning']['false_reachable'] for r in rr),shared_gradient_jobs=sum(r['gradient_jobs'] for r in rr),building_gradient_jobs=sum(r['building_gradient_jobs'] for r in rr),lazy_search_resumes=sum(r['by_family'].get('lazy_building',{}).get('work_events',{}).get('search_resume',0) for r in rr),lazy_settled_cost_delta=sum(r['by_family'].get('lazy_building',{}).get('lazy_settled_cost_delta',0) for r in rr))
summary=dict(status='COMPLETE',aggregate=aggregate,quality_pooled=quality_pooled,plan_sha256=hashlib.sha256((D/'PLAN.json').read_bytes()).hexdigest(),timing=timing,quality=quality_rows,limits=P['limits'],uncertainty=P['analysis']['uncertainty'],parity='Within-arm four OFF repeats and ON initialChecksum/finalChecksum/ticks exact; existing qualification supplies per-tick parity; cross-arm equality not required',population='Three previously examined warm states, not a new test-set estimate',timing_scope='Clean OFF only: probe, work, compute and checksum tracing disabled; engine measured simulation CPU; whole-process timing includes setup/teardown')
(D/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n')
lines=['# Gen005 vs gen004 warm-state comparison','',summary['population']+'. Four interleaved OFF repeats per state, then one independent ON diagnostic per arm. Positive CPU deltas mean gen005 used more CPU.','', '|State|Format|Gen004 CPU ms/tick|Gen005 CPU ms/tick|Change|Process CPU change|Wall TPS change|','|---|---|---:|---:|---:|---:|---:|']
for r in timing:
 a=r['arms'];c=r['change_pct_gen005_vs_gen004'];lines.append(f"|{r['group']}|{r['format']}|{a['gen004']['benchmark_cpu_ms_per_tick']['mean']:.6f}|{a['gen005']['benchmark_cpu_ms_per_tick']['mean']:.6f}|{c['benchmark_cpu_ms_per_tick']:+.2f}%|{c['total_cpu_seconds']:+.2f}%|{c['wall_tps']:+.2f}%|")
lines+=['',f"Pooled clean OFF ({aggregate['gen004']['ticks']} ticks per arm): simulation CPU {aggregate['gen004']['total_simulation_cpu_seconds']:.6f}→{aggregate['gen005']['total_simulation_cpu_seconds']:.6f}s; process CPU {aggregate['gen004']['total_process_cpu_seconds']:.6f}→{aggregate['gen005']['total_process_cpu_seconds']:.6f}s; wall {aggregate['gen004']['total_wall_seconds']:.6f}→{aggregate['gen005']['total_wall_seconds']:.6f}s; pooled throughput {aggregate['gen004']['pooled_wall_tps']:.2f}→{aggregate['gen005']['pooled_wall_tps']:.2f} ticks/s (sum ticks divided by sum wall).", '', 'All repeat ranges, total CPU, wall time, TPS, per-family work and distinct movement/planning outcomes are retained in SUMMARY.json. ON timings are excluded from CPU comparisons.','',*['- '+v for v in P['limits']], '',summary['uncertainty']+'.']
(D/'README.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(timing,indent=2))
