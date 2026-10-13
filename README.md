# Engine experiment record

This is a curated, source-qualified record of the investigation behind PR #1162.
It preserves decisions, adverse results, compact measurement tables, fixed inputs,
prototype patches and method/provenance records. It is **not** a full raw-data backup
or an exhaustive transcript of every attempted command.

## Start here

- [Experiment catalogue](experiment-record/catalogue.json): tried ideas, scope,
  results, decisions, reopen criteria, completed/incomplete cohorts and file mapping.
- [Decision ledger](experiment-record/decision-ledger.csv): one compact row per core family.
- [File manifest](experiment-record/manifest.json): exact source paths, package paths,
  SHA-256 hashes and inclusion purposes.
- [Download the compact experiment package](experiment-record.tar.gz).
- [Core adoption decisions](experiment-record/core/optimization/adoption-decisions.json).
- [Historical Playground table](experiment-record/historical-playground/runs.csv).
- [Historical Oazis table](experiment-record/historical-oazis/runs.csv).
- [Scaling observations](experiment-record/thread-scaling/scaling-scalars.csv) and
  [scaling summaries](experiment-record/thread-scaling/scaling-summary.json).
- [Compact core native measurements](experiment-record/core/derived/compact-native-measurements.jsonl).

The earlier [native verification index](README.txt), [native file manifest](native-evidence-manifest.json)
and [89-file native archive](native-evidence.tar.gz) remain intact.

## What was measured

There are 3,320 compact core native measurement rows, with warmup/source labels;
480 scaling observations (120 warmups, 360 measurements); 448 historical Playground
winner games; and 192 historical Oazis attempts (190 winners, two one-hour caps).
Oazis remains held with 256 planned attempts unrun. These are different units and
must not be summed as independent games or optimization hypotheses.

The original comparisons include an initial 144-job factor matrix, 96 mature jobs,
68 V2 follow-up jobs, 24 selected telemetry replication jobs, 14 GPU-sync jobs,
39 GPU-recovery jobs, 21 final paired jobs/186 process runs and 18 final natural games.
Some rows are subsets or repeated controls: use each cohort's scope, not a grand total.

The restricted-allocation auto32-to-auto6 worker comparison measured roughly 47%
lower process CPU and 34% lower elapsed time across ten layouts. The final fixed-three
package comparison measured about 1.5% lower CPU, 2.2% lower elapsed and 26% lower peak
RSS. They answer different questions and use different baselines; do not add gains,
transfer them to unrestricted devices, or treat them as measurements of later d2 integration.

## Prior decisions

### CPU resource sizing

Largest underreported accepted fix: auto32→auto6 within six eligible logical CPUs reduced whole-process CPU 46.98%, wall34.25%, RSS14.32% in ten-layout initial matrix. Fixed-three negative control essentially flat. Earlier eight-seed full Playground affinity-auto6 CPU29.81% and whole elapsed18.16% lower.

**Decision:** Accepted general eligible-CPU ceiling; no global optimum3, no unrestricted-host speed promise.

**Reopen only with:** Reopen for affinity/quota correctness on additional operating systems or coordinator allocations, not a universal three-thread optimum. Compare speed and throughput under the same total budget.

### Scheduler wake guard

Benefits strongest in oversubscribed auto32; at N3 Playground wall+0.16%,CPU+0.34%; Oazis wall−1.17%,CPU−1.16%. N4/N6 mixed.

**Decision:** Rejected for final source; extra scheduling policy did not justify complexity after fixing allocation.

**Reopen only with:** Only revisit with measured runnable eligibility/notification contention at sensible allocations and a simpler explicit readiness design; do not repeat unconditional wake tweaks assuming the oversubscribed gain generalizes.

### Snapshot ownership V1 and V2

Negligible independent memory benefit; mixed CPU, small-Nicowar/Oazis slowdowns, adverse Playground GPU-on wall result.

