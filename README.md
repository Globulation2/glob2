# Verification evidence: finish replenishment delivery trips

PR: https://github.com/Globulation2/glob2/pull/1364

Final source: `912823e185d9f99f9b6e6c813abf69f0e0f57443`. Base: `b3c4e225b3cfa1fc214a29e4e4e6a85f948ba395`. This branch is retained evidence only; it is not merged into the product branch.

- 106 native cases passed:17 focused staffing/replay cases,83 construction/material/market/historical-save regressions,6 multiplayer execution and acceptance cases.
- Final focused checks include saving with active couriers above inventory demand, in both binary and text continuation. Shared stock and extended mixed cargo are exercised.
- Both Chromium153 serial and threaded WebAssembly builds passed all 6 staffing cases and matched all 4,096 native per-tick checksums. See browser/*/comparison.json and native/staffing.trace.
- Ordinary release build succeeded. Release match verification is verified, and checksums.txt exactly matches the committed FourSquares1.verify-trace.txt. The release client also loaded and ran games/cross-replay.game.gz for 1,500 ticks.
- Simulation revision 122 and the regenerated match record pass test/check_sim_revision.py. Save floor 58 is preserved; replay writer/floor 194 reject earlier execution trajectories.
- Independent read-only subagent review found no blocking production issues; its test-strength feedback was addressed. See review.md.

## Scope and limits

Broader native and release verification cover the unchanged production source from f6bcb934b8; subsequent commits only strengthen the continuation test and explicitly include its stream header. Final focused and both browser checks were refreshed for the final source. Windows, macOS, ARM/mobile execution and interactive feel were not verified here. Linux and both browser modes are the measured platform coverage. Hosted checks may still be queued; local validation is the acceptance basis under repository policy and the maintainer's explicit merge authorization.

The optional study-summary.json describes the earlier controlled prototype study, not final-source or held-out whole-game effectiveness evidence. It is retained as context only. The maintainer requested stopping the remaining campaign and finalizing this mechanism.

## Commands

Native: `python3 tools/dev_build.py -Q -s -j8 engine-tests unit-tests`, followed by the focused filters ReplenishmentStaffing/* and ReplayStepCounter/*; broader BuildingProductionCombat, BuildingMaterialDemand, BuildingStock, MarketsV2, LegacyRoundTripSave, historical TeamStatsSave, JavaScriptCompatibility, and selected TurnEngineHarness cases. Exact case inventory and results are in the XML/logs.

Golden update: `test/run_tests.py --filter 'TurnEngineHarness/the committed match record*' --update-fixtures`; then normal verification and `python3 test/check_sim_revision.py`.

Release: `scons -Q -s -j8 release=1`; `glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr --map-file maps/FourSquares1.map.gz --output-dir ...`; compare emitted checksums to the committed trace; `glob2 game repeat games/cross-replay.game.gz --ticks1500 --runs1` (space-separated CLI flags in the actual invocation).

Browser: `python3 tools/dev_build.py target=web web_variant=both -Q -s web-tests`; execute ReplenishmentStaffing via Module.start in Chromium, retain XML and staffing.trace, compare native bytes. Initial build used 4 allocated cores while native work ran, then 12 after native completion; final incremental refresh used 4. All task CPU affinity stayed within CPUs0–11.

Environment/compiler/configuration and source/binary hashes are recorded alongside this file. No runtime dependency changes.
