# Maxima save continuation

`checkpoint-30000-v115.game.gz` contains two default Maxima players at tick
30000 on a 128×128 symmetric arena (map seed 42, game seed 19). The expected
JSON maps each of the 512 ticks from 30000 through 30511 to the SHA-256 of its
complete checksum record (tick, aggregate, and ordered team/entity fields) when
continuing that retained checkpoint with the current resource-property simulation and AI policy, including simulation
revision 40. `expected-resources-30000-30512.json` is the active trajectory;
`expected-terrain-30000-30512.json` retains the simulation-revision-20 baseline;
`expected-scoped-gradients-30000-30512.json` and earlier expectations retain the
historical team/entity-only hashes for their earlier simulation policies.
The current baseline includes property-driven renewable Food seed protection,
renewable Wood reserves, the runtime registry/material cache checksum state,
scheduled building gradients, greedy fetching, persistent owner-specific RNG
streams, 30-tick normal timing, building area effects, and delayed resource
growth integrated with the landscape resource catalog. CI compares platform continuations against this same trajectory. The original v115 checkpoint
is retained to keep testing older-save loading. The test also saves at tick 30256,
reloads, and compares all remaining records against the uninterrupted continuation
to verify save/load continuity independently of the fixed baseline. Reload checks
adjust only the MapHeader format-version contribution to the aggregate checksum,
using the versions and team/player counts in both headers. All other aggregate
bits and all team/entity fields must match.

Run the retained regression from the repository root:

```sh
python3 test/maxima/check_save_continuation_fixture.py build/src/glob2
```

To reproduce the original scenario with the current engine and default parameters
(the resulting checkpoint differs when AI policy changes):

```sh
build/src/glob2 --generate-map --generator 15 --map-seed 42 \
  --param teams=2 --param width=7 --param height=7 \
  --write-map true --rotations 1 --output-dir /tmp/maxima-continuation-map
build/src/glob2 --run-game \
  --map-file /tmp/maxima-continuation-map/map-r0.map \
  --game-seed 19 --player maxima --player maxima --ticks 30512 \
  --save every:30000 --telemetry checksums --output-dir /tmp/maxima-continuation
build/src/glob2 --run-game \
  --load-game /tmp/maxima-continuation/checkpoint-30000.game \
  --ticks 30512 --telemetry checksums --output-dir /tmp/maxima-continuation-resumed
python3 test/compare_save_continuation.py \
  /tmp/maxima-continuation/game.replay.checksums \
  /tmp/maxima-continuation-resumed/game.replay.checksums
```

The generated checkpoint is compressed with gzip, with its timestamp set to zero.
The current expected JSON uses `test/check_javascript.py`'s `complete_ticks()`
parser and hashes complete records in `[30000, 30512)`.

To refresh only the resource baseline, run the checker with `--update-fixtures`.
It validates the midpoint save/reload before writing the expectation. Then run
without that flag in both default and `--parallel-ai` modes. The legacy checkpoint
and historical expectations remain unchanged. `--output artifacts/NAME` retains
commands, logs, traces, saves and a hash manifest.
