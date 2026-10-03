# Probability victory validation

Evidence for PRs #358, #361 and #363. `provenance.json` identifies the tested source revision, tree, integrated master and exact validation commands.

Final local results on Linux/GCC 15.2: 1,142 headless native cases pass (526 runner groups); 90 display-dependent cases are skipped by that headless run. Both affected custom-game/statistics display cases pass separately. All eight production CLI tests, the shared JavaScript corpus, 51 Python estimator/model/telemetry tests and CI policy/concurrency/Scene boundary tests pass. ThreadSanitizer completes a 600-tick threaded headless game and a 300-tick windowed game without race reports.

The standard multiplayer record matches save format 131, protocol 53 and SIM_REVISION 5. The enabled-rule fixture compares 65 per-tick checksums through adjudication. `probability-cli/` retains exact command arguments, generated maps, saves, replay and complete tick records, including uninterrupted versus checkpoint-reloaded continuation. `screenshots/` contains the rule control and all sixteen spectator rows at 640×480.

The full native runner used a 1,200-second local wall-clock allowance during heavy shared-machine CPU contention. The final catalog contract case completed in 264 seconds, within its normal 600-second limit; no test assertion or committed timeout was weakened. Earlier catalog timeouts and the transient 100 ms fertility CPU-budget failures are retained in `historical-local-failures/`; the final suite passes that unchanged CPU assertion. `hosted-first-attempt/` retains earlier Windows and sanitizer failures, subsequently addressed by the stack or merged master fixes.

The maintainer explicitly authorized merging on passing relevant local tests while hosted CI is impaired. Hosted GCC 13, foreign native platforms, browsers and cross-platform per-tick checksum agreement have not been verified for this final revision. The inherited coefficients describe the original 1,110-game campaign at 21a9fba9a and have not been recalibrated for current AI/rules or human play. No human gameplay review was performed by this validation run. The optional rule remains off by default.