**Decision:** Both ownership candidates rolled back; original simpler ownership retained with optional query-only census.

**Reopen only with:** Do not reimplement eager/lazy reclamation unchanged. First account for published, staged, paused search and retained capacities; show an independent memory win without extra CPU.

### Replay telemetry representation

Initial V2 RSS10.8–37.5% lower across ten layouts; mature Playground about22–23% lower GPU-off and13% GPU-on, Oazis11%/8–9%. Adverse mature PG GPU-on direct V2/V1 CPU+2.015% retained; V1 original large-mixed CPU signal did not recur at same size.

**Decision:** V2 accepted with exact outputs/replay/lifetime gates; no universal speed claim.

**Reopen only with:** Retain lossless V2; target proven remaining schema/value duplication. Recheck mature GPU-on overhead and complete byte-for-byte output/lifetime gates.

### GPU cancellation and recovery

Startup GPU contexts cost roughly114–116MiB while useful completion low; recovery increased committed GPU fields but added CPU and wall cost across varied workloads.

**Decision:** Keep synchronization correctness and permanent-failure safe CPU fallback; reject recovery as optimization.

**Reopen only with:** Do not adopt retry recovery merely to increase participation. Reopen around useful batch size, transfer volume and publication deadlines, measuring total CPU/wall/RSS and useful commits.

### Field depth scheduling

Lazy Playground wall+5.39%,CPU+0.42%; Oazis wall+4.09%,CPU−3.23%. Full Playground wall−2.76%,CPU+1.49%; Oazis wall−0.48%,CPU+9.58%.

**Decision:** No universal switch: lower computed-work counters did not ensure faster completion.

**Reopen only with:** Profile-specific latency/CPU choices may be valid after multi-device evidence; no global lazy/full switch follows from these two-map controls.

### Allocator and retained pools

Arena cap tiny RSS reduction (<1.2%) with CPU generally higher. Pool0 reduced PG RSS2.39% but CPU+1.86%, wall+1.60%; Oazis mixed. Heap showed removal raised total requested allocation and calls.

**Decision:** No arena global cap or wholesale pool removal adopted.

**Reopen only with:** Target actual ownership and capacity, not global arena caps or deleting all pools. Require measured allocation-call and CPU cost alongside RSS.

### Historical autosave cost

Autosave-off CPU and wall31.36% lower in that historical workload.

**Decision:** Explains cost; not accepted optimization because autosave must remain. Current serialization optimization not established by this probe.

**Reopen only with:** Optimize serialization or repeated preparation while retaining autosave. Feature-off savings are a diagnostic upper bound, not an accepted capability removal.

### Historical chamfer/BFS dispatch

Chamfer/BFS CPU ratios2.57/3.10 for transitional policy (PG/Oazis),2.29/2.73 with cap256.

**Decision:** Causal historical dispatch cost demonstrated; cannot attribute whole historical interval or infer current replacement.

**Reopen only with:** Use historical control to understand the transition, not to revert current gradient rules. Any new dispatch must preserve actual current scalar outputs/publication ticks.

### Historical exact gradient kernel

Existing optimized kernel CPU/wall about22.4% lower PG and20.6% lower Oazis.

**Decision:** Confirmed prior optimization payoff, not fresh48h shipping speedup.

**Reopen only with:** Prior payoff was confirmed; do not count it as a newly discovered optimization. Current kernel remains baseline for future disassembly and oracle-verified proposals.

### Pipeline AI/gradient scheduling

AI1/grad0 wall+16.44%,CPU−14.75% vs AI4/grad2; AI1/grad2 wall−0.51%,CPU−6.93%.

**Decision:** Established CPU/latency tradeoff, not general current default policy.

**Reopen only with:** Investigate resource-profile tradeoffs under unchanged AI decisions and eight-tick publication; CPU savings and completion latency can conflict.

### Gradient chunk and FIFO batching

