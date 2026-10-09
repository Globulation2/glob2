# Victory-condition validation (local evidence)

Final base: `012d57694f790788f3fe3c5e2a08196d236b949a` (fetched master, integrated without conflicts).
Tested uncommitted patch: `final.patch`, SHA256 `308c102abe30df5e29fa31d9f237104b8ce7d40928de4bac5455643f10ad954a` (includes the new OutcomeReason header).
Host: macOS arm64, Apple clang 21.0.0, SCons 4.11.1, Python 3.13; native release build, default dependencies, no PortAudio.

## Build

Initial target: `scons -j6 release=1 tests glob2`.
Final successful build:

```sh
PYTHONPATH=/opt/homebrew/Cellar/scons/4.11.1/libexec/lib/python3.14/site-packages /opt/homebrew/bin/python3.13 /opt/homebrew/bin/scons -j6 release=1 tests
```

See `final-provenance-build.log`. Asset cache inputs were SHA256-verified against the existing local cache. After an interrupted build, existing objects were reused only where exact compiler commands matched; uncertain objects and affected sources were rebuilt. No compiled objects were copied from another checkout.

## Results

Five new regression cases failed against the previous production logic and passed with the fixes; see `baseline-regressions.log`, `fixed-regressions.log`, `baseline.xml` and `fixed.xml`.

Final verification:

```sh
python3 test/run_tests.py -j3 --no-display --filter 'HungryDefeat/*' --filter 'TrappedUnitLifecycle/*' --filter 'WinningCondition*/*' --filter 'WinProbability/*' --filter 'MatchSetup/*' --filter 'LegacyRoundTripSave/*' --filter 'GameHeaderTextSaveLoad/*' --filter 'ReplayStepCounter/*' --filter 'BaseTeamSaveLoad/*' --filter 'EngineSession/*prestige*' --filter 'TurnEngineHarness/the committed*' --junit artifacts/victory/final.xml
python3 test/check_sim_revision.py --base origin/master
git diff --check
```

All 68 individual cases passed, zero failures or skips (`final-tests.log`, `final.xml`). The runner reports 37 grouped entries. The simulation-version gate and whitespace check passed. Golden record regeneration used `python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*' --junit artifacts/victory/golden-update.xml --artifacts artifacts/victory/multiplayer`; see `golden-update.log`. The committed golden was then independently verified. Simulation revision is 41; the golden record was updated and its checksum trace remained unchanged.

Coverage includes last-slot feeding and healing, healthy/starving/no-controller/explorer-only controls, trapped survival/rescue/production recovery, disabled prestige termination, eliminated prestige leaders and negative standings, alliance/draw behavior, probability decoding and victory, save/load continuation (including a format-143 legacy save), replay checksum/acceptance checks, setup/sim-version boundaries, and multiplayer per-tick checksum verification. Hospital recovery save/load compares 512 full-game checksum ticks. Multiplayer verification compares more than 600 ticks across native clients.

## Limits

Cross-platform per-tick equivalence was not run on Linux, Windows or browsers. Manual local gameplay was not performed. Native repeated-seed, save/load and multiplayer checks do not establish cross-platform coverage. No save-format fields or compatibility floors changed. The new simulation revision separates mixed clients; legacy replay trajectories can differ when they encounter the corrected outcomes. Changes can prolong a match when a last unit is recovering or when prestige victory is disabled, and allow a surviving colony to win despite an eliminated colony having a higher score.

## In-game outcome explanation follow-up

The popup now displays a translated reason below the win/loss/draw heading. The snapshot captures the reason key at the simulation read boundary; the render/UI thread only translates that captured key. Results use the same resolver, preserving the existing opponent-left explanation. The resolver accounts for allied victories and the second-pass opponents-defeated label that can follow a timer/probability decision. This follow-up changes presentation only; the earlier fixes still require simulation revision 41.

Final build: same toolchain/flags as above, target `tests build/darwin/client/release/src/glob2`; logs `outcome-final-provenance-build.log` (final), `outcome-reason-repair-build.log` and `outcome-final-build.log`. A targeted reason test exposed a timer being labelled conquest; the resolver was corrected and the full selected inventory rerun.

```sh
python3 test/run_tests.py -j3 --filter 'WorldSnapshot/*' --filter 'SceneExtract/*' --filter 'EngineSession/*prestige*' --filter 'HungryDefeat/*' --filter 'TrappedUnitLifecycle/*' --filter 'WinningCondition*/*' --filter 'WinProbability/*' --filter 'MatchSetup/*' --filter 'LegacyRoundTripSave/*' --filter 'GameHeaderTextSaveLoad/*' --filter 'ReplayStepCounter/*' --filter 'BaseTeamSaveLoad/*' --filter 'TurnEngineHarness/the committed*' --filter 'GUIInteractionCoverage/local victory popup*' --filter 'GUIInteractionCoverage/match and editor dialogs*' --filter 'GUIInteractionCoverage/match dialogs accept*' --junit artifacts/victory/outcome-verified.xml --artifacts artifacts/victory/outcome-ui
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 test/check_sim_revision.py --base origin/master
git diff --check
```

All 127 individual native cases passed, zero failures/skips (96 runner entries). The additional snapshot and display cases cover reason capture/frozen leases, unchanged checksums, late probability display, timer/prestige winners and losers, elimination, scenario/allied reasons, actual local popup text matching results, dialog layout and keyboard actions. Five translation regression tests passed; strict catalog validation found no missing keys, untranslated entries or structural errors in all 33 supported catalogs.

The assigned translation sub-agent reviewed all 231 new values. Esperanto prestige, Basque timer and Hungarian scenario wording were refined. Scenario text was subsequently changed and reviewed in all languages to be neutral for scripted wins and losses. Review was model-assisted multilingual review, not native-speaker certification. No fallback English entries were added.

Visual evidence: `victory-prestige-reason.png` (converted losslessly from the display-test BMP); other dialog captures under `outcome-ui/`. Native cross-platform and manual gameplay limits above still apply. Master was fetched again before final verification and remains the recorded base. The final post-test source edit clarified a comment only; function bodies and catalog values remain those exercised by the 127-case run.

## Committed revision verification

Tested commit `e5b0ddf8b90e47ec2c9c5456218e9a4667f2da19`, base `012d57694f790788f3fe3c5e2a08196d236b949a`. Rebuilt native tests/game with the same release command and reran the complete selected 127-case inventory on the committed revision: all passed without skips. Strict 33-catalog audit, five translation tests and sim-revision verification also passed. See `committed-build.log`, `committed-tests.log`, `committed.xml`, `committed-translations.log`, `committed-translation-tests.log` and `committed-sim-revision.log`.
