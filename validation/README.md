# Local validation and remaining limits

Production head: `0f37e4aa9d86ba82b9872a12eaa4402dcff22c0e`.
Base: `486a2230a4ddb257ba9dee34d3aace05f2260d6f`; fetched again after validation and unchanged. Isolated PR checkout; no merge/rebase required.
macOS 26.6.2 ARM64, Apple M3, Apple Clang 21.0.0. See `environment.json`, dependency linkage and build logs for configuration and executable hashes. These are development correctness runs, not performance measurements.

Build:
```sh
PYTHONPATH=. python3 tools/dev_build.py engine-tests unit-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2
```
Development flags: `dev_fast=1`, `-O0 -g1`, linker auto, compiler caching, no PCH/unity. Runtime/development dependency manifests are unchanged by the PR.

## Focused results

- Final-head core run: **171 cases passed**, exit 0 (`core.log`, `core-junit.xml`). Includes per-team quota isolation/no borrowing/FIFO, 16-team executor horizon, all 512 pending slots, 288-job binary/text save round trips, old byte-sized pending counts, fixed publication deadlines, reader invalidation, snapshot kernels, worker determinism and replay acceptance boundaries.
- Save-format run: **75 passed, one stale test assertion failed** at `1c79d05611544b6fe2b4b689b9bed8f9448904ed`. The assertion expected the former replay floor. Updated that test to format 168; its final-head rerun passed, exit 0 (`save-gate-final.*`). Includes legacy save loading and whole-game hiring continuation across all publication phases. Exact original command: `save-command.json`.
- Turn/network run: **17 passed, one 120-second timeout** at `1c79d056...` while other tests ran. The hostile-order fuzz case passed isolated at the final head in 57 seconds, exit 0, with a 300-second budget (`network-fuzz-final.*`). Other cases cover record verification, reload repair, stalls, latency/jitter/loss and map/script installation independence.
- These represent **265 distinct passing cases after the targeted reruns**. Production source is identical between `1c79d056...` and the final head; the only final amendment updates two test files' hardcoded replay-floor expectations. Initial failures/timeouts are retained rather than overwritten.
- Regenerated the committed match record and trace with the fixture-update command below, then verified through the turn suite. `check_sim_revision.py --base origin/master` passes (`sim-version-final.log`).
- Whole-game traces agree byte-for-byte with one versus four compute participants: Holiday Island 2, four AI teams, seed 73, 4096 ticks; Oazis, eleven AI teams, seed 73, 2048 ticks. Both world checksum and entity checksum sidecars match. `game-check-summary.json` records hashes; scripts and per-run commands/results are archived.
- The optimized prototype's uninterrupted Holiday Island team4 trace also matches the production implementation exactly for all 4096 ticks.

Exact focused commands:
```sh
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'GradientPipeline/*' --filter 'BuildingGradientInvalidation/*' --filter 'MapGradientInvalidation/*' --filter 'GradientPreparation/*' --filter 'ReplayStepCounter/*' --filter 'UnitCustomization/*' --filter 'JavaScriptCompatibility/*' -j4 --junit artifacts/team-quota-pr/core-junit.xml
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'TurnEngineHarness/*' -j3 --junit artifacts/team-quota-pr/network-junit.xml
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'TeamStatsSave/32 sampling phases*' --timeout 300 -j1 --junit artifacts/team-quota-pr/save-gate-final-junit.xml
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'TurnEngineHarness/a client sending hostile orders*' --timeout 300 -j1 --junit artifacts/team-quota-pr/network-fuzz-final-junit.xml
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --update-fixtures --filter 'TurnEngineHarness/the committed*' --junit artifacts/team-quota-pr/golden-junit.xml
python3 test/check_sim_revision.py --base origin/master
```

## Known continuation limitation

A separate structured CLI Holiday Island save-at-2048/load-to-4096 comparison **does not match** the uninterrupted trajectory. It differs immediately at loaded tick 2048, for both a final save and a periodic checkpoint. The same check fails with the earlier optimized benchmark binary using **both the original global-four policy and team4**. Therefore this check is not claimed as a pass or as a newly introduced PR regression. Its root cause remains uninvestigated beyond that baseline reproduction. Direct pipeline save/load and the existing whole-game continuation harnesses pass; they do not establish that this CLI scenario is fixed.

`run_game_checks.py` stops at the first continuation assertion; `run_checkpoint_checks.py` records the checkpoint mismatch and completes the Oazis participant comparison. Baseline scripts reproduce the failure on the frozen prototype, with commands and binary hashes. The archives retain the saves and checksum traces for follow-up.

## Evidence layout and omissions

`harness-artifacts.tar.gz` retains test-generated saves, match records, verification traces and continuation fixtures. `games.tar.gz`, `baseline-games.tar.gz` and `baseline-global4-games.tar.gz` retain CLI commands, results, saves and checksum sidecars, excluding user profiles. `build*.log` and JUnit reports retain build and test output, including initial failed attempts. Benchmark methods, seeds and optimized-prototype sources are in the sibling `benchmarks/` directory.

No Windows/Linux/browser/mobile execution or cross-platform checksum equivalence was performed here. No rendered real-time jitter or weaker-device measurements, final-production release performance run, packaging checks, or maintainer playtest are claimed. Those gaps and the CLI continuation issue are reasons to keep the PR in draft. The focused checks address admission ordering, bounded executor capacity, pending-state persistence, worker determinism and version gates. Maintainer acceptance remains pending.
