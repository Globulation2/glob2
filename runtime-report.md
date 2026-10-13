# Capture-inclusive runtime follow-up

There is supported runtime benefit in the measured large-population serial capture → Scene preparation → software GUI drawing pipeline. The initial draw-only benchmark prepared its Scene once, so it could not establish a capture benefit. This follow-up measures a fresh capture and Scene on every measured boundary, plus a retained-snapshot control that captures zero times.

## Main release experiment

Before: `3a3917757752fe64a2af9c1dc964c074794ebf3e`; after: `5a40f7a9f281cd1aa71c6980573cee3f6f35a36f`. These frozen revisions isolate the compact-core refactor from newer master CPU gradient/render optimizations. Exact source archives were rebuilt with ordinary SCons release=1 (-O3); diagnostic probe mains use -O3 -g. Source validation compares tracked simulation/render/library files to the Git blob IDs. Binary hashes and compile/link commands are included.

Linux x86-64, AMD Ryzen Threadripper 2950X, GCC 15.2, existing pinned SDL/dependencies. Affinity is CPUs 4/5/6/7, one hardware thread on four physical cores; not an exclusive cpuset. Governor remains schedutil; no global CPU policies or other processes were changed. Per-run load averages and sampled CPU frequencies are retained. Builds/profiling do not overlap release timing runs. Rendering uses the same 640×480 software backend with clouds disabled and final presentation excluded.

The 1,024-worker fixture has a 128×128 map and 64 inns; the ordinary control has 32 workers, a 64×64 map and 16 inns. These are preliminary seed-713 fixtures, not a qualified sustainable colony corpus. Static GUI tests freeze simulation, then explicitly invalidate the boundary to force fresh capture; reuse controls retain the valid static-world snapshot. AI/movement cases request their observation component union but execute no controller/planner in the GUI test.

One whole-run warmup pair, 128 internal warm frames, 1,024 measured frames per run, alternating before/after order by case/repetition. Started with ten pairs; the predeclared primary interval-width rule (>2 percentage points) triggered one extension of every GUI case to twenty pairs, regardless of direction. No samples were discarded. Primary outcome: large-colony/plain mean capture-inclusive frame time. The resulting primary interval remains wider than the precision target, but its lower bound is positive. This is evidence of benefit with a bounded estimate, not movement trial qualification.

Run-level paired geometric mean ratios and 50,000-resample bootstrap intervals (seed 713). Pointwise 95% intervals accompany a conservative 99.5833% Bonferroni interval across the twelve primary full-runtime comparisons (eight GUI cases, four engine cases). Frames within a run are not treated as independent repetitions. All final simulation checksums match between variants. Each fresh GUI run asserts exactly one capture per frame; each reuse control asserts zero captures. Rendering also checks that it preserves the loaded simulation checksum.

| GUI case | Pairs | Before mean µs | After mean µs | Time reduction, 95% CI | Family-adjusted interval |
|---|---:|---:|---:|---|---|
| large-colony/ai | 20 | 524.92 | 484.30 | 7.75% (6.72–8.83%) | 6.29–9.33% |
| large-colony/movement | 20 | 512.69 | 484.38 | 5.49% (3.94–6.99%) | 3.22–7.61% |
| large-colony/panels | 20 | 532.95 | 503.26 | 5.58% (3.92–7.27%) | 3.20–8.08% |
| large-colony/plain | 20 | 514.19 | 478.57 | 6.92% (5.55–8.25%) | 4.91–8.83% |
| large-colony/reuse | 20 | 441.33 | 441.96 | -0.17% (-1.82–1.44%) | -2.62–2.20% |
| large-colony/reuse-panels | 20 | 451.04 | 460.30 | -2.12% (-4.19–-0.18%) | -5.24–0.70% |
| ordinary-control/panels | 20 | 430.79 | 439.49 | -2.04% (-3.75–-0.45%) | -4.52–0.23% |
| ordinary-control/plain | 20 | 414.86 | 423.83 | -2.16% (-4.68–0.41%) | -5.80–1.62% |

The four large-colony fresh-capture cases remain positive under the family-level check. Ordinary cases and retained-snapshot controls do not establish benefit. The first experiment suggests a small panel-control slowdown at pointwise 95% confidence; its family interval includes no change. That negative result is retained and investigated below.

## Attribution to capture

- plain: capture 76.63 -> 32.41 µs; capture plus Scene preparation 85.94 -> 41.81 µs; full timed pipeline 514.19 -> 478.57 µs.
- panels: capture 77.67 -> 43.61 µs; capture plus Scene preparation 87.39 -> 53.57 µs; full timed pipeline 532.95 -> 503.26 µs.
- ai: capture 78.30 -> 42.86 µs; capture plus Scene preparation 88.24 -> 52.76 µs; full timed pipeline 524.92 -> 484.30 µs.
- movement: capture 76.50 -> 33.44 µs; capture plus Scene preparation 86.26 -> 43.40 µs; full timed pipeline 512.69 -> 484.38 µs.

Plain capture is about 14.9% of the original timed pipeline, falling to about 6.8%. Capture counters show 44.22 µs saved per frame. The full pipeline saves 35.62 µs on arithmetic means; other work and measurement variation account for the residual, so the full saving is not assumed equal to the capture counter.

