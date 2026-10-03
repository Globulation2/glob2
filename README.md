# Probability victory validation

Evidence for PRs #358, #361 and #363. `provenance.json` identifies the final stack revision and source tree.

Local results: 51 Python estimator/model/telemetry tests; 573 headless unit cases; 30 focused native cases covering probability arithmetic, condition version gates, alliance outcomes, Scene copies and multiplayer verification; production CLI save/reload and parallel continuation checks; custom-game preference/replay integration and visual captures. Logs and JUnit records retain the actual results.

The enabled-rule golden fixture checks 65 consecutive simulation checksums through adjudication at tick 5120. It is selected for secondary native compatibility runs; foreign-platform agreement is pending hosted verification. The standard multiplayer fixture was regenerated for save format 129, protocol 52 and SIM_REVISION 2.

`probability-cli/` retains the generated map, saves, replay, full tick records and exact command arguments from the probability-rule CLI check. Its checkpoint continuation compares complete tick records with uninterrupted execution. `screenshots/` contains the rule control and all sixteen spectator rows at 640×480.

Hosted primary GCC 13, secondary native and browser verification remain pending. The inherited coefficients have not been recalibrated against current AI/rules or human play; historical calibration claims describe the original campaign. No human gameplay review was performed by this validation run.

PR #358 merged after its current ready-PR gate passed. The remaining stack was rebased onto that merge and refreshed against master 5c46e832b, whose source inventory and engine initialization changes overlap the stack. The client and all test harnesses rebuilt successfully; all local checks above were rerun except the unchanged Python suites. The focused native JUnit file contains 30 native cases plus the preference/engine/replay integration case.

Final review found that assigning an early probability loser could change inputs for later teams. The retained failing regression log demonstrates the original error; the fixed regression passes all six orderings of three teams. New probability losses are masked while deciding a sample, with physically dead and other-condition losses still excluded. No cache or saved state was added.

Hosted Windows verification exposed an outdated assertion that current VERSION_MINOR must be 128. The repaired boundary test accepts the new optional-rule format and directly rejects current + 1; the full shared JavaScript corpus, 30 focused native cases and 573 headless unit cases pass after this test-only update. Their results were rerun at the revision recorded in provenance; other local evidence is from its parent, with identical production code. `hosted-first-attempt/` retains both the Windows failure and a separate Mesa llvmpipe mutex race from the windowed sanitizer job. The headless sanitizer game completed 600 ticks. Hosted verification remains pending on the repaired revision.
