import csv,json,statistics as st,sys
from pathlib import Path
sys.path.insert(0,str(Path('test').resolve()))
from benchmark_resource_growth import interval
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('artifacts/resource-growth')
rows=json.loads((root/'component.json').read_text())['samples']
summary=[]
for size in (128,256,512):
 for scenario in ('sparse','dense','saturated','harvested','blocked','multi','disabled'):
  groups={v:{r['repeat']:r for r in rows if r['size']==size and r['scenario']==scenario and r['repeat']>=0 and r['variant']==v} for v in range(4)}
  for v in range(4):
   data=list(groups[v].values())
   row={'size':size,'scenario':scenario,'variant':v}
   for key in data[0]:
    if key not in ('size','scenario','repeat','variant'): row[key]=st.median(r[key] for r in data)
   for label,ref in [('legacy',0),('owner',2)]:
    pairs=[r['elapsed_ns']/groups[ref][rep]['elapsed_ns'] for rep,r in groups[v].items()]
    row[label+'_ratio']=st.median(pairs)
    row[label+'_ci']=interval(pairs)
    row[label+'_delta_ms']=st.median((r['elapsed_ns']-groups[ref][rep]['elapsed_ns'])/1e6 for rep,r in groups[v].items())
   summary.append(row)
(root/'component-summary.json').write_text(json.dumps(summary,indent=2))
ecology=json.loads((root/'ecology.json').read_text())['samples']
eco={}
for policy in ('reserve','deplete'):
 for key in ('food','deposits','harvested','depleted','seeded','replenished'):
  old={r['seed']:r[key] for r in ecology if r['variant']==0 and r['harvest_policy']==policy}
  new={r['seed']:r[key] for r in ecology if r['variant']==1 and r['harvest_policy']==policy}
  ratios=[new[s]/old[s] for s in old if old[s]]
  eco[policy+'/'+key]={'legacy_mean':st.mean(old.values()),'delayed_mean':st.mean(new.values()),'median_paired_ratio':st.median(ratios) if ratios else None,'ratio_ci':interval(ratios) if ratios else None,'paired_delta_range':[min(new[s]-old[s] for s in old),max(new[s]-old[s] for s in old)]}
(root/'ecology-summary.json').write_text(json.dumps(eco,indent=2))
lines=['# Resource growth measurements','','All ratios below are candidate/control elapsed time: greater than 1 means slower. Component rows cover 64 growth passes; engine rows cover 256 ticks. One warm-up and ten rotated paired measurements. Bootstrap intervals describe these runs on this shared Linux host; they do not establish platform-independent performance.','','## Component measurements','','| Size | Scenario | Legacy ms | Immediate split ms | Delayed owner ms | Delayed shared ms | Shared/legacy ratio (95% CI) | Shared/owner ratio (95% CI) |','|---:|---|---:|---:|---:|---:|---|---|']
for size in (128,256,512):
 for scenario in ('sparse','dense','saturated','harvested','blocked','multi','disabled'):
  rs=[r for r in summary if r['size']==size and r['scenario']==scenario]
  cells=[str(size),scenario]+[f"{r['elapsed_ns']/1e6:.3f}" for r in rs]
  for ref in ('legacy','owner'):
   r=rs[3]; cells.append(f"{r[ref+'_ratio']:.2f} ({r[ref+'_ci'][0]:.2f}–{r[ref+'_ci'][1]:.2f})")
  lines.append('| '+' | '.join(cells)+' |')
lines += ['','The legacy component path runs the retained immediate algorithm in the candidate binary; the engine comparison below uses the actual preserved master executable. New/old trajectories intentionally differ. All attempts in the new kernel read one immutable snapshot, and delayed cases leave the final eight batches un-published after draining computation. Disabled-growth ratios are dominated by sub-microsecond bookkeeping and should not be interpreted as useful speed changes.','','## Ecology: 20 paired seeds, 512 ticks','','This controlled 128² uniform-crop fixture harvests every eight ticks, either preserving a one-unit reserve or allowing removal. It measures replenishment, spread, depletion and sustained harvest; full-match balance still requires play assessment. Both algorithms run in the candidate executable.','','| Measure | Legacy mean | Delayed mean | Median paired ratio (95% CI) |','|---|---:|---:|---|']
for k,r in eco.items():
 ratio='n/a (zero baseline)' if r['ratio_ci'] is None else f"{r['median_paired_ratio']:.4f} ({r['ratio_ci'][0]:.4f}–{r['ratio_ci'][1]:.4f})"
 lines.append(f"| {k} | {r['legacy_mean']:.2f} | {r['delayed_mean']:.2f} | {ratio} |")