- plain fresh-minus-reused difference of differences: 36.25 µs saved (95% interval 25.13–46.62 µs). Capture counters show 44.22 µs saved.
- panels fresh-minus-reused difference of differences: 38.94 µs saved (95% interval 27.32–51.26 µs). Capture counters show 34.06 µs saved.

This control supports a causal capture contribution: the plain retained-snapshot control is roughly unchanged, while the fresh-capture pipeline improves. The phase counter saving lies within the ablation interval. Warmed capture allocations are zero for both variants; this saving is not attributed to fewer measured allocations. Logical copy/payload bytes and retained/capacity counts remain distinct counters.

## Longer engine measurements

One warmup and ten rotated paired release repetitions, same frozen binaries as prior engine tests. 512 total ticks, --benchmark-warmup=64, 448 CPU-measured ticks, four compute participants, identical initial states/seeds/orders. Wall run_ns includes the internal warmup; CPU and tick-distribution measurements use the benchmark window. These longer trajectories have different service/resource work from the earlier 128-tick pilot and are not pooled with it.

| Engine case | Wall reduction, 95% CI | Family-adjusted interval |
|---|---|---|
| large-colony/reference | 3.38% (-0.24–6.97%) | -1.87–8.45% |
| large-colony/shadow | 3.63% (2.39–4.98%) | 1.98–5.62% |
| ordinary-control/reference | 0.53% (-3.02–4.61%) | -4.44–6.83% |
| ordinary-control/shadow | -1.83% (-6.10–1.56%) | -8.58–2.69% |

The large-colony K=1 shadow comparison supports a modest runtime improvement relative to the previous shadow implementation. It still executes additive diagnostic planning; this is not a live movement speedup. Capture savings are roughly 31 ms across a roughly 13-second run, too small to explain the whole improvement. Admission, worker/memory effects and other live-unit-layout costs are separate possible contributors; no attribution to them is established here. Planner fields/visited/cache-hit counts match, including 2,463 fields and 37,474,396 visited cells in representative paired results. Reference runs copy no unit state during the measured run and remain inconclusive; their small overall changes cannot be credited to reduced unit capture.

## Panel regression investigation and independent replication

The first twenty-pair experiment suggests about 2% slower ordinary panels and retained-snapshot colony panels at pointwise 95% confidence. Scoped optimized Callgrind of drawUnitInfos across 48 calls (16 warm + 32 measured) reports 10,756,449 -> 10,774,474 instructions (+0.17%), with 5.0% fewer simulated I1 misses and 2.5% fewer D1 read misses. Small additional accessor instructions do not explain a large new workload; simulated cache counts are not real hardware timings. Before/after 640×480 selected-unit images are RGB-byte-identical. Raw profiles, annotations, commands and images are included.

Then ran a separate one-warmup/ten-pair targeted replication with 512 warm frames and 4,096 measured frames per run, using the same binaries and settings. It is not pooled with the first experiment and is labelled follow-up rather than a new global claim.

| Longer-window GUI case | Time reduction, 95% CI |
|---|---|
| large-colony/panels | 5.56% (3.95–7.45%) |
| large-colony/plain | 8.45% (6.63–10.30%) |
| large-colony/reuse-panels | -0.48% (-2.29–2.03%) |
| ordinary-control/panels | -0.73% (-3.34–2.21%) |

Large-colony fresh-capture gains reproduce. The two panel-control follow-up intervals include no change, so the small initial slowdown is not clearly reproduced. These bounds still allow a modest regression; they do not prove zero regression or erase the first negative results. Panel changes remain a precision/coverage limitation rather than a demonstrated large new code cost.

## Scope and recommendation

This establishes runtime benefit for the compact-core capture-inclusive pipeline on these Linux software controls. The observed gain is tens of microseconds per advancing observation, with a larger relative benefit when drawing work is small. Do not extrapolate to every viewport, GPU renderer, gameplay phase or CPU. Production threaded rendering draws an already published Scene; these serial tests do not establish live-match FPS or simulation/render overlap benefits. Capturing once per measured frame models one advancing boundary per frame, not the production pacing ratio.

Keep the prototype in draft and movement at Stage A. No original coverage, performance, memory or gameplay gate was relaxed; no live delay is qualified. Missing supported platforms, complete visual/gameplay corpus, 30-seed distributions, allocator-inclusive memory qualification and footage review remain as recorded in the existing evidence. Smaller ordinary/panel effects need further precision and representative coverage before a universal integration recommendation. Canonical representation and version constants are unchanged; this follow-up changes only documentation and ignored diagnostic/evidence files.

## Review files

- metadata.json: source/platform/affinity/settings/precision rule.
- source-validation.json and probe compile/link JSON: exact source provenance and toolchain commands.
- render-pairs, engine-pairs, panel-followup-pairs: all raw samples, per-run settings, results and logs.
- summary.json, capture-ablation.json, panel-followup-summary.json: calculations and intervals.
- panel-profiles: scoped optimized Callgrind, byte-identical images and RGB hashes.
- runtime-benefit and capture-runtime-phases: charts in PNG/SVG/PDF.
- Scripts reproduce the measurements and analysis; executables/build directories/virtual environments/personal profile folders are excluded from the archive.

PR source after the documentation-only follow-up: `6b0dcacb8a5ad81592197de759252179cee01036`; master fetched again: `277510840054b1416f2defd7d86fe1431dd0ddc0`. Compiled/timed code remains the frozen before/after source commits above.
