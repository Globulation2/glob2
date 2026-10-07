# Where resource growth adds CPU work

The pure growth kernel reads live and snapshot inputs at essentially equal speed. The regression belongs to the surrounding snapshot/publication architecture and, in some workloads, existing gradient and AI work. Sharing a capture does not make its increased payload free.

## CPU accounting

Exclusive thread CPU time, milliseconds per 1,024 ticks, mean of five paired runs. Positive numbers are additional CPU in the current shared pipeline versus legacy. These rows add up (within rounding). CPU time is summed across threads; it is not elapsed tick time. Negative deltas are reductions, not costs assigned away.

| Scenario | Snapshot capture | Growth calculation + publication + submission, net of old growth | Gradients | AI decision/observation | Other engine work | Total extra CPU (95% CI) |
|---|---:|---:|---:|---:|---:|---:|
| ai512 | +84.9 | +25.8 | -21.6 | +16.9 | +14.7 | +120.8 (+40.1 to +190.0) |
| dense | +72.7 | +18.5 | -8.5 | +26.4 | +12.9 | +122.1 (+57.9 to +185.5) |
| disabled512 | +0.3 | -0.1 | -5.5 | +6.5 | +5.4 | +6.6 (-121.7 to +90.5) |
| multi | +42.8 | +317.1 | +499.2 | +29.4 | +9.7 | +898.2 (+466.8 to +1233.2) |

The unassigned column is measured process CPU minus the instrumented scopes. It includes other engine work and timing overhead; it is not a proven single cause. The disabled-growth delta is statistically inconclusive here. This instrumented campaign does not reproduce the previous uninstrumented disabled-control regression reliably, so a specific causal explanation for that regression remains unproven.

## Inside the growth cost

Absolute mean CPU milliseconds per 1,024 ticks. Calculation alone is often cheaper than the old combined pass; publication and scheduling consume that saving. Old/new ecological trajectories differ, so this is engine workload accounting, not an identical-work algorithm comparison.

| Scenario | Old combined growth | New calculation | New publication | New preparation/submission |
|---|---:|---:|---:|---:|
| ai512 | 227.7 | 241.2 | 11.0 | 1.3 |
| dense | 100.5 | 73.4 | 44.6 | 1.1 |
| disabled512 | 0.8 | 0.0 | 0.0 | 0.7 |
| multi | 802.7 | 681.7 | 436.3 | 1.8 |

Publication rechecks current habitat/permissions/occupancy, validates deposit identity, applies capped increments through authoritative setters, updates growth statistics and change tracking. It scans proposals only. Scheduling CPU is about 1–2 ms per 1,024 active-growth ticks in these scenarios; it is not the dominant added cost.

## Shared capture payload

All variants perform 1,025 captures. Mean total copied MB (decimal), including initial capture. These totals are already exported by the engine; they are not estimated from wall time.

| Scenario | Legacy copied MB | Current copied MB | Sidecar-layout copied MB |
|---|---:|---:|---:|
| ai512 | 923.8 | 1063.5 | 1058.1 |
| dense | 824.5 | 1070.5 | 1072.6 |
| disabled512 | 552.3 | 555.9 | 556.3 |
| multi | 3824.0 | 4830.4 | 4767.2 |

The per-cell resource record grew from 12 to 16 bytes to carry deposit incarnation. The snapshot refresh copies this wider record, using changed 16×16 chunks and row copies. Dirty history/buffer availability and changed growth trajectories also affect copied bytes. Additional Growth/Rules requirements and leases exist even though the capture boundary is shared. The previous matching-window hardware profiles locate substantial cycles in refresh copying; the CPU scopes here measure the entire capture, not only memcpy. No bandwidth-only attribution is claimed. Gradient scopes identify where CPU is spent, but old/new resource trajectories and cache behavior both change; this accounting alone cannot distinguish additional gradient work from higher cost per operation.

## Identical-kernel control

The added opt-in benchmark compares `ResourceGrowth::calculate(map.stateView(), ...)` against the same function and seeds using a captured view of the same unchanged world. It verifies ordered proposals and RNG continuation for 32 seeds across 15 size/scenario pairs (128, 256 and 512 square; sparse, dense, saturated, blocked, multi-material). Snapshot acquisition, RNG construction and fixture setup are outside the timer. There is a warm-up and 20 paired rotated measurements, 128 calls each. Output buffers are preallocated.

