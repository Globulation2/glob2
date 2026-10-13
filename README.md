# Renderer instruction and branch investigation

These measurements identify which work reaches the workers. They do **not** establish frame-time or FPS gains on this contended machine. Native perf instruction/branch counts are the principal compute-work measurement; Callgrind provides detailed function attribution and call graphs. No production code or renderer activation policy was changed.

## Findings

- The warm static cache leaves no terrain or raster jobs. Drawing and presentation copying stay on the owner thread.
- Animated terrain moves roughly 61% of Callgrind instructions to graphics workers. Owner-side material preparation, drawing and presentation remain substantial. There are four terrain batches and 12 jobs per measured frame; these batches are not one fully concurrent frame-wide job graph.
- The correctly configured half-scale GUI moves only roughly 2.3% to graphics workers. Each frame has 24 raster tiles, four raster jobs, and 879 fallback operations. The largest instruction consumers are SDL modulated alpha blending, alpha fills and scaled blits on the owner. Adding workers cannot distribute operations that remain in this path.
- Forced rebuilding moves roughly 93.4% of Callgrind instructions to workers, demonstrating that substantial work can reach them when cache reuse is disabled. This is an intentionally rebuilding-heavy workload, not a normal warm frame.
- Parallel execution adds a modest number of user-space instructions: roughly 0–1.2% versus immediate execution in these workloads. Native kernel instructions increase in active worker workloads; their variability is reported separately. The data does not support a claim that additional user-space computation grows proportionally with worker count.

The evidence supports investigating the large owner-side blending/scaling path before concluding that parallel rendering should be abandoned. It does not establish how much faster those operations would become after parallelization. Memory bandwidth saturation, waiting latency, energy use and scaling speedups remain unmeasured.

## Native hardware counts

Millions of retired user-space instructions per measured frame; three runs per cell. All four events collected with 100% PMU running coverage, no multiplexing. Every mode's user instruction count varied by less than 0.15% across repeats. Kernel work and branches are separate below. Immediate is traditional drawing; serial is synchronous batched drawing. Parallel1 uses the tiled parallel algorithm on one background worker and is not equivalent to serial batching.

| Workload | Immediate | Serial batched | 1 worker | 2 workers | 4 workers |
| --- | --- | --- | --- | --- | --- |
| Warm static map | 7.772 | 7.773 | 7.773 | 7.773 | 7.773 |
| Animated terrain | 33.400 | 33.529 | 33.612 | 33.608 | 33.606 |
| Half-scale GUI + pan | 181.546 | 181.981 | 182.082 | 182.136 | 182.049 |
| Forced terrain rebuilding | 289.632 | 292.452 | 293.009 | 292.895 | 292.837 |

Millions of native user branches per frame and parallel2 user-instruction change versus immediate:

| Workload | Immediate branches | 2-worker branches | Extra user instructions |
| --- | --- | --- | --- |
| Warm static map | 0.630 | 0.630 | +0.00% |
| Animated terrain | 3.913 | 3.962 | +0.62% |
| Half-scale GUI + pan | 14.577 | 14.656 | +0.32% |
| Forced terrain rebuilding | 29.855 | 30.615 | +1.13% |

Millions of kernel instructions per frame, mean[min,max]. Kernel work includes target syscalls, worker synchronization and small profiler signal-boundary overhead; scheduler/host activity affects it. Do not treat it as an invariant renderer payload or as CPU time.

| Workload | Immediate | 2 workers | 4 workers |
| --- | --- | --- | --- |
| Warm static map | 0.353[0.332,0.388] | 0.370[0.330,0.422] | 0.349[0.340,0.354] |
| Animated terrain | 0.422[0.395,0.441] | 1.077[1.019,1.149] | 1.067[1.029,1.115] |
| Half-scale GUI + pan | 1.032[0.800,1.311] | 1.288[1.169,1.368] | 1.382[1.227,1.635] |
| Forced terrain rebuilding | 1.658[1.251,1.872] | 6.296[5.231,8.388] | 5.959[5.155,7.516] |

## Callgrind distribution

Graphics-worker instruction share of all process instructions, averaged over three repeats. Callgrind serializes threads: these percentages are neither CPU-time fractions nor Amdahl speedup estimates. Its complete self-cost records reconcile exactly to every thread footer, and the sum of thread footers reconciles exactly to collected global events for all 60 runs.

| Workload | 1 worker | 2 workers | 4 workers |
| --- | --- | --- | --- |
| Warm static map | 0.00% | 0.00% | 0.00% |
| Animated terrain | 60.76% | 60.69% | 60.74% |
| Half-scale GUI + pan | 2.28% | 2.28% | 2.28% |
| Forced terrain rebuilding | 93.43% | 93.42% | 93.42% |

Millions of Callgrind instructions per frame:

| Workload | Immediate | Serial batched | 1 worker | 2 workers | 4 workers |
| --- | --- | --- | --- | --- | --- |
| Warm static map | 8.267 | 8.260 | 8.267 | 8.257 | 8.260 |
| Animated terrain | 34.093 | 34.215 | 34.297 | 34.337 | 34.312 |
| Half-scale GUI + pan | 186.539 | 187.008 | 187.075 | 187.031 | 187.081 |
| Forced terrain rebuilding | 297.107 | 300.624 | 300.962 | 300.970 | 300.976 |

Native PMU and Callgrind absolute counts differ: instrumentation, instruction-counter semantics and library execution under Valgrind differ. They agree on the direction and modest size of user instruction overhead. Native branch events count a different set of branch types from Callgrind Bc+Bi; absolute cross-tool branch equality is not expected. Function percentages in work-summary.json use exclusive self-cost, avoiding double-counting inclusive call graphs. Data references and simulated cache/misprediction events are retained but do not measure real bandwidth or hardware misses.

