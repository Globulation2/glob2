# Maxima school and pool validation

The school fix removes both the reachable-local-algae demand gate and the discovered-algae placement gate. School targets rise from 1/2 to 2/4, population threshold falls from 50 to 45, and the high-technology threshold falls from 72 to 65. Training-building construction staffing rises from 4 to 5 (schools and barracks). Pool utility/arbiter thresholds fall from 43/30 to 20/15.

This favors earlier and more technology construction and increases its material and labor demand. Workers still need actual construction resources. Existing saves retain their saved parameter values; the school logic fix applies when they resume.

## Recorded checks

- `economy-before.log`: new no-algae regression fails against the original AI object at `desired_schools>0`.
- `economy-after.log`: the economy suite passes with the final code and defaults.
- `configuration.log`: all 14 strategy configuration tests pass with the final defaults.
- `policy.log`: all 12 structural policy tests pass. Stale placement and food-ledger numeric inventories from earlier changes were refreshed without changing those implementations.
- Release client build and `git diff --check` pass on macOS.

Commands:

```sh
scons -j4 release=1 server=0
GLOB2_USER_DIR=/tmp/maxima-validation python3 test/run_maxima_implementation_regressions.py --reuse-built-objects --test MaximaEconomyRegressionTest --test MaximaStrategyConfigTest
python3 -m unittest discover -s test -p MaximaStrategyPolicyTest.py
```

## Saved-game observation

The telemetry excerpts retain director snapshots and placement lifecycle records from `maxima_upgrade_test.game` (save 107, seed 1789441453, initial tick 32891). The private source save and final save are not included; these are recorded observations, not a standalone replay fixture.

Before: no schools requested and upgrades disabled. After: two school sites observed at ticks 33468 and 33567, barracks upgrades observed at ticks 33567 and 33663, and one barracks upgrade completes at tick 35983. The run used the saved settings, before the new default school targets were applied. School completion was not observed within this run.

```sh
build/src/glob2 --run-game --load-game /path/to/maxima_upgrade_test.game --ticks 36000 --output-dir /tmp/maxima-school-fixed --telemetry maxima --telemetry team-timeline --save final
build/src/glob2 --run-game --load-game /tmp/maxima-school-fixed/final.game --ticks 36064 --output-dir /tmp/maxima-school-reloaded
```

Both load/continuation commands succeeded. No save fields, order payloads or engine simulation rules change. AI decisions intentionally change on continuation; recorded orders remain the replay/network interface. Cross-platform checksum equivalence was not tested. The requesting maintainer played the installed changes, reported that they worked well, and explicitly approved merging them.
