# Maxima open-ground rally evidence

Local platform: macOS. Build:

```sh
scons -j6 release=1 server=0 build/src/glob2 maxima-continuation-test
python3 test/maxima/run_maxima_implementation_regressions.py --reuse-built-objects \
  --test MaximaCombatIntegrationTest --test MaximaTacticsStandaloneTest \
  --test MaximaLifecycleTest --test MaximaStrategyConfigTest
build/src/MaximaContinuationTest
```

`before.txt` records the new occupied-ground assertion failing against the old
combat implementation. `regressions.txt` records all four selected suites passing
after the change. The combat suite exercises:

- A four-tile rally on open ground near a friendly inn.
- Twenty warriors moving through the real simulation, with fifteen arriving on
  distinct legal tiles within the six-tile readiness radius and launching on the
  next review, including toroidal wrap.
- Fifteen warriors outside the flag but within two extra tiles counting as ready;
  a group farther away does not launch.
- A reachable nine-tile pocket rejected as too small; another friendly inn used.
- A new building on the rally triggering relocation to unoccupied ground.
- Existing wave growth, stalled assembly, multiple waves and saved-wave restoration.

`archive.txt` records the binary/text continuation archive regression. No new
saved fields or version gates are introduced. Existing saved strategies keep their
resolved radius; new default strategies use four tiles. Rally policy changes the
AI's future orders, not engine order execution or replay playback. No full-match
strength comparison was run. Bradley play-tested the standalone PR GUI build and
explicitly approved merging. This is author play-test approval, not independent
review.

The movement fixture skips normal `setGameHeader` setup, leaving the player-wait
flag uninitialized. On Ubuntu 22.04 no warriors arrived because `syncStep` could
be skipped. The fixture now explicitly clears the wait state and asserts that
every requested tick advances. The arrival check also uses the approved two-tile
readiness margin; widening that assertion alone did not resolve the CI failure.

The movement regression resets the simulation RNG to seed 5489 for each of its
two scenarios (ordinary placement and a 52-tile toroidal translation). It compares
every tick's heavy engine checksum with the retained macOS baseline in
[`rally-movement-checksums.txt`](../../../test/maxima/fixtures/rally-movement-checksums.txt).
Each row contains translation, simulation tick, and checksum. Both Linux CI jobs
run this comparison, so a pass requires the same checksum at every simulated tick,
not merely the same eventual launch. Windows CI builds the game but does not run
this native Maxima harness; Windows checksum equivalence remains unverified.

To deliberately regenerate the baseline after reviewing a simulation change:

```sh
GLOB2_RECORD_RALLY_CHECKSUMS=1 python3 test/maxima/run_maxima_implementation_regressions.py \
  --reuse-built-objects --test MaximaCombatIntegrationTest
```

Normal test runs must leave that variable unset. `regressions.txt` retains the
local comparison and related regression results.
