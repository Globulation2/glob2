Local / VM verification

- Tested revision: final PR head `5f0f3abdfd60f5956d67186ec37d2d542f105e6e`, composed of compiled production source `4285ca7ea6db02cd114d10692074e9fa7796f06a` and the exact twelve golden fixture files committed by `5f0f3abdf`. No production changes follow the tested source.
- Base/integration: fetched `origin/master` at `84edb385cd740c3979c06ef901ab4871ea8115c2`. The only base commit absent from the branch changes predictive CI selection; it does not change engine code or native dependencies. The PR is mergeable. No active collectors were rebased or resumed.
- Environment: macOS 26.6.2 ARM64, Apple Clang 21.0.0, SCons 4.11.1, Python 3.14.7; Linux: Ubuntu 24.04.5 x86-64 / GCC 13.3, ordinary O3 release. Exact toolchain output and dependency inputs are in the evidence receipts.
- Configuration: ordinary release/O3, `dev_fast=0 pch=0 unity=0 linker=default`, explicit empty `CXXFLAGS`, CPU backend. No fast/PCH/unity binary is timed. The fitted model is `e828b0a1…`, generated header `d22e1eb7…`; all coefficient sets use bounded deterministic integer inference.
- Risk coverage: exact painted-zone counter edits/load/reset/tiling, first paint and last-tile erase/repaint, Farm-only rules, no empty-zone demand/outcomes/RNG changes, warm admission minimum/reactive/cap/restored queues, cold initialization, saved capture metadata and committed deadlines. Save, replay/network simulation versions, observer equivalence and thread/platform determinism are checked separately from fit accuracy.

Exact native build (same arguments on Linux, platform-owned ordinary dependency prefix):

```sh
scons release=1 CXXFLAGS= dev_fast=0 pch=0 unity=0 linker=default \
  --build=build/candidate-release -j6 \
  build/candidate-release/src/glob2 \
  build/candidate-release/test/glob2-engine-tests \
  build/candidate-release/test/glob2-unit-tests
```

Linux uses its recorded qualified parallelism and platform build-directory names. Build logs retain the actual compiler/linker arguments and dependency prefix.

```sh
python3 test/run_tests.py --build-dir build/candidate-release --binary all --tag golden --update-fixtures
python3 test/run_tests.py --build-dir build/candidate-release --binary all --tag golden
python3 test/run_tests.py --build-dir build/candidate-release --binary all \
  --filter 'AdaptiveGradientState/*' --filter 'DelayedGradientPublication/*' \
  --filter 'GradientLegacyQueue/*' --filter 'GradientMarketTurnover/*' \
  --filter 'BuildingGradientInvalidation/*' --filter 'ReplayStepCounter/*' \
  --filter 'JavaScriptCompatibility/Current simulation rejects earlier replays and enforces acceptance boundaries' \
  --filter 'MapPaintedAreas/*' --filter 'ZoneGradientDemand/*' \
  --filter 'ZoneGradientFarmDemand/*' --filter 'ZoneCountsPersistence/*' \
  --filter 'GradientCollectionCommandLine/*' --filter 'ResourceGradientExploration/*'
python3 test/check_sim_revision.py --base origin/master
python3 tools/cli_reference.py --binary build/candidate-release/src/glob2 --check
python3 -m unittest tools.test_adaptive_gradient_tournament
```

Mac: ordinary release succeeds; 17 goldens pass; 72 scheduled focused tests / 97 JUnit cases pass; version/CLI gates pass. The initial zone-control build also passes 49 JUnit cases, including all 40 MapQuery cases. Admission tooling: 20 tests pass. Refitted model: 42 permanent tests and 4,240 actual-head Python/C++ UBSan prediction vectors pass, with bounds/parity across all experts and the fallback. No production loader acceptance check was weakened.

The runtime driver records all eight exact `game run` argv arrays. It compares cold and mature runs with probes off/on, one/four participants, old179 save normalization, and current save continuation. It asserts actual threshold 24, minimum 32 and demanded cap 1,024. Per-tick world/snapshot/replay checksums, saved bytes and diagnostic CSVs are retained. Linux also passed 17 goldens, 72 focused checks / 97 JUnit cases and eight runtime checks. All 32 raw checksum/save pairs and 16 diagnostic CSV pairs match Mac byte for byte. Linux verifies the exact committed Mac-generated match record, rather than independently generating different standard-library random orders.

Performance: 32 counterbalanced probe-disabled runs (four arms × two common saved states × four repeats) plus six separate diagnostic runs, all terminal without replacement. Final CPU/tick is 0.121302 ms versus master 0.154532 ms (21.50% lower; paired savings 20.18–23.50%). The refit-only timing difference is locally flat. Zone gating removes empty observations without changing raw movement-error/stall totals in these windows; refitting improves four-player movement cost but not every planning outcome. Per-family raw counts, denominators, CPU totals, wall time, ticks/TPS, exact warm saves/seeds and all commands are linked. No remaining savings are labeled pure model intelligence.

Limits: these are two previously examined warm states, not a fresh full-game quality tournament or a population confidence interval. Background activity is recorded; timing is not claimed to use an uncontended physical core. Original capped trajectories lack some late-game coverage. Markets are unmeasured, Guard balancing was off, and zero eligible goals do not prove no painted zones. Windows, browsers/mobile and interactive gameplay feel were not executed for this final revision. Existing state layout 178/save floor 58 remain; simulation/replay gates move to 185/SIM95 as intended.

Evidence: [landing page and full reconstruction inputs](EVIDENCE_URL), including model/filter/exclusion provenance and raw archive verification, native build/test logs, exact saves/replays/checksums/CSV bytes and independent reviews. Hosted cheap/predictive checks are separate from this native evidence and are not claimed as simulation verification.

Author acceptance: this focused evidence covers the changed behavior and compatibility boundaries above; final merge is explicitly authorized by the maintainer. Omitted platform and gameplay coverage remains stated rather than inferred.
