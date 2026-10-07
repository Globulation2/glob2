from pathlib import Path
import json,statistics as S,random
r=Path('artifacts/resource-growth/deeper');rows=[json.loads(l) for l in (r/'stages/measurements.jsonl').read_text().splitlines()];rows=[x for x in rows if x['repeat']>=0]
def ci(xs):
 g=random.Random(713);a=sorted(S.mean(g.choices(xs,k=len(xs))) for _ in range(2000));return [a[49],a[1949]]
def stage(x):
 a=[v/1e6 for v in x['stage_cpu']['cpu_ns']];total=x['result']['benchmark_run_cpu_ns']/1e6
 return [a[0],sum(a[1:5]),sum(a[5:7]),a[7],total-sum(a),total]
lines=['# Where resource growth adds CPU work','',
'The pure growth kernel reads live and snapshot inputs at essentially equal speed. The regression belongs to the surrounding snapshot/publication architecture and, in some workloads, existing gradient and AI work. Sharing a capture does not make its increased payload free.','',
'## CPU accounting','',
'Exclusive thread CPU time, milliseconds per 1,024 ticks, mean of five paired runs. Positive numbers are additional CPU in the current shared pipeline versus legacy. These rows add up (within rounding). CPU time is summed across threads; it is not elapsed tick time. Negative deltas are reductions, not costs assigned away.','',
'| Scenario | Snapshot capture | Growth calculation + publication + submission, net of old growth | Gradients | AI decision/observation | Other engine work | Total extra CPU (95% CI) |','|---|---:|---:|---:|---:|---:|---:|']
data={}
for sc in sorted({x['scenario'] for x in rows}):
 vs={v:{x['repeat']:x for x in rows if x['scenario']==sc and x['variant']==v} for v in ['legacy','current','layout']};d=[[stage(vs['current'][i])[k]-stage(vs['legacy'][i])[k] for i in vs['legacy']] for k in range(6)];m=[S.mean(x) for x in d];lo,hi=ci(d[-1]);lines.append('| '+sc+' | '+' | '.join(f'{v:+.1f}' for v in m[:-1])+f' | {m[-1]:+.1f} ({lo:+.1f} to {hi:+.1f}) |');data[sc]=vs
lines+=['','The unassigned column is measured process CPU minus the instrumented scopes. It includes other engine work and timing overhead; it is not a proven single cause. The disabled-growth delta is statistically inconclusive here. This instrumented campaign does not reproduce the previous uninstrumented disabled-control regression reliably, so a specific causal explanation for that regression remains unproven.','',
'## Inside the growth cost','',
'Absolute mean CPU milliseconds per 1,024 ticks. Calculation alone is often cheaper than the old combined pass; publication and scheduling consume that saving. Old/new ecological trajectories differ, so this is engine workload accounting, not an identical-work algorithm comparison.','',
'| Scenario | Old combined growth | New calculation | New publication | New preparation/submission |','|---|---:|---:|---:|---:|']
for sc,vs in data.items():
 vals=[S.mean(x['stage_cpu']['cpu_ns'][i]/1e6 for x in vs[v].values()) for v,i in [('legacy',1),('current',2),('current',3),('current',4)]];lines.append('| '+sc+' | '+' | '.join(f'{x:.1f}' for x in vals)+' |')
lines+=['','Publication rechecks current habitat/permissions/occupancy, validates deposit identity, applies capped increments through authoritative setters, updates growth statistics and change tracking. It scans proposals only. Scheduling CPU is about 1–2 ms per 1,024 active-growth ticks in these scenarios; it is not the dominant added cost.','',
'## Shared capture payload','',
'All variants perform 1,025 captures. Mean total copied MB (decimal), including initial capture. These totals are already exported by the engine; they are not estimated from wall time.','',
'| Scenario | Legacy copied MB | Current copied MB | Sidecar-layout copied MB |','|---|---:|---:|---:|']
for sc,vs in data.items():lines.append('| '+sc+' | '+' | '.join(f"{S.mean(x['result']['ai_pipeline']['bytes_copied']/1e6 for x in vs[v].values()):.1f}" for v in ['legacy','current','layout'])+' |')
lines+=['','The per-cell resource record grew from 12 to 16 bytes to carry deposit incarnation. The snapshot refresh copies this wider record, using changed 16×16 chunks and row copies. Dirty history/buffer availability and changed growth trajectories also affect copied bytes. Additional Growth/Rules requirements and leases exist even though the capture boundary is shared. The previous matching-window hardware profiles locate substantial cycles in refresh copying; the CPU scopes here measure the entire capture, not only memcpy. No bandwidth-only attribution is claimed. Gradient scopes identify where CPU is spent, but old/new resource trajectories and cache behavior both change; this accounting alone cannot distinguish additional gradient work from higher cost per operation.','',
'## Identical-kernel control','',
'The added opt-in benchmark compares `ResourceGrowth::calculate(map.stateView(), ...)` against the same function and seeds using a captured view of the same unchanged world. It verifies ordered proposals and RNG continuation for 32 seeds across 15 size/scenario pairs (128, 256 and 512 square; sparse, dense, saturated, blocked, multi-material). Snapshot acquisition, RNG construction and fixture setup are outside the timer. There is a warm-up and 20 paired rotated measurements, 128 calls each. Output buffers are preallocated.','',
'| Size | Scenario | Snapshot/live paired median time ratio (95% CI) |','|---:|---|---:|']
import sys
sys.path.insert(0,'test');from benchmark_resource_growth import interval
ks=json.loads((r/'kernel.json').read_text())['samples']
for size,sc in sorted({(x['size'],x['scenario']) for x in ks}):
 a={x['repeat']:{} for x in ks if x['size']==size and x['scenario']==sc and x['repeat']>=0}
 for x in ks:
  if x['size']==size and x['scenario']==sc and x['repeat']>=0:a[x['repeat']][x['snapshot']]=x['elapsed_ns']
 ratios=[x[True]/x[False] for x in a.values()];lo,hi=interval(ratios);lines.append(f'| {size} | {sc} | {S.median(ratios):.3f} ({lo:.3f}–{hi:.3f}) |')
