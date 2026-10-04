# AI custom-rule qualification evidence for PR 678

Tested feature revision: `39bab83c967239e38e1310f12650c988501831fd`.
Integrated base: `69db1fb5d` (native/browser build repair). Pre-change comparison: `79229b3101c6bd6e0c83abb5ed55579834595785`.
Latest master inspected: `429f505684338b5676356e5f32817d4f8f362db3`. It merges without conflicts (tree `6a3bd55513f4bb029af1eceb3060078d287ebd31`). Its changed files were overlaid from that merge tree for a successful Linux build and 10 focused engine tests. Feature checkout remains on its reviewed base; the newer language/font work does not change simulation.

## Results

- 144 native games and 18 JavaScript games, nine profiles, fixed seeds, swapped seats, alternating SmallForTwo and balanced_for_2 maps, 30,000-tick cap: all successful, all replay audits successful; 3,653,518 simulation ticks. Every controller emitted useful work. No disabled training visits or live order-selection violations.
- Linux: 91 relevant engine cases plus 18 farm cases passed; 25 real-binary CLI cases passed. macOS: 82 relevant engine cases and 25 CLI cases passed. These include save/load continuation, historical saves, current and historical AI state, JavaScript profiles, replay boundaries, multiplayer verification, authoritative training/upgrade checks and repairs. Touch checks ran on Linux with Xvfb.
- 16 standard native games match the original revision's complete per-tick detailed traces: 359,914 ticks, no differences.
- 24 affected scenarios, all eight AIs across no upgrades, no hunger, peaceful, no regrowth, combat modifiers and combined disabled mechanics: identical complete checksum files on macOS arm64 and Linux x86_64, 4,096 ticks per scenario.
- Python tournament adapter: 25 cases passed. Translation validation passed with two intentionally pending new strings in each non-English catalog. Sim-revision/golden contract passed. The golden record and verification trace were regenerated together.

## Environments and commands

macOS Darwin 25.6.0 arm64, Apple Clang 21.0.0; Linux Ubuntu 24.04 x86_64, GCC 13.3.0. Both used the repository's pinned SDL dependency configuration 11. Builds used `release=1`, platform-native SCons flags recorded in test provenance; compute tests and games used one worker and no compute experiments.

```
GLOB2_SDL3_PREFIX=<pinned-prefix> scons release=1 -j8 engine-tests <native-build-dir>/src/glob2
GLOB2_TEST_AI_RULE_AUDIT=1 python3 test/run_tests.py --build-dir <native-build-dir> \
  --filter 'AIRules/*' --filter 'AIStateContinuation/*' --filter 'CastorContinuation/*' \
  --filter 'JavaScript*/*' --filter 'TurnEngineHarness/*' --filter 'ReplayStepCounter/*' \
  --filter 'GameHeaderTextSaveLoad/*' --filter 'GameGUITouch/*' \
  --filter 'RoundTripHungerGate/*' --filter 'NicowarFarming/*' --filter 'CortexUpgrade/*'
python3 test/run_tests.py --build-dir <native-build-dir> --filter 'FarmAreas/*'
python3 test/tournament_cli_integration.py --binary <native-build-dir>/src/glob2 --output <new-evidence-dir>
python3 test/check_sim_revision.py --base 69db1fb5d
python3 test/test_tournaments.py
python3 data/check_translations.py --strict
python3 test/test_translations.py
```

Copy the included temporary qualification scripts into `artifacts/ai-rules/` of a checkout before running them. `plan.json` and individual `qualification.json` files preserve every exact command, rule, seed and binary hash. The audit environment variable is enabled by tournament.py. Qualification uses 30,000 ticks; baseline mode uses the same native standard games. `cross.py` loads the published initial saves with a 4,096-tick cap. `compare-default.py` streams detailed tick records rather than comparing gzip headers.

## Evidence inventory and limits

`qualification/` contains all 162 initial/final saves, compressed replays, stdout telemetry, replay audits and structured results. `qualification-manifest.json` records original byte counts and hashes, including every full tournament checksum file. Full tournament traces remain retained in the Linux qualification directory; the evidence branch publishes a full 30,000-tick baseline/candidate trace pair and all 24 complete cross-platform trace pairs. Gunzip `.replay.gz` before replaying. The raw `.checksums` format is GCS1; `test/tournament_cli_integration.py` provides its parser. `cross-comparison.json`, `qualification-analysis.json` and `logs/default-comparison.json` summarize the checked comparisons.

The full hosted matrix, Windows, Android and browser simulation equivalence were not run. A first Linux host had compiler internal errors; qualification instead completed on the clean GCC 13.3 host. One Xvfb resize check failed once under load and passed on retry and again in the final 91-case run. An initially missed local copy of the regenerated golden trace was corrected before final validation. Development fixture/audit mistakes were fixed before qualification and are not game-engine defects.

These tournaments diagnose availability and continuation; they do not establish optimal strategy or prove absence of every possible stall. Peaceful or immortal combat variants can remain unresolved at the cap. A maintainer playthrough remains required for review; no human playthrough is claimed. This evidence branch intentionally contains temporary review artifacts rather than permanent product documentation.