| Size | Scenario | Snapshot/live paired median time ratio (95% CI) |
|---:|---|---:|
| 128 | blocked | 0.992 (0.990–0.994) |
| 128 | dense | 1.008 (1.006–1.014) |
| 128 | multi | 0.994 (0.988–0.998) |
| 128 | saturated | 0.988 (0.984–0.992) |
| 128 | sparse | 1.022 (1.014–1.029) |
| 256 | blocked | 1.014 (1.011–1.019) |
| 256 | dense | 1.012 (1.007–1.017) |
| 256 | multi | 1.012 (0.968–1.056) |
| 256 | saturated | 1.012 (1.003–1.022) |
| 256 | sparse | 1.004 (0.995–1.014) |
| 512 | blocked | 1.044 (0.922–1.169) |
| 512 | dense | 1.046 (0.881–1.207) |
| 512 | multi | 0.967 (0.863–1.143) |
| 512 | saturated | 1.022 (0.851–1.199) |
| 512 | sparse | 1.045 (0.761–1.328) |

## Layout-only experiment

The temporary sidecar patch moves incarnation counters into a separate array and returns frequently scanned resource records to 12 bytes. It preserves growth decisions, save bytes and deadlines. It copies counters using the same resource dirty chunks, retaining 16 bytes of resource-plus-incarnation payload per cell. This deliberately does not optimize counter refresh. The patch is evidence only; production sources were restored and the restored executable exactly matches the original SHA-256.

Uninstrumented comparison: one warm-up plus ten paired rotated/reversed measured runs, each 1,024 ticks, current shared pipeline versus sidecar shared pipeline. Positive throughput means the sidecar is faster.

| Scenario | Sidecar throughput change (95% CI) |
|---|---:|
| ai512 | +1.5% (+0.4 to +3.1%) |
| dense | -2.9% (-4.2 to -0.3%) |
| disabled512 | +1.8% (+0.8 to +3.5%) |
| multi | -2.2% (-9.7 to +7.0%) |

The sidecar is not a demonstrated general throughput fix. Its extra copy stream can increase capture CPU while improving some resource scans. A useful next optimization would track incarnation dirtiness independently, since stock changes do not alter identity, and measure resource-buffer refresh separately. That optimization has not been implemented or measured here.

## Method, reproducibility and limits

- Same AMD Threadripper 2950X/Linux x86-64/GCC 15.2 release build as the previous profiling report. Existing cpuset wrappers reserved cores 0–3 and siblings 16–19; engine affinity 0–3, performance governor. Audits record restoration. Core reservation does not isolate package memory bandwidth, power or every kernel activity.
- Stage runs: 72 total, including warm-ups; 5 measured pairs per scenario and 3 variants, rotated/reversed. Layout timing: 88 total. Kernel: 630 rows including warm-ups. Fixed fixture hashes, binary hashes, commands, CPU affinity, host activity and frequencies are in metadata/raw rows.
- Profiling-only source copies use CLOCK_THREAD_CPUTIME_ID and subtract nested scopes. Eight coarse scopes cover capture, old growth, calculate, publication, submission, gradient seed, gradient propagation and AI callbacks. The simulation is drained before ending the measurement. Timer/atomic overhead is present; use uninstrumented results for throughput. Confidence intervals are deterministic percentile bootstrap, 2,000 resamples; CPU table uses paired means, ratio tables paired medians. Five CPU pairs give limited uncertainty estimates.
- Current and sidecar final heavy checksums and sampled/proposed/accepted/stock-addition counters match in every run. Sidecar passes all eight focused ResourceGrowth tests, including pending-work save/load and deadline cases. This is not a new cross-platform or per-tick determinism qualification. No experimental production code is committed.
- Baseline revision d42d3e512; current production executable SHA-256 1ada564b4abd014b3dd354753e4812c8fcfaf08d835ae139e174b7e944cf2fcf. Test/docs-only source changes extend 14a5d8fe9. Master fetched at 4cb2ac058; the previously documented draft-PR integration/golden conflicts remain outstanding. This investigation does not resolve the whole implementation acceptance plan.
- Included: raw measurements, kernel equivalence test log/XML, stage source copies/header/build commands, sidecar patch, focused tests, wrapper audits, runner/report scripts. Prior hardware counters and uninstrumented old/new results remain under `../profiling/`. Initial kernel trials used an incomplete live view and failed; the final passing case uses stateView. Failed exploratory builds are not validation claims.