lines+=['','## Layout-only experiment','',
'The temporary sidecar patch moves incarnation counters into a separate array and returns frequently scanned resource records to 12 bytes. It preserves growth decisions, save bytes and deadlines. It copies counters using the same resource dirty chunks, retaining 16 bytes of resource-plus-incarnation payload per cell. This deliberately does not optimize counter refresh. The patch is evidence only; production sources were restored and the restored executable exactly matches the original SHA-256.','',
'Uninstrumented comparison: one warm-up plus ten paired rotated/reversed measured runs, each 1,024 ticks, current shared pipeline versus sidecar shared pipeline. Positive throughput means the sidecar is faster.','',
'| Scenario | Sidecar throughput change (95% CI) |','|---|---:|']
summary=json.loads((r/'summary.json').read_text())
for sc,a in summary['layout-timing'].items():
 d=a['layout-vs-current']['throughput'];lo,hi=d['ci'];lines.append(f"| {sc} | {100*d['median']:+.1f}% ({100*lo:+.1f} to {100*hi:+.1f}%) |")
lines+=['','The sidecar is not a demonstrated general throughput fix. Its extra copy stream can increase capture CPU while improving some resource scans. A useful next optimization would track incarnation dirtiness independently, since stock changes do not alter identity, and measure resource-buffer refresh separately. That optimization has not been implemented or measured here.','',
'## Method, reproducibility and limits','',
'- Same AMD Threadripper 2950X/Linux x86-64/GCC 15.2 release build as the previous profiling report. Existing cpuset wrappers reserved cores 0–3 and siblings 16–19; engine affinity 0–3, performance governor. Audits record restoration. Core reservation does not isolate package memory bandwidth, power or every kernel activity.',
'- Stage runs: 72 total, including warm-ups; 5 measured pairs per scenario and 3 variants, rotated/reversed. Layout timing: 88 total. Kernel: 630 rows including warm-ups. Fixed fixture hashes, binary hashes, commands, CPU affinity, host activity and frequencies are in metadata/raw rows.',
'- Profiling-only source copies use CLOCK_THREAD_CPUTIME_ID and subtract nested scopes. Eight coarse scopes cover capture, old growth, calculate, publication, submission, gradient seed, gradient propagation and AI callbacks. The simulation is drained before ending the measurement. Timer/atomic overhead is present; use uninstrumented results for throughput. Confidence intervals are deterministic percentile bootstrap, 2,000 resamples; CPU table uses paired means, ratio tables paired medians. Five CPU pairs give limited uncertainty estimates.',
'- Current and sidecar final heavy checksums and sampled/proposed/accepted/stock-addition counters match in every run. Sidecar passes all eight focused ResourceGrowth tests, including pending-work save/load and deadline cases. This is not a new cross-platform or per-tick determinism qualification. No experimental production code is committed.',
'- Baseline revision d42d3e512; current production executable SHA-256 1ada564b4abd014b3dd354753e4812c8fcfaf08d835ae139e174b7e944cf2fcf. Test/docs-only source changes extend 14a5d8fe9. Master fetched at 4cb2ac058; the previously documented draft-PR integration/golden conflicts remain outstanding. This investigation does not resolve the whole implementation acceptance plan.',
'- Included: raw measurements, kernel equivalence test log/XML, stage source copies/header/build commands, sidecar patch, focused tests, wrapper audits, runner/report scripts. Prior hardware counters and uninstrumented old/new results remain under `../profiling/`. Initial kernel trials used an incomplete live view and failed; the final passing case uses stateView. Failed exploratory builds are not validation claims.']
(r/'README.md').write_text('\n'.join(lines)+'\n')
