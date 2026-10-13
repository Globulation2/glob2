# Capture and submission profiling

Tested production revision: `7588574389595d6fd51efeac293e55afdfc68ac5`.
Fetched master: `277510840054b1416f2defd7d86fe1431dd0ddc0`; production branch unchanged.
No tracked production changes were made. This is profiling evidence, not an implementation or a speedup claim.

## Methods and limits

macOS ARM64, Apple Clang 21, ordinary release `-O3` objects; native binary SHA256 recorded in provenance.json. Three trials for both the existing standalone advancing harness and a diagnostic harness using `Game::PreparationCompletion::Deferred`, as EngineRun does. Each trial runs 32 configurations of 2,048 advancing ticks: two/four teams, 128 initial units/team, seed 1234567, no AI or Numbi/Castor, zero/two workers, delays 0/1/2/8. Both stepping variants and all phase-instrumented runs match all 32 original final checksums; all 16 zero/two worker final checksum pairs match. These are final checksums, not a new cross-platform per-tick verification claim. Existing PR compatibility evidence remains separate.

The component probe holds eight snapshot leases, warms 128 captures, then measures 100,000 captures per case, three trials. Warmed capture allocations are zero. Diagnostic ablations skip work and produce incomplete snapshots: they are cost experiments, not valid implementations. A tighter direct-capture probe alternates baseline and four ablations in the same process for 25 rounds of 10,000 captures, two teams/256 units. It includes projection and buffer reuse but excludes Store byte accounting. Paired differences are nonadditive estimates because code layout/cache effects change.

Callgrind 3.18.1 with GCC 11.4 on Linux ARM64 (Docker) profiles the actual executor header and extracted, unchanged event field visitor/capture/capacity methods and the exact UnitState-to-UnitView memcpy operation. It does not profile the full Linux engine. GCC/GLIBC instruction counts are not macOS timings. Simulated cache dimensions are in raw profiles and do not represent Apple hardware caches. Native executor phase instrumentation adds clock reads and measures mutex acquisition, slot filling, and unlock+notify together. No production instrumentation was added.

## Static capture attribution

Interleaved direct capture baseline: 3.637 us per capture, two teams/256 units, eight retained leases.

| Diagnostic ablation | Median paired reduction |
| --- | ---: |
| Skip 256 UnitState copies | 1.625 us |
| Skip event-counter capture | 0.464 us |
| Skip full entity slot-table clear | 0.348 us |
| Skip effective attack query | 0.090 us |

Each UnitState is 360 bytes: 90 KiB for 256 units. The fixed GameplayEventCounters object is 7,592 bytes/team, plus variant and extra unit-type rows. Slot lookup tables clear 8 KiB/team/capture regardless of live population. Team views add about 0.1 us in the component probe. A separate 25-round paired Store experiment finds only 0.016 us saved by removing byte accounting. Earlier noninterleaved differences overstated that cost. No new batch/snapshot pool is justified by these results.

Callgrind per operation (20,000 measured repetitions, setup excluded): event capture with 32 building variants about 1,656 instructions, 99 conditional branches, and one indirect branch; capacity accounting about 56 instructions; clearing the two-team slot tables about 1,055 instructions; copying 256 unit records about 21,006 instructions. Event capture also enters std::vector copy-assignment paths for empty optional UnitStatistics rows; the raw profile supports investigating a small builtin-only fast path, not claiming an established saving.

## Submission attribution

One statistics lane job, eight-batch horizon, 20,000 batches/trial, five native trials. The paced owner performs synthetic work between submissions so workers can sleep. This is an isolated scheduler experiment, not a match benchmark.

| Sleeping workers | Lock acquisition | Slot filling | Unlock + notify_all |
| --- | ---: | ---: | ---: |
| 1 | 0.022 us | 0.023 us | 0.834 us |
| 2 | 0.023 us | 0.022 us | 1.236 us |
| 4 | 0.020 us | 0.021 us | 2.423 us |

A diagnostic notify_one variant reduces unlock+notify to 0.744 us with two workers and 0.796 us with four; all submitted jobs complete. This is not sufficient correctness coverage for a general executor change: multi-job groups, ordered lanes, presentation, continuations, maintenance and shutdown must retain wakeup/progress guarantees. Busy back-to-back submissions instead spend more on lock contention. Actual advancing matches keep workers active and submit costs are much smaller than the static sleeping-worker benchmark.

## Advancing matches, engine stepping mode

Three-trial medians in us/tick, delay eight and two background workers:

