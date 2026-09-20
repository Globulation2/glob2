# Maxima army capacity evidence

The two compressed input saves are the user's `army size` and `army size 2`.
Both have unit upgrades enabled. Their original files were inspected read-only;
all continuations used isolated profiles.

In `army size`, 28 of 30 warriors are untrained, four of five barracks are
upgrade sites, and eight untrained warriors are on a defense flag. The resumed
army target is 120, but the ordinary barracks target is three and the extra-seat
workaround stops at five buildings. In `army size 2`, 25 of 33 warriors are
untrained, two of four barracks are usable, and food-service stress suppresses
the extra-barracks request. The first resumed army target is 78.

The change shifts funded births from surplus workers to warriors, scales training
capacity with the army target, credits the completed capacity of barracks sites,
and keeps working training capacity during further upgrades. Existing upgrades
also retain their category commitments when the upgrade quota shrinks, without
consuming independently authorized new-building slots.

## Saved-game comparisons

Same input state and tick cap for each pair, on macOS. Baseline is the rally PR
build at `2b864d7d23638120cba60c76cc8cea1ee7937da9`; only the army-capacity changes
in this PR differ in the new runs. No seed or saved strategy overrides were used.

Each continuation runs for 20,000 engine ticks:

| Save | Build | Workers | Warriors | Trained warriors* | Barracks* |
| --- | --- | ---: | ---: | ---: | ---: |
| army size | baseline | 240 | 14 | 1 | 3 |
| army size | changed | 72 | 26 | 1 | 4 |
| army size 2 | baseline | 471 | 35 | 12 | 2 |
| army size 2 | changed | 329 | 104 | 44 | 9 |

*Training and building counts are the engine's last periodic statistics sample;
worker/warrior counts are the exact final state. Barracks include sites.

The first save remains under destructive enemy pressure: its new barracks sites
are destroyed, and its population falls much further when surplus worker births
pause. This is not evidence of improved survival or match strength. In the second
save additional barracks actually complete and train the larger force. These are
two diagnostic continuations, not a win-rate study or human play-test approval.

`long-*-result.json` retains the complete structured engine results.
`director-snapshots.csv` retains every director observation from the four runs;
`barracks-events.csv` retains every barracks construction/upgrade/repair event.
The original saves' stored histories are part of the inputs.

```sh
gzip -dc docs/artifacts/maxima-army-capacity/army_size.game.gz > /tmp/army-size.game
gzip -dc docs/artifacts/maxima-army-capacity/army_size_2.game.gz > /tmp/army-size-2.game
# Repeat with the baseline executable and fresh output directories.
build/src/glob2 --run-game --load-game /tmp/army-size.game --ticks 66272 \
  --telemetry maxima --output-dir /tmp/army-capacity-1
build/src/glob2 --run-game --load-game /tmp/army-size-2.game --ticks 58944 \
  --telemetry maxima --output-dir /tmp/army-capacity-2
```

## Regression and compatibility checks

```sh
scons -j6 release=1 server=0 build/src/glob2
python3 test/maxima/run_maxima_implementation_regressions.py --reuse-built-objects \
  --test MaximaEconomyRegressionTest --test MaximaPlacementStandaloneTest \
  --test MaximaDirectorRegressionTest --test MaximaCombatIntegrationTest \
  --test MaximaLifecycleTest
```

`regressions.txt` records the local results, including live/save/reload continuity.
The economy suite tests both army-size conditions, retained food funding, genuine
workforce shortage, full training backpressure, target completion, and issued
upgrade capacity. The placement suite checks that contracted upgrades cannot
consume new-building slots after their quota shrinks.

The economy suite also applies the new swarm mix to the real engine at seed 5489,
with 50 workers and a funded army deficit. It requires warrior births without
additional worker births, and compares every one of 512 ticks with the macOS
heavy-checksum baseline in `test/maxima/fixtures/army-birth-checksums.txt`.
Both Ubuntu CI jobs run that comparison. Windows CI does not run this native
harness, so Windows checksum equivalence remains unverified. The rally checksum
regression remains in the selected test set as well.

To deliberately regenerate the birth baseline after a reviewed simulation change:

```sh
GLOB2_RECORD_ARMY_CHECKSUMS=1 python3 test/maxima/run_maxima_implementation_regressions.py \
  --reuse-built-objects --test MaximaEconomyRegressionTest
```

Normal tests leave that variable unset. No save fields or format versions change.
The policy changes future Maxima orders, including in existing saves; engine order
execution and replay/network acceptance gates are unchanged. Gameplay pacing and
army/economy tradeoffs need human play testing before merge.
