# Consolidated turret targeting verification

Source: `47fa805ed779d975592422834ae0ff6f69b3a8bf`; integration base: `35b84d4ffd774742ada41d33c39ab1a50551f113`.
Normal targeting is L=3, R=2, K=1, margin=1. Save/replay floor 159, simulation revision 55, map floor 58. The maintainer explicitly authorized adopting cached firing and breaking old-match compatibility; original/new target differences are accepted.

## Results and limits

- Final release build: engine tests, turret engine benchmark and unit tests pass compilation. Linux x86_64, GCC 13.3.0, GNU C++20, -O3, release=1, dev_fast=0. SDL3 prefix `/home/bradley/glob2/build/sdl3/prefix`; managed development dependency prefix `build-6b62ea86279899032d0eabbb`. Binary SHA256 and source/build provenance are in final-provenance.json and individual logs.
- Exact committed head focused engine verification: **144 passed, 0 failed, 1 display-dependent skip**, committed-focused.xml/log. Covers pure kernel/index behavior, truncation, cooldown admission, identity reuse, alliances, source invalidation, persistence, shared snapshots, executor failure/deadlines, statistics integration, catalog headers, direct/deferred boundaries and multi-client restart/network verification.
- Exact committed head whole-engine save continuation: **27/27 cuts**, **16,129 assertions pass**; immediate diagnostic control, shadow mode and cached firing each cover cuts 1,2,4,8,16,38,39,40,41. committed-continuation/continuation.csv preserves each result. Saves drain without moving logical publication deadlines.
- Exact committed head whole-engine worker traces: **9,216 assertions pass**, three workloads, participants **1,2,4,8**, diagnostic control agrees with immediate kernel, cached worker counts agree with cached single-participant checksums/orders/final saves. committed-traces contains per-tick checksums and final saved worlds. The immediate kernel is a correctness control, not a retained legacy CPU baseline.
- Integrated base broad headless sweep: **1,460 passed, 5 failed, 206 skipped**, a511a73113e020df49e2a359ff36a521c978ba37, current-full.xml/log/inventory. Two failures were test assumptions about current header format and initial statistics admission; fixed in final commit and both pass the final focused run. The only subsequent production change is the regenerated match record; the other edits correct these tests. The broad sweep was not repeated after those fixture/test-only edits.
- Golden regeneration: **11 passed, 0 failed**, current-goldens.xml/log, simulation-55 match record retained in source. Golden generation and broad sweep started concurrently; the record writer finished before the broad match-record case, and final focused verification rechecks the committed record with no writers running.
- Exact committed head headless unit inventory: **1,083 passed, 1 failed, 89 skipped**, committed-unit.xml and committed-unit-headless.log. Remaining failure: ResourceRegistry / maximum resource catalog compiles every preparation trait and rejects overflow atomically, throwing resolved definitions exceed 32 MiB. This area is unchanged by turret consolidation; no passing whole-unit-suite claim is made. An initial direct unit invocation accidentally included display cases with the dummy video driver; it was interrupted and superseded by the registered no-display run. Its log is retained.
- Remaining broad failures: TorusRender / game rendering; picking and cache changes in software rendering (same zoom assertion reproduced using preserved pre-consolidation binary in torus-before.log); JavaScriptSimulation / original fixture and economic-planner fixture replay trace comparisons. The two JavaScript cases pass their save-continuation and worker checks but fail replay comparisons. These were not investigated further per maintainer instruction. Ordinary turn-engine record verification, scripted map replay checks and restart/repair tests pass.
- `python3 test/check_sim_revision.py --base 35b84d4ffd774742ada41d33c39ab1a50551f113`: pass; revision matches committed record and golden change. `git diff --check`: pass.
- Linux only. Windows, macOS, browser, Android/iOS cross-platform checksum equivalence, display/GPU checks, packaging and maintainer gameplay review remain unverified. No cross-platform equivalence claim.

## Performance and selection

