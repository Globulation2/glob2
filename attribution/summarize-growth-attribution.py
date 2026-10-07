import json,statistics as S,sys
from pathlib import Path
sys.path.insert(0,'test')
from benchmark_resource_growth import interval
root=Path('artifacts/resource-growth/attribution')
x=json.loads((root/'existing-capture.json').read_text())['samples'];summary={}
lines=['# Resource growth: shared-snapshot cost attribution','',
'This follow-up corrects the attribution in the original report. Its standalone component benchmark charged all snapshot capture to growth while the legacy control captured none. That measures adopting snapshots, not growth\'s incremental cost when AI/gradients already capture them. The original full-engine results remain valid observations; the standalone ratios do not explain their overhead.','',
'## Controlled existing-capture experiment','',
'All four variants now capture `SimulationSnapshot::All` once per observation. New growth consumes a projection of that existing handle. Same candidate binary, seed 713, initial fixture and 64 passes; one warm-up plus ten rotated measured repeats for 21 scenarios. Initial capture is warmed outside timing. Timed capture includes ongoing refresh. This benchmark runs no AI jobs and retains no artificial AI leases, so it isolates an existing capture service, not real AI contention. The old and new growth algorithms still follow different trajectories. All variants have the candidate\'s 16-byte resource cell, so this comparison excludes the old-to-new cell-size penalty.','',
'| Size | Scenario | Legacy+capture ms | Immediate split ms | Delayed owner ms | Delayed shared ms | Immediate/legacy (95% CI) | Shared/legacy (95% CI) |','|---:|---|---:|---:|---:|---:|---|---|']
for size in [128,256,512]:
 for sc in ['sparse','dense','saturated','harvested','blocked','multi','disabled']:
  rs={v:{r['repeat']:r for r in x if r['size']==size and r['scenario']==sc and r['variant']==v and r['repeat']>=0} for v in range(4)}
  entries={}
  for v,r in rs.items():
   entry={k:S.median(a[k] for a in r.values()) for k in r[0] if k not in ['scenario','size','variant','repeat']}
   ratios=[a['elapsed_ns']/rs[0][i]['elapsed_ns'] for i,a in r.items()]
   entry['ratio']=S.median(ratios);entry['ci']=interval(ratios)
   entry['delta_ms']=S.median((a['elapsed_ns']-rs[0][i]['elapsed_ns'])/1e6 for i,a in r.items())
   entries[v]=entry
  summary[f'{size}/{sc}']=entries
  fmt=lambda v:f"{entries[v]['ratio']:.3f} ({entries[v]['ci'][0]:.3f}–{entries[v]['ci'][1]:.3f})"
  lines.append(f'| {size} | {sc} | '+ ' | '.join(f"{entries[v]['elapsed_ns']/1e6:.2f}" for v in range(4))+f' | {fmt(1)} | {fmt(3)} |')
lines+=['','Enabled immediate-split paired median ratios span 0.956–1.618, rather than the earlier standalone 1.69–6.57. Most are approximately 1.00–1.29; sparse 512² is the 1.62 outlier. Disabled cases are a bookkeeping control. These runs occurred at a different host load from the earlier campaign: compare variants within this campaign, not their absolute milliseconds against earlier runs.','',
'## Where the time goes with existing capture','',
'Each entry below is a median over ten measurements, milliseconds per 64 passes. Medians do not necessarily sum. Compute overlaps capture in shared execution; joins overlap compute and must not be added. Timers measure elapsed wall time, including possible descheduling, not per-stage CPU time.','',
'| Scenario | Variant | Total ms | Capture ms | Compute ms | Publish ms | Join ms | Tracked copy MiB | Snapshot peak MiB |','|---|---|---:|---:|---:|---:|---:|---:|---:|']
for sc in ['256/dense','512/multi','512/saturated','512/sparse']:
 for v,name in enumerate(['legacy','immediate','delayed owner','delayed shared']):
  a=summary[sc][v]
  lines.append(f'| {sc} | {name} | '+ ' | '.join(f'{a[k]/1e6:.2f}' for k in ['elapsed_ns','capture_ns','compute_ns','publication_ns','wait_ns'])+f" | {a['copied_bytes']/2**20:.2f} | {a['snapshot_peak_bytes']/2**20:.2f} |")
