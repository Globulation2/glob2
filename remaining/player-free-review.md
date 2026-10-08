# Player-free statistics and independent review

Tested source: `0039b69e5b85c539ee43cd00682fafeaea95dd8d`; integrated base `67fd5b935c98c3d6d0c5ba9d25754ce7df0f83f9`. Linux x86-64/GCC15.2 release build with existing frozen dependencies (see [identity](./player-free-freeze.json)).

360 runs: six scenarios × 20 seeds × immediate reference/owner/shared, 512 ticks per run on 64² maps. Two passive teams receive statistics; there are no player seats, controllers, units, buildings or harvesting. Every tick independently scans all material stocks. All accounting and owner/shared per-tick checks passed. Disabled growth remains unchanged; blocked growth adds no deposit tiles; active scenarios perform positive growth.

The stock counters reconcile exactly to actual stock changes. The two passive teams have identical global counters and zero building-coverage counters. Delayed physical tile additions match seed counters; owner/shared heavy checksums and full per-tick global statistics match. Pending-save tests independently reconcile restored stock and statistic increments, and mismatched-world publication verifies rejected-proposal accounting.

## Ecology comparison (mean final total stock across 20 seeds)

| Scenario | Immediate reference | Delayed owner/shared | Difference |
|---|---:|---:|---:|
| dense | 6764.85 | 6568.25 | -2.91% |
| sparse | 435.15 | 409.75 | -5.84% |
| multi | 14893.60 | 12408.05 | -16.69% |
| saturated | 9032.25 | 8823.00 | -2.32% |
| blocked | 4041.65 | 4031.75 | -0.24% |
| disabled | 2570.25 | 2570.25 | +0.00% |

These are descriptive ecology results, not performance claims or proof that delays alone cause the differences. The immediate reference calls the retained old growth pass directly; owner/shared advance full Game::syncStep. RNG schedules, feedback and seed stocks differ. This is not an execution of the latest master binary. Heavy checksums join work every tick, so this does not exercise sustained overlapping batches as a timing run would. Existing deadline/out-of-order tests remain separate.

## Review and cleanup

An independent subagent reviewed the full growth refactor, 144/145 save lineages, worker/snapshot lifetimes, statistics, architecture and documentation. It found no confirmed simulation defect. Applied its findings: documented ownership/lifecycle and exact statistic semantics; clarified the seed fallback and retained reference path; added publishedProposals, because calculated proposals include future work and omit restored outputs. Published proposals now equal accepted plus rejected, including invalidated batches. No gameplay/save/replay/network version change in this follow-up.

## Commands and verification

```sh
CCACHE=1 GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX=/home/bradley/glob2-terrain-art2/build/linux/client/release/recording/prefix scons -j12 release=1 server=0 optimized_assets=0 build/linux/client/release/src/glob2 build/linux/client/release/test/glob2-engine-tests
LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib python3 test/run_tests.py --binary engine --no-display -j4 --filter 'ResourceGrowth/*' --filter 'ResourceGrowthBenchmark/player-free*' --filter 'TeamStatsSave/*' --filter 'WorldSnapshot/*' --filter 'SharedWorkerLifecycle/*'
GLOB2_GROWTH_PLAYER_FREE_OUTPUT=$PWD/artifacts/resource-growth/remaining/player-free-results.json LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib python3 test/run_tests.py --binary engine --no-display -j1 --timeout 600 --filter 'ResourceGrowthBenchmark/player-free*'
LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib python3 test/run_tests.py --binary engine --no-display -j4 --tag golden
```

Focused final tests: 65 passed, 1 display skip. Golden tests: 12 passed. Expanded comparison: 1 test / 360 runs passed. See player-free-final.xml, player-free-golden.xml, player-free-extended.xml and [raw results](./player-free-results.json). Initial compile attempt had a test-only CAPTURE macro arity error; corrected and rebuilt. No timings are inferred from these unreserved correctness runs.

Fetched master `0507700f1dcb4ff444ad2483b3320e9d16ec4ed0` before final validation. It introduces the vertex-terrain model and a new 146 save lineage beyond the integrated 67fd5b935 base. That integration is outstanding, including overlapping SIM 32 allocation; this follow-up does not establish compatibility with the newer master. Previous performance tables refer to their frozen revisions. PR remains draft. Non-Linux, threadless and display execution unavailable; no cross-platform determinism claim.
