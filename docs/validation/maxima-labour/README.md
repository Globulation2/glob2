# Maxima labour and combat validation

Native macOS validation for the Maxima-only branch. Compressed version-108 and
version-109 checkpoints, whole/resumed traces, and `commands.json` retain a
1,300-tick continuation comparison and a real older-Maxima save loading check. `tests.txt` records the
regression runner results; `replay-boundaries.txt` records the replay floor,
ceiling and decoding tests. The native tournament compatibility test checks
per-player configuration, network header round trips, and old/current protocol
acceptance. Save version 109 retains the readable-save floor at 58; protocol
34 prevents peers using the old Maxima decisions from joining.

Reproduce after a release client build:

```sh
python3 test/run_maxima_implementation_regressions.py --reuse-built-objects --test MaximaEconomyRegressionTest --test MaximaCombatIntegrationTest --test MaximaLifecycleTest --test MaximaImplementationIntegrationTest --test MaximaLabourStandaloneTest --test MaximaReconStandaloneTest --test MaximaStrategyPolicyTest
scons release=1 server=0 tournament-compatibility-test
build/src/TournamentCompatibilityTest
scons -C test ReplayStepCounterTest
test/ReplayStepCounterTest
```

The economy fixture checks the old version-108 execution layout and saved
allowances before an UpdateSwarm event. The lifecycle fixture compares 300
resumed orders and per-tick simulation checksums on each of two seeds. The
combat fixture follows a level-1 attack through pending creation and subsequent
reviews. Historical tournament figures in the research notes were not rerun.

Linux/Windows checksum equivalence and human gameplay assessment remain
outstanding. These local checks do not establish unchanged gameplay balance.
