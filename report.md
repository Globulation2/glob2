# GPU offload milestone: execution is asynchronous; performance gates remain unmet

The campaign remains active. The first clean screen saves **24.2% process CPU per tick**, below the required 30%, and worsens tick p99 by **20.4%**. This is development evidence for one early 512² open game; it cannot establish aggregate acceptance.

## Measured screen

Five rotated paired rounds used the same retained save, eight compute slots, release builds, 8,192 fixed ticks with 1,024 warmup ticks, and the local RTX 2070 SUPER selected explicitly. An exclusive cgroup v2 partition reserved eight physical CPU cores and both SMT siblings (8–15,24–31); unrelated compiler processes remained on the other eight cores. Driver threads within the game process were included in process CPU. Shared memory, power and kernel/IRQ interference remain possible. Interval-wide GPU isolation was not proven in this historical screen. A later resource inventory found a persistent desktop compute context; device contention remains a qualification limitation, and new diagnostic runs explicitly record device UUID mapping and resource boundaries.

| GPU versus frozen PR CPU | Paired ratio | One-sided 95% upper bound | Gate |
| --- | ---: | ---: | --- |
| Process CPU/tick | 0.7581 | 0.7640 | Fail: must be ≤0.70 |
| Warm wall time/tick | 1.0168 | 1.0273 | Fail: point worsens and bound >1.02 |
| Tick p99 | 1.2040 | 1.2575 | Fail: point worsens and bound >1.05 |
| Mean publication wait/tick | 0.2349 | 0.2523 | Improved; tails still fail |

The same-source GPU/CPU ratio was 0.7501 (upper 0.7601), with tick p99 ratio 1.1733 (upper 1.2075). Candidate CPU versus frozen CPU was 1.0106 (upper 1.0203). No aggregate result is available: independent maps and middle/late phases are still missing.

The frozen baseline is PR commit `13843165bd7e778650142fe884c7af37b40b56f1`, binary SHA-256 `00e1c595710ee4f814fcab5d64a6a6ba15c769b151b441fa43ae71818a37bb5d`. The screened candidate is `3f394a366cc2ba47eae32853178d0cf89dcc8ac6`, binary SHA-256 `02057ab221a5afbaa47960f53201fa5dbe976e20f7bab64b9a2e848d0caf82bf`. The candidate's newer changes have not been timed.

## Why the next wave targets host overhead

Across 7,168 warm ticks, mean candidate CPU cost was 40.684 s: seed preparation 3.309 s, propagation 17.237 s, remainder 20.139 s. Forced GPU cost was 30.514 s: preparation 3.650 s, coordinator 4.274 s, remainder 22.590 s. Removing propagation creates enough theoretical room, but coordinator/driver/handoff overhead consumes much of it.

Each GPU sample executed 3,049 device fields with 87,144 dispatches and 10,893 convergence checks. The coordinator completed 7,176 fields with maximum batch one, and there were no warm fallbacks or memory-budget declines. Only one cost upload occurred. Backend calling-thread CPU was 3.45–3.52 s. More completed service fields than actual device fields is not proof of offload; counters now distinguish those outcomes.

RSS rose from about 509 MB on CPU to 634 MB with GPU. Tracked accelerator host/device peaks were about 15.3/3.15 MB; driver storage is separate. This wave has not demonstrated RAM savings.

Earlier unreserved polling comparisons overlapped unrelated builds. Every sample is retained, but those results are rejected for qualification. Repeating that configuration unchanged is not the next experiment.

## Implemented and verified checkpoint

Required GPU requests own immutable inputs and original seeds. A process-owned coordinator executes them while CPU workers can take unrelated jobs; original tickets remain pending until exact output or one CPU continuation finishes. Fixed publication order and saved pending results are retained. Map reconfiguration does not join optional initialization. Accelerator buffers are demand sized and bounded.

Native source at integration `1809113deb93746b05c835f26e95f185dc9d2b93` is tree-identical to evidence freeze `bcfb570758e0b4ad7b8756ee63639e592ab56691` (Git tree `01d93c2baab91d74e938391e42e4d519a5685f0c`). The release binary SHA-256 is `d4e79215e0dc4814ffb7822a5aafac0aeb7a96c3f9933865415080458b0b8d21`. It passed **87 selected unit and 79 selected engine cases**, zero failures/skips. Nine epoch/parity/check-interval configurations each passed 274 actual assertions across all six kernel plans and mixed retirement/reuse. An initial XML parser miscounted skipped listing cases; the original classification is retained alongside its correction.

At 512², frozen PR CPU, current CPU, forced GPU and worker-noop GPU matched full-world, simulation and replay/entity checksum sidecars at every tick for 2,048 initial ticks and 2,048 ticks continuing each variant's own save (eight valid rows). CPU, epoch, parity and combined modes also matched across 768 ticks (four valid rows). These are exactness results, not new timing results.

A scoped development harness can write/import 1024² fixtures while ordinary map loading still rejects them and generator/network limits remain unchanged. Open and winding/corridor fixtures match CPU/GPU across 256 initial ticks. **The longer mixed-AI 1024² continuation failed on CPU:** Nicowar emits an `ORDER_ALTER_FORBIDDEN` brush with dimensions 1023×1024 at observed tick 264, due tick 272, while the existing order codec permits at most 512 per side. The pending order also fails on save reload. Failed traces, saves and decoded command evidence are retained; this corpus is unsupported pending a compatible fix and has not passed large-map continuation or performance gates. No protocol bound was silently widened.

Automatic mode still begins unknown classes on CPU. Bounded probe/policy primitives are implemented, but live probes and promotion remain disabled pending qualification and complete tuning-enabled measurements.

## Next experiments and outstanding gates

Instrument ownership/handoff, exact worker/coordinator thread IDs, driver CPU and per-tick GPU publication stalls. Screen worker-side fixed-field completion and epoch tile masks separately; the latter removes one mask-clearing command per dispatch while preserving inactive ping-pong copies. Screen fixed parity kernel bindings separately to reduce repeated driver argument calls. None is enabled by default or claimed faster before measurement. Cross-deadline ready-only batching requires measured homogeneous batch bounds and conservative slack estimates.

The production SIMD CPU reference and development comparator are being checked against the actual strongest CPU path; a slower reference must never manufacture apparent GPU savings. Full corpus, strongest CPU/master baseline, rendering/frame guards, thread-count/platform compatibility, save/replay/network checks, failure injection, held-out confirmation, removal ablations and tuning-on overhead remain required.

Raw manifests, all screen samples, reservation receipts, exactness receipts and native inventories are preserved separately from source history. The PR stays draft.
