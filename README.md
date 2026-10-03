# Probability victory validation

Evidence for PRs #358, #361 and #363. `provenance.json` identifies the final stack revision and source tree.

Local results: 51 Python estimator/model/telemetry tests; 573 headless unit cases; 29 focused native cases covering probability arithmetic, condition version gates, alliance outcomes, Scene copies and multiplayer verification; production CLI save/reload and parallel continuation checks; custom-game preference/replay integration and visual captures. Logs and JUnit records retain the actual results.

The enabled-rule golden fixture checks 65 consecutive simulation checksums through adjudication at tick 5120. It is selected for secondary native compatibility runs; foreign-platform agreement is pending hosted verification. The standard multiplayer fixture was regenerated for save format 129, protocol 52 and SIM_REVISION 2.

`probability-cli/` retains the generated map, saves, replay, full tick records and exact command arguments from the probability-rule CLI check. Its checkpoint continuation compares complete tick records with uninterrupted execution. `screenshots/` contains the rule control and all sixteen spectator rows at 640×480.

Hosted primary GCC 13, secondary native and browser verification remain pending. The inherited coefficients have not been recalibrated against current AI/rules or human play; historical calibration claims describe the original campaign. No human gameplay review was performed by this validation run.