## Native instruction attribution

Sampling uses instructions:u with a fixed 500,000-instruction period, not a time/cycle frequency. All nine profiles have zero lost samples; unknown leaf addresses are below 0.5%. Native sampling corroborates worker placement and dominant terrain/SDL functions. DWARF unwinding is incomplete for many worker samples: the leaf IPs are valid, but native inclusive callgraphs omit much worker ancestry. Use leaf-samples.txt and self.txt for native attribution and Callgrind for complete call graphs. Counts, missing callchain inventory and symbol coverage are in work-summary.json.

## Validation and correction of earlier evidence

All 60 native cold/warm captures are byte-identical across the five execution modes, covering six scenarios and 12 comparison groups. Simulation checksum remains 2231453127. CAMERA lines verify effective 0.5 zoom in the GUI workload; all four profiled cases use an actual1280×800 surface. Capture timings are ignored irrespective of recorded host activity.

Earlier runs labeled half-scale in the PR1160 evidence actually ran at zoom 1.0 because the harness requested zoom before initializing map/viewport dimensions. Those labels and conclusions about half-scale performance were invalid and are superseded. The local profiling harness now extracts/prepares the scene and initializes camera dimensions before setting zoom. GUI camera position also changes to the correctly initialized local-team viewport, so old/new GUI timings cannot be compared as the same workload. Map controls explicitly retain origin 0,0. Earlier wall/CPU timing reports remain exploratory under the user's instruction to avoid timing-dependent conclusions on this machine; no cumulative FPS claim is established by this investigation.

## Scope, provenance and reproduction

Source base 277510840054b1416f2defd7d86fe1431dd0ddc0 (merged PR1160), fetched before creating the isolated worktree. Apply expanded.patch. The local experiment raises the worker cap to four, corrects camera setup and adds profiling boundaries. This is not a shipping cap change: memory/lifecycle/platform policy for four workers has not been qualified. Production default remains immediate and the production parallel cap remains two.

Linux x86_64, Ryzen 7 6800H, GCC 13.3, SDL 3.4.16, Valgrind 3.22.0. Release build: GLOB2_SDL3_PREFIX=<pinned SDL prefix> scons release=1 retain_symbols=1 -j8 software-render-benchmark. No fast/PCH/unity build. Exact binary hashes and validated symbol/stripped PT_LOAD parity are in binary-provenance.json; linked library paths and hashes, asset manifest, build logs, source patch and all process commands/environments are retained. Full debug executable remains local due its 1 GiB size; the stripped executable is included and the patch/base/build flags permit rebuilding the profile target. Matching binary hashes are required for artifact reuse.

Fixture 128×128, four teams, 40 units, 100 buildings, tick 0. SHA256 26cc515a843c418ccc6af6d024398ee5ab1c6d535e6e01d1c53635cd881d3da4. Same 7,470 legacy runtime assets, clouds disabled, frozen simulation, compute telemetry off, fixed compute pool 5 (owner+four background workers). No competing simulation or actual browser/mobile/HD gameplay coverage. Warmup 30 frames; measure 10 frames, except forced rebuilding 5. Pan advances 0.25 logical pixels per frame. Half GUI has paused terrain, not animated terrain.

Callgrind starts/stops instrumentation only around the measured loop and retains separate threads. Explicit model: I1/D1 32 KiB, 8-way, 64-byte lines; LL 16 MiB, 16-way, 64-byte lines. This is a chosen synthetic cache model with cold caches at instrumentation start. Initial ZERO/DUMP boundary attempts produced invalid wrapped counters and were rejected; initial six-event perf groups could not schedule and were rejected. Neither is included in accepted raw profiles.

Perf attaches to all existing target threads while the target is stopped at the pre-loop getrusage boundary; control-FIFO acknowledgement enables events before resuming. At loop completion the target stops again, events disable with acknowledgement, and the profiler exits before shutdown resumes. Events are {instructions:u,instructions:k,branches:u,branches:k}. Only the target process is counted; other host activity is recorded, not a timing rejection criterion. Read-only privileged perf was used without altering host security settings. Profiler/benchmark elapsed time is deliberately excluded; the stop boundaries distort it. Waiting or presentation latency cannot be recovered from instruction counts alone.

Streaming uses temporary per-page terrain caches. Outer terrain metric counters therefore report zero despite substantial worker work; use profiles for this case. Disabled executor telemetry likewise reports no available worker counters, not proof that workers were absent.

Download all parts, verify SHA256SUMS, concatenate inputs.tar.gz.part-* into inputs.tar.gz, and extract it. Concatenate profiles.tar.gz.part-* into profiles.tar.gz and extract it into a separate evidence directory. Inputs contain fixture.game and runtime-assets/. Profiles contain render-profile/ with raw measurements and scripts. Update support.py's original machine-specific fixture/asset/shader paths, provide an SDL software-capable display and the pinned dependency prefix, rebuild the symbol target from the base+patch, and place it at render-profile/symbol-bin/expanded. Obtain the stripped target via objcopy --strip-debug. Run callgrind.py, perf_profile.py (read-only sudo perf required) plus serial/static controls as retained in commands.txt, run.py captures, perf_leaf.py, analyze_work.py and report.py. Parser checks reject inconsistent/wrapped totals or invalid/multiplexed PMU counters. Do not reuse existing metadata directories when intentionally reproducing runs.

Evidence is on a dedicated branch. No generated profiles, benchmark reports, experimental worker caps or Valgrind includes were committed to master. This study compares execution modes on the optimized post-merge source; it is not a new pre-merge-versus-post-merge cumulative performance measurement.