| Match | Owner total | Shared capture | Batch setup | Submission | Publication | Deadline wait | Worker stats |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 teams, no AI | 61.733 | 10.212 | 0.073 | 0.089 | 0.054 | 0 | 3.880 |
| 2 teams, Numbi/Castor | 142.266 | 30.622 | 0.088 | 0.100 | 0.057 | 0 | 5.027 |
| 4 teams, no AI | 65.356 | 13.302 | 0.083 | 0.201 | 0.072 | 0 | 6.978 |
| 4 teams, Numbi/Castor | 191.785 | 40.915 | 0.114 | 0.113 | 0.105 | 0 | 10.598 |

One phase-instrumented run of the two-team AI case finds visibility 12.267 us/tick, occupancy 7.895, unit record filling 3.045, growth/resource fields 3.408, building records 0.842, slot clears 0.720, team views 0.233, statistics inputs 0.613, and remaining setup/metadata 2.649. Its total is 31.672 us/tick. Phase clocks perturb execution; use these as attribution, not as an exact partition of the uninstrumented median.

Most of this capture is shared map/AI/world-preparation work. Statistics require catalog/entities/teams/rules/events every advancing tick and spatial map inputs only every 512 ticks. Where another consumer already captures entity/team components at that cadence, the measured statistics-input addition is approximately 0.5–0.6 us for two teams. Where no other consumer needs those components, statistics force the entity/team capture too. Neither full shared capture nor sleeping-worker submission cost can automatically be charged entirely to offloading team statistics. A matched advancing synchronous control remains necessary to establish the marginal net gain.

## Benchmark boundary finding

The existing standalone harness uses default Complete stepping: it calls map preparation at the end of each tick, creating a narrow next-tick map snapshot, then enriches/replaces it for stats/AI on the following read. Original runs captured roughly 4,096–4,261 times in 2,048 ticks. Engine Deferred stepping avoids that immediate end-of-tick preparation; the diagnostic harness captured 2,049–2,214 times instead. Two-team AI capture falls from 38.928 to 30.622 us/tick; four-team AI capture from 62.120 to 40.915. These are measured stepping-mode differences, not gains from modifying production code. All 32 final checksums match between modes. Older standalone numbers should not be presented as engine-path costs.

## Priorities

1. Shared snapshots: investigate visibility and occupancy copy bytes, dirty-chunk behavior and retained-buffer age first; these dominate advancing capture. Visibility currently copies discovered and visible arrays together under a common tracker.
2. Entity capture: reuse slot-index layouts while live membership/order stays unchanged, with explicit generations and correct birth/death/conversion/load/reset handling. This saves the measured full clear without altering unit data.
3. Event capture: inspect builtin UnitStatistics copy fast paths; copying only events already helped, but the remaining event DTO is not tiny.
4. Executor: consider wakeups proportional to runnable work. Benefits are mainly when workers sleep; actual advancing submit is around 0.1 us here.
5. Do not prioritize another allocation pool, TeamView copying or metrics accounting based on these results.

No overall offload speedup is claimed. Full engine Linux/Windows/browser/mobile execution was not newly verified by this profiling task.

## Reproduction

Run probes from the repository root. Exact native compile/link arrays and sources are archived; all production objects come from the existing release build. `run-capture.py` drives component trials; `build-interleaved.py` builds the tighter probe; `build-phases.py` shows phase instrumentation. The deferred harness change is the replacement of `w.step()` with `game.syncStep(0, Game::PreparationCompletion::Deferred)` plus `gui.consumeClientEvents()` in the advancing benchmark only.

Native match command: `GLOB2_STATS_BENCHMARK=1 GLOB2_STATS_BENCHMARK_TICKS=2048 GLOB2_STATS_PROFILE=1 GLOB2_PERF_DISABLE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy <binary> --test-suite=TeamStatsPipeline --test-case='advancing match statistics timing*'`.

Executor compile: `clang++ -std=c++20 -O3 -g -pthread -I<probe-dir> -DEXHEADER='"timed.h"' -DTIMED executor.cpp -o executor-timed`; run `executor-timed 3 1000 20000 paced` (three execution threads means two background workers). Linux executor command: `g++ -std=c++20 -O3 -g -pthread -I/work -DEXHEADER='"current.h"' executor.cpp -o executor-linux`; `valgrind --tool=callgrind --branch-sim=yes --cache-sim=yes --callgrind-out-file=executor-callgrind.out ./executor-linux 1 0 20000`.

Linux event probe compiles against repository headers and SDL3 development headers with the include list in linux-includes.txt. Run `valgrind --tool=callgrind --instr-atstart=no --branch-sim=yes --cache-sim=yes --callgrind-out-file=event-<mode>.out ./event-linux <mode>`, modes 0 event capture, 1 capacity accounting, 2 slot clears, 3 per-unit state copies. Client-request measurement is in the `.out.1` files.