p=root/'engine-timing/summary.json'
if p.exists():
 engine=json.loads(p.read_text())
 lines += ['','## Whole engine: default delay 8, executor size 4','','Every other delay/thread combination is retained in `engine-timing/summary.json`. Owner placement uses the same executor size as shared placement, keeping AI/gradient settings fixed.','','| Scenario | Legacy run ms | Delayed owner run ms | Shared run ms | Shared/legacy ratio (95% CI), delta ms | Shared/owner ratio (95% CI), delta ms |','|---|---:|---:|---:|---|---|']
 for scenario,vs in engine.items():
  a,b,c=vs['legacy-t4'],vs['d8t4-owner'],vs['d8t4-shared']
  cells=[scenario]+[f"{v['run_s']*1000:.2f}" for v in (a,b,c)]
  for ref in ('legacy-t4','d8t4-owner'):
   r=c[ref]; cells.append(f"{r['run_time_ratio']:.3f} ({r['ratio_95pct_ci'][0]:.3f}–{r['ratio_95pct_ci'][1]:.3f}), {r['run_delta_ms']:+.2f}")
  lines.append('| '+' | '.join(cells)+' |')
 lines += ['','`measurements.jsonl` retains process wall/CPU/RSS, exact command lines, input/binary hashes, load averages, full snapshot/AI/gradient metrics, growth counters, queue/compute/publication/join times, pending-buffer peaks, and candidate tick percentiles. The original executable supplies a tick histogram, not exact percentiles. Growth wait time combines publication waits and final draining; it is not a pure deadline-stall counter.','']
p=root/'engine-default-final/summary.json'
if p.exists():
 engine=json.loads(p.read_text())
 lines += ['','## Refreshed default timings on final integration revision','','The full matrix above measured the feature before the master abort-session fix. This separate campaign remeasures delay 8/thread count 4 on the final integrated executable. The fix changes the failed-session path; all fourteen final integration traces match the earlier feature revision. Both campaigns are retained.','','| Scenario | Legacy run ms | Owner run ms | Shared run ms | Shared/legacy ratio (95% CI), delta ms | Shared/owner ratio (95% CI), delta ms |','|---|---:|---:|---:|---|---|']
 for scenario,vs in engine.items():
  a,b,c=vs['legacy-t4'],vs['d8t4-owner'],vs['d8t4-shared']
  cells=[scenario]+[f"{v['run_s']*1000:.2f}" for v in (a,b,c)]
  for ref in ('legacy-t4','d8t4-owner'):
   r=c[ref];cells.append(f"{r['run_time_ratio']:.3f} ({r['ratio_95pct_ci'][0]:.3f}–{r['ratio_95pct_ci'][1]:.3f}), {r['run_delta_ms']:+.2f}")
  lines.append('| '+' | '.join(cells)+' |')
 lines += ['','| Scenario | Shared process wall ms | Process CPU ms | Peak RSS MiB | Tick median / p95 / p99 µs | Compute / queue / joins / publication ms |','|---|---:|---:|---:|---|---|']
 for scenario,vs in engine.items():
  c=vs['d8t4-shared']; ps=c['tick_percentiles_ns'];g=c['growth']
  pct=' / '.join(f"{ps[k]/1000:.1f}" for k in ('tick_p50_ns','tick_p95_ns','tick_p99_ns'))
  costs=' / '.join(f"{g[k]/1e6:.2f}" for k in ('growth_computeNs','growth_queueNs','growth_waitNs','growth_publicationNs'))
  lines.append(f"| {scenario} | {c['wall_s']*1000:.2f} | {c['cpu_s']*1000:.2f} | {c['peak_rss_bytes']/2**20:.2f} | {pct} | {costs} |")
 lines += ['','The disabled-growth owner/shared comparison submits no growth jobs. Its variability is a negative control for host/scheduling noise; small apparent gains or regressions elsewhere cannot be treated as definitive. No overall end-to-end speedup is established. Shared placement remains the requested default.','','Snapshot copying, proposal retention and publication add substantial standalone cost. Shared execution reduces some delayed-owner costs, but does not produce a consistent engine win over the original algorithm in these workloads. A quiet, isolated run is still needed to qualify small engine differences.']
p=root/'engine-controlled/summary.json'
if p.exists():
 engine=json.loads(p.read_text())
 lines += ['','## Controlled full-engine scenarios','','These additional starting saves were emitted by the fixture generator compiled against the baseline. They preserve buildings/teams but replace the field with uniform crops: sparse/blocked 128², dense/low-stock active-AI/disabled 256², saturated/multi-material 512². Each comparison uses delay 8, four executor slots, one warm-up and ten paired measured runs of 256 ticks. Fourteen separate owner/shared checksum runs passed. The low-stock case retains active Nicowar/Warrush controllers; other cases retain idle controllers.','','| Scenario | Legacy run ms | Owner run ms | Shared run ms | Shared/legacy ratio (95% CI), delta ms | Shared/owner ratio (95% CI), delta ms |','|---|---:|---:|---:|---|---|']
 for scenario,vs in engine.items():
  a,b,c=vs['legacy-t4'],vs['d8t4-owner'],vs['d8t4-shared']
  cells=[scenario]+[f"{v['run_s']*1000:.2f}" for v in (a,b,c)]
  for ref in ('legacy-t4','d8t4-owner'):
   r=c[ref];cells.append(f"{r['run_time_ratio']:.3f} ({r['ratio_95pct_ci'][0]:.3f}–{r['ratio_95pct_ci'][1]:.3f}), {r['run_delta_ms']:+.2f}")
  lines.append('| '+' | '.join(cells)+' |')
 lines += ['','All controlled shared/legacy intervals include parity. Dense, low-stock active-AI, multi-material and saturated fields show lower shared costs than delayed owner execution, but that does not establish an improvement over legacy growth. This directly illustrates the split overhead consuming the recovered parallel benefit in these runs.']
(root/'performance.md').write_text('\n'.join(lines)+'\n')