The selected pre-consolidation L3/R2/K1 shadow report remains on evidence commit ede20215ceb2e71f0e3330b9c3e8af362b538b7a under selected-L3R2K1-20261013. In 868 Numbi/Castor combat opportunities, target/aim agreement was 747 (86.06%), 5 complete misses (0.58%), no censored windows, p95 completed added delay zero ticks. Controlled 14 birth/approach opportunities had no misses and p95 two ticks. Maximum completed stock-combat delay was five ticks; historical single-tick air windows include failures. Missed-window counts are reported separately, not silently removed. These results describe the original-bullet reference trajectory, not final changed battle outcomes.

**There is no completed final L3 CPU, process-memory or whole-engine performance delta.** The maintainer stopped the timing campaign and selected L3. Partial contended processes remain preserved in the previous report. Earlier L8/R1 timings are not attributable to L3 or to the current merged base with new statistics/snapshot work. Final testing here establishes implementation/continuation/determinism, not a new performance qualification. New firing path performs validation of at most three published entries and never ranks or spatially scans on the owner.

## Commands

All heavy commands were confined to our own affinity `taskset -c 8,10,12,14`; other host work was left alone. Correctness runs overlapped other work and are not timing samples.

```sh
taskset -c 8,10,12,14 env GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py dev_fast=0 release=1 -j4 engine-tests turret-engine-benchmark unit-tests

taskset -c 8,10,12,14 python3 test/run_tests.py --binary engine --build-dir build/linux/client/release/dev-linker-auto --no-display --exclude-tag benchmark --exclude-tag callgrind -j4 --timeout 900 --write-inventory artifacts/turret-targeting/consolidation/current-full-inventory.json --artifacts artifacts/turret-targeting/consolidation/current-full --junit artifacts/turret-targeting/consolidation/current-full.xml

taskset -c 8,10,12,14 python3 test/run_tests.py --binary engine --build-dir build/linux/client/release/dev-linker-auto --no-display -j4 --update-fixtures --tag golden --exclude-tag benchmark --exclude-tag callgrind --artifacts artifacts/turret-targeting/consolidation/current-goldens --junit artifacts/turret-targeting/consolidation/current-goldens.xml

taskset -c 8,10,12,14 python3 test/run_tests.py --binary engine --build-dir build/linux/client/release/dev-linker-auto --no-display --filter 'TurretTargeting/*' --filter 'TurretScanTile/*' --filter 'WorldSnapshot/*' --filter 'ComputeExecutor/*' --filter 'TeamStatsPipeline/*' --filter 'TeamStatsSave/*' --filter 'UnitCatalog/*' --filter 'SimulationReadPhase/*' --filter 'SimVersion/*' --filter 'TurnEngineHarness/*' -j4 --artifacts artifacts/turret-targeting/consolidation/committed-focused --junit artifacts/turret-targeting/consolidation/committed-focused.xml

taskset -c 8,10,12,14 env GLOB2_TEST_ARTIFACTS=artifacts/turret-targeting/consolidation/committed-continuation build/linux/client/release/dev-linker-auto/test/glob2-turret-engine-benchmark --test-case='whole engine save continuation*' --no-colors=true

taskset -c 8,10,12,14 env GLOB2_TEST_ARTIFACTS=artifacts/turret-targeting/consolidation/committed-traces build/linux/client/release/dev-linker-auto/test/glob2-turret-engine-benchmark --test-case='whole engine cached traces*' --no-colors=true

taskset -c 8,10,12,14 python3 test/run_tests.py --binary unit --build-dir build/linux/client/release/dev-linker-auto --no-display --junit artifacts/turret-targeting/consolidation/committed-unit.xml --artifacts artifacts/turret-targeting/consolidation/committed-unit

python3 test/check_sim_revision.py --base 35b84d4ffd774742ada41d33c39ab1a50551f113
git diff --check
```

## Evidence recovery

The companion manifest gives source/base SHA, archive SHA256, chunk byte counts and chunk SHA256. Concatenate integration-evidence.tar.gz.part-* in lexical order, verify its SHA256, then extract with tar. The archive preserves successful and failed logs/XML, inventory, per-tick traces and final saves. Historical build failures and diagnostic/import work are included for provenance; only the committed-* final run results above establish final-head coverage. Derived caches and private work outputs are not production assets.