Chunk128 sometimes~1.5% faster but one256² fixture~30.3% slower with more capacity. FIFO alternatives all25 median CPU/wall regress,24/25 intervals entirely adverse.

**Decision:** Rejected both general replacements; keep current64 and current FIFO. Warm kernel microbench only, not mature game evidence.

**Reopen only with:** Do not repeat fixed16/128 chunks or batching32/128 as blanket replacement. Reopen only with a new work/granularity model and fixture-specific explanation for regressions.

### Mature memory and CPU attribution

Mature heap peak521,535,011 requested bytes: gradient fields130.8MB,snapshots93.3MB,search frontiers74.1MB,replay55.2MB. Mature Callgrind timed out before target START/STOP and produced no valid mature attribution.

**Decision:** Useful ownership priorities; failed one-hour instrumented attempt is real effort but no result. New cache invalidation/ownership redesign remains unimplemented.

**Reopen only with:** New ownership/invalidation census and continuous late-game profiles are still useful. One-hour Callgrind timed out before target and supplies no valid mature attribution.

## Scheduler, profiles and incomplete work

The cached worker-selection prototype removed one scan in linked release assembly
and passed source-qualified correctness checks. It was **not adopted** and has zero
accepted measurements of the changed worker path. Two N1 measured pairs are negative
controls only. Original observer/preflight failures are preserved.

Earlier foundation and scheduler heads have actual native, browser and Android
build/package/correctness receipts. A browser startup timeout and its separate same-byte
quiet qualification remain separate records. Those results do not validate the final
head's current platform coverage or physical-device performance.

Six profile names and policy/provenance machinery exist; six calibrated tiers, automatic
luxury/eco selection and adaptive presentation do not. Ready queues, owner helping,
new field-cache reuse and new GPU transfer/batch improvements were not implemented.
The same-budget 16-attempt completion/throughput pilot was prepared and never launched.

## Reading older reports

Original reports are preserved verbatim. Some retain earlier pending/running flags,
absolute filesystem links or conclusions later superseded. Use completion receipts,
final adoption decisions and catalogue scopes as authority. The source-to-package map
in catalogue.json locates preserved files; absolute paths are provenance, not web links.

Mature sampled attribution has 8,986 reconciled samples and 56.87% empty callchains.
Use the corrected leading-namespace/accounting supplements. Earlier snapshot-heavy
classification was contaminated by template/argument names and is not authoritative.
Leaf ranking is not complete caller attribution or a bound on synchronization cost.

## Recalculate saved statistics without running games

Extract experiment-record.tar.gz, then from its experiment-record directory run:

```sh
python3 thread-scaling/recompute-scalars.py --root .
```

This standard-library script reads only saved CSV/JSON. It recomputes 196 historical
means and point/paired-bootstrap estimates for 60 scaling cells with 11 metrics.
The included verification records a match to original calculations. It excludes warmups
and capped runs according to the original cohort rules. It does not launch games/builds,
create new measurements, enlarge the sample or establish new causal certainty.

Original campaign launchers and patches are historical records, often tied to old
paths, versions, dependencies and observer protocols. Do not blindly apply them to
current master or rerun campaigns before reading their recorded adverse results.

## Reconstruction limits

This preserves numerical findings and why prototypes were retained, rejected or left
unmeasured. It does not reproduce every original process byte-for-byte. Large mature
checkpoint files, full per-tick dumps, binaries/toolchains, raw profiler recordings and
browser recordings are omitted. Some earlier historical runs never had full per-tick
traces. The final compiled golden assertion's temporary runtime trace was not retained.
All local originals and held workloads remain untouched.

Source, binary, library and fixture hashes are included where recorded. Curated prototype
patches have base/source qualification; some reconstructed patches were verified against
original build manifests. Older proof is not current-source acceptance. Reopen rejected
ideas only with new causal evidence or a materially different design/workload/architecture.
