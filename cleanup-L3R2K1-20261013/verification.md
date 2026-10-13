# Reviewed turret cleanup: final verification

Tested head: `83378886827bf1067a97d62fb056d3653265ce9b`. Integrated base: `35b84d4ffd774742ada41d33c39ab1a50551f113` (refetched before final validation; unchanged).

Normal targeting remains L=3, R=2, K=1, margin=1. This cleanup preserves the selected gameplay and format159 save bytes; SIM_REVISION remains55 for the complete adoption PR. Old-match compatibility and changed targeting were explicitly accepted by the maintainer.

## Cleanup and review

Removed runtime preparation/cooldown ablations, the special output-capacity reservation, duplicate captured attack-strength storage, unused admission scanning and redundant snapshot requirement aliases. One normal preparation path shares immutable damage rows and reads effective attack strength directly from UnitView. Diagnostic clocks execute only when profiling is enabled. Saved-rule validation uses a pure helper instead of allocating a temporary Pipeline and casting away const. Two true reserved format159 bytes preserve normal save/checksum continuity and cannot enable retired research variants. A new malformed-settings test verifies rejection of false and noncanonical flags.

Removed an obsolete shared-cycle microbenchmark whose original label no longer represented the old implementation after adoption. The dedicated whole-engine immediate-kernel control, diagnostic and cached benchmark remains. Parameter tuning stays in research harnesses; ordinary games retain L3/R2/K1.

Independent subagent /root/review inspected both adapters, sparse enumeration/ranking, index reuse, source supersession/compatibility, first-valid firing, publication, failures, save/drain deadlines, Game integration and measurement documentation. Four actionable findings and minor follow-ups were addressed. Second review and final fixture review closed with no outstanding blocker. Source inspection only; reviewer did not run tests or edit files. Full feedback is in cleanup-review.md.

## Exact-head results

- Release engine-tests and turret-engine-benchmark build: pass, cleanup-verified-build.log.
- Focused engine registry: **145 passes, zero failures, one display-dependent skip**, cleanup-final-focused.xml/log. Covers targeting/scan geometry, occupied index, captured modifiers, cooldown admission, deadlines, failure handling, sources/slot reuse/alliances, current saves and retired flags, snapshots/executor, statistics integration, catalog headers, direct/deferred stepping, replay acceptance/step counters and multi-client restart/repair/verifier behavior.
- Whole-engine continuation: **27/27 cuts**, **16,129 assertions pass**. Immediate diagnostic control, shadow and cached modes each test cuts1,2,4,8,16,38,39,40,41. CSV preserves every result.
- Whole-engine traces: **9,216 assertions pass**, three workloads with participants1/2/4/8. Cached checksums/orders/final saves agree across worker counts; diagnostic agrees with immediate-kernel control. The control is not a legacy CPU baseline.
- Cleanup equivalence against47fa805ed: **nine checksum-CSV/final-save files byte-identical** across the three workloads, including cached and immediate-control final saves. cleanup-final-equivalence.json preserves each comparison. No new golden regeneration is necessary for this semantic-preserving cleanup; the simulation55 committed record is verified by the focused run and version guard.
- Simulation version guard and `git diff --check`: pass.

Initial cleanup focused run had139passes/sixfailures/onedisplayskip: six scheduling fixtures had relied on forecast=false and started with cooling turrets. Corrected the fixtures to explicitly start ready, preserving their admission/publication purpose under the normal forecast. There was no production change after the initial cleanup. Failed logs and reruns are retained; final checks above pass.

## Environment and limits

Linux x86_64, GCC13.3.0, C++20, release=1/dev_fast=0, -O3. SDL3 prefix `/home/bradley/glob2/build/sdl3/prefix`; managed dependency prefix build-6b62ea86279899032d0eabbb. Exact binary hashes, platform and provenance in cleanup-final-provenance.json and logs. Own heavy processes confined to cores8,10,12,14; affinity was not exclusive and other host work was left alone. These are correctness runs, not timing samples.

No final L3 CPU/process-memory/whole-engine improvement claim. Preserved selection evidence is at evidence commit ede20215ceb2e71f0e3330b9c3e8af362b538b7a / selected-L3R2K1-20261013. Earlier timings are for other configurations/bases and cannot substitute for final L3 qualification.

The prior broader integrated headless run had1460passes and fivefailures; two header/statistics test assumptions were fixed before this cleanup. Remaining known failures are the rendering assertion reproduced on the old binary and two JavaScript fixture replay comparisons the maintainer asked not to pursue. Prior unit inventory had1083passes, one resource-catalog size-limit failure and89skips. These are preserved in evidence commit79eeae059a50c6bcb268d51423ac8053ba8f7bdf / consolidated-L3R2K1-20261013. No all-suite green claim; unrelated broad/unit cases were not repeated for this narrow cleanup. Engine and benchmark harnesses affected by the configuration API were rebuilt and checked. Cross-platform checksum equivalence, display/GPU coverage, packaging and gameplay review are unverified.

## Commands

```sh
taskset -c 8,10,12,14 env GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py dev_fast=0 release=1 -j4 engine-tests turret-engine-benchmark

taskset -c 8,10,12,14 python3 test/run_tests.py --binary engine --build-dir build/linux/client/release/dev-linker-auto --no-display --exclude-tag benchmark --exclude-tag callgrind --filter 'TurretTargeting/*' --filter 'TurretScanTile/*' --filter 'WorldSnapshot/*' --filter 'ComputeExecutor/*' --filter 'TeamStatsPipeline/*' --filter 'TeamStatsSave/*' --filter 'UnitCatalog/*' --filter 'SimulationReadPhase/*' --filter 'ReplayStepCounter/*' --filter 'TurnEngineHarness/*' -j4 --artifacts artifacts/turret-targeting/consolidation/cleanup-final-focused --junit artifacts/turret-targeting/consolidation/cleanup-final-focused.xml

taskset -c 8,10,12,14 env GLOB2_TEST_ARTIFACTS=artifacts/turret-targeting/consolidation/cleanup-final-continuation build/linux/client/release/dev-linker-auto/test/glob2-turret-engine-benchmark --test-case='whole engine save continuation*' --no-colors=true

taskset -c 8,10,12,14 env GLOB2_TEST_ARTIFACTS=artifacts/turret-targeting/consolidation/cleanup-final-traces build/linux/client/release/dev-linker-auto/test/glob2-turret-engine-benchmark --test-case='whole engine cached traces*' --no-colors=true

python3 test/check_sim_revision.py --base origin/master
git diff --check
```

The manifest records source/base revisions and archive/chunk SHA256. Concatenate cleanup-evidence.tar.gz.part-* in lexical order, verify archive SHA256, then extract. Logs/XML, source review, per-tick traces and final saves are included. Earlier failed fixtures and builds are retained separately from final-head results.
