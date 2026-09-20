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
  distinct legal tiles and launching on the next review, including toroidal wrap.
- Fifteen warriors outside the flag but within two extra tiles counting as ready;
  a group farther away does not launch.
- A reachable nine-tile pocket rejected as too small; another friendly inn used.
- A new building on the rally triggering relocation to unoccupied ground.
- Existing wave growth, stalled assembly, multiple waves and saved-wave restoration.

`archive.txt` records the binary/text continuation archive regression. No new
saved fields or version gates are introduced. Existing saved strategies keep their
resolved radius; new default strategies use four tiles. Rally policy changes the
AI's future orders, not engine order execution or replay playback. No cross-platform
per-tick simulation checksum comparison or full-match strength comparison was run;
playing the resulting pacing remains part of independent human review.