lines+=['','For dense 256², legacy non-capture work is approximately 2.58 ms; immediate computation plus publication is approximately 2.65 ms. Capture itself rises from 3.60 to 4.05 ms. For multi-material 512², legacy non-capture work is approximately 29.38 ms, versus 35.60 ms compute plus publication; capture rises from 104.95 to 127.51 ms. Subtraction of independent medians is illustrative, not an exact additive accounting.','',
'Delayed owner execution retains snapshots until its deadline computation; buffers therefore accumulate more changed chunks before reuse. In multi-material 512², tracked copying rises from 177.18 MiB immediate to 258.19 MiB delayed owner, and snapshot peak from 20.08 to 44.08 MiB. Shared execution releases leases earlier and reduces both (see table). However, small jobs and concurrent capture/compute can still cost more elapsed time; the present counters cannot partition that remainder into executor synchronization, cache/memory contention and host scheduling.','',
'## Actual engine: incremental shared capture','',
'Reanalysis of the previously published final-engine campaigns, holding AI/gradient flags, executor size 4, starting save and 256 ticks fixed; growth delay 8. Every baseline and candidate scenario reports 257 captures. Values are baseline/shared medians; deltas and confidence intervals are paired by repeat. Trajectory changes prevent a perfectly causal attribution to growth alone.','',
'| Campaign/scenario | Capture old/new ms | Paired capture delta ms (95% CI) | Tracked copy old/new MiB | Paired simulation CPU delta ms |','|---|---|---|---|---:|']
e=json.loads((root/'engine-breakdown.json').read_text())
for c,scenarios in e.items():
 for sc,t in scenarios.items():
  a=t['d8t4-shared'];b=t['legacy-t4'];d=a['paired_legacy_deltas'];ci=d['capture_ms']['ci']
  lines.append(f"| {c}/{sc} | {b['capture_ms']:.2f} / {a['capture_ms']:.2f} | {d['capture_ms']['median']:+.2f} ({ci[0]:+.2f}–{ci[1]:+.2f}) | {b['copied_MiB']:.2f} / {a['copied_MiB']:.2f} | {d['run_cpu_ms']['median']:+.2f} |")
lines+=['',
'CPU here uses `benchmark_run_cpu_ns`, which isolates the simulation run across threads, rather than whole-process CPU including setup/save. Stage wall-time deltas cannot be subtracted from CPU deltas as an additive breakdown. The disabled control illustrates host variance.','',
'## What is established, and what remains unmeasured','',
'- Growth shares the existing capture. No additional per-tick capture occurred in these engine runs. Static components can be reused; they are not blindly copied every tick.',
'- ResourceCell grew from 12 to 16 bytes for the deposit incarnation: 33% more resource-cell bytes for the same copied cells. All existing resource snapshot consumers pay that layout cost. This does not imply 33% more total capture time or prove how much of the observed delta it caused.',
'- Resource dirty tracking copies 16×16-cell chunks. Different growth/stock updates and older reusable buffers change copied volume. Owner-delayed retention visibly increases volume in the controlled benchmark; actual AI/gradient leases can already retain those buffers, reducing the incremental effect.',
'- Multi-material stock sidecars are copied in full when Resources refreshes. Inspection also found that the existing bytesCopied counter excludes this sidecar memcpy. All byte tables therefore report tracked copying, not total memory traffic. Capture time includes the sidecar work. This accounting gap existed before this change.',
'- Compute/proposal emission and publication have measured costs, but legacy growth already performed ecology and mutation work. Their entire cost is not overhead. In immediate dense cases the compute-plus-publish cost is close to legacy non-capture work.',
'- Whole-engine deadline/final-drain joins in dense/multi/saturated shared cases are only about 0.05–0.08 ms per 256 ticks. Large queue-residence counters measure elapsed waiting while other work proceeds, not CPU usage or equivalent owner stalls.',
'- Separating incarnation storage from frequently copied stock cells is a concrete optimization candidate. Improving buffer reuse and copy granularity is another. Neither has been implemented or credited with a predicted speedup.',
'- A definitive fine-grained CPU attribution still needs per-component CPU profiling and same-trajectory controls. This experiment does not establish an end-to-end win, nor that the snapshot architecture is inherently unviable.','',
'## Reproduction','',
'From the implementation checkout, with its recorded release dependencies:','',
'```sh',
'GLOB2_GROWTH_EXISTING_CAPTURE=1 GLOB2_GROWTH_BENCHMARK_OUTPUT=$PWD/artifacts/resource-growth/attribution/existing-capture.json LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib python3 test/run_tests.py --binary engine --filter "ResourceGrowthBenchmark/paired*" --tag benchmark --no-display -j1 --timeout 1800 --junit artifacts/resource-growth/attribution/existing-capture.xml --artifacts artifacts/resource-growth/attribution/test',
'```','',
'`engine-breakdown.json` retains all engine metrics/deltas. `existing-capture.json` retains all 924 controlled samples. The two summarizer scripts accompany this report. This update changes only benchmark tooling and documentation; no production simulation behavior changes.']
(root/'existing-capture-summary.json').write_text(json.dumps(summary,indent=2))
(root/'README.md').write_text('\n'.join(lines)+'\n')
