# Movement shadow fixes and retest

Source revision 3a3917757752fe64a2af9c1dc964c074794ebf3e, integrated base 0709322f3b140507f432aa820611c455314c91c2. Engine source was built at parent 675c8f823; the final commit regenerates CLI documentation/completions only. Before revision 16ae31c224f633c8c483cee54b76382f6d134d70, base c7d688e9c. Linux x86-64, GCC 15.2.0, pinned SDL prefix, release -O3 for measurements and dev_fast=1 for focused checks.

## Changes

- Direct Game::syncStep callers capture movement only if the current tick was not already admitted. Null-order acknowledgement invalidation stays intact for other consumers.
- Reverse search, seed checks and direction ranking reuse one MapState::View instead of rebuilding it for every legality check.
- Route field keys retain team, travel capabilities and mode-specific goals/home semantics. Caller identity, position, medical/activity state and unrelated assignment fields remain on requests/suggestions for validation but no longer fragment compatible field caches. Conservative world/resource/area/configuration/building invalidation remains.
- Integrated current master's optional executor telemetry, preserving movement lanes and teardown. Refreshed generated CLI docs/completions.

## Correctness

97 engine and 71 unit cases passed, including eleven movement cases. The movement matrix compares per-tick checksums and canonical save bytes across K=1/2/4/8 and participants 1/2/4/8. New regressions exercise same-tick invalidation after admission and compare cached compatible building-goal suggestions against fresh computation.

Separate 64-tick release controls compare previous reference, updated reference and updated K=4 shadow execution. Replays, replay checksum files, per-tick world checksums and final saves are byte-identical across all three variants. Ten timing pairs and all four-delay pilot controls also match final checksums.

Five gate-evaluator, 24 CI-policy and five compute-trace Python cases passed. CLI generation check and documentation checker passed. Main-worktree diff is clean after commits.

## Release measurements

One preliminary 1,024-worker colony, seed 713, CPU gradients, four participants, 128 ticks. One whole-run warmup per variant, then ten paired repetitions with alternating order. Timing runs do not export replays/saves/checksums. Setup is excluded from engine run_ns; the simulated startup phase is included. The previous executable is preserved and hashed.

Mean shadow engine time: 9,665.895 ms before, 3,330.522 ms after. Geometric mean paired after/before ratio: 0.34439, or 65.56% less elapsed time. Paired bootstrap over ten log-ratios, 20,000 resamples with RNG seed 713: 95% reduction interval 64.11–66.88%. This interval describes run-to-run timing on this one input, not workload/seed variation or gameplay effects.

Across all 128 ticks: captures 257 -> 129; fields 1,836 -> 1,086; expanded cells 27,811,718 -> 16,480,647. The longer window includes later resource changes, so the initial 16-tick reduction must not be extrapolated over whole matches. Before/after timing includes the integrated master executor telemetry change (default off); not every elapsed difference can be attributed solely to the three movement fixes.

The complete pilot repeats four preliminary fixtures and delays 1/2/4/8 (20 runs). All K fail the unchanged gate somewhere in the corpus. Ordinary-control and four-team owner capture/admission cost remain above the generous removable-cost bound at every K. Large-colony K=1 is near the bound; K=2/4 exceed it. K=8 misses 95% coverage in applicable large fixtures. Some one-shot cost ratios below 1 are insufficient for acceptance or precision claims. Every fixture remains unqualified for long-term sustainability/gameplay. No live rule is enabled and no Stage B/C progression is allowed.

## Callgrind

Matching 16-tick large-colony K=1 profiles use original optimized objects relinked without stripping, four participants, Valgrind 3.26.0, Ir events, separate thread output. No DWARF line tables. Callgrind elapsed time is not performance evidence. Whole-program counters include setup; named function costs isolate relevant work. Exclusive categories and caller->callee counts avoid double-counting nested work.

- Captures: 33 -> 17. Store capture inclusive Ir: 13,903,560 -> 9,210,468 (33.8% lower).
- Legality -> map-view calls: 82,208,713 -> 0. Planning/search -> map-view calls after: 2,678.
- Fields: 637 -> 64. Expanded cells: 9,621,248 -> 966,656.
- Route-field construction inclusive Ir: 26,080,396,710 -> 1,431,812,413 (94.5% lower).
- Final checksum in both profiles: 33499183.
- Admission/boundary bookkeeping remains substantial; owner boundary inclusive Ir does not improve (13,362,555 -> 14,177,122, also reflecting updated executor code).

## Limits and recommendation

Retain the optimized diagnostic prototype in draft and stop at Stage A. Next investigation should target owner admission/snapshot extraction and invalidation-driven field rebuilds. Do not infer live speedup from faster shadow execution.

No macOS arm64, Windows x64 or browser serial/threaded comparison; no qualified sustained corpus, thirty gameplay seeds, footage review or allocator-inclusive 160 MiB acceptance. No save/replay/network format, compatibility floor or SIM_REVISION changes are needed for these diagnostic changes; gameplay movement remains authoritative and unchanged.

Commands, executable/fixture hashes, raw samples, gate output, profiles, correctness artifacts and JUnit XML are retained alongside this report. The evidence archive excludes executables, object files and personal profile files.
