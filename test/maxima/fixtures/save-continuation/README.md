# Maxima save continuation

`checkpoint-30000-v115.game.gz` contains two default Maxima players at tick
30000 on a 128×128 symmetric arena (map seed 42, game seed 19). The expected
JSON maps each of the 512 ticks from 30000 through 30511 to the SHA-256 of its
ordered team/entity checksum record from the uninterrupted run. Aggregate hashes
are excluded because they include the save header/version.

Run the retained regression from the repository root:

```sh
python3 test/maxima/check_save_continuation_fixture.py build/src/glob2
```

To reproduce the scenario with the current engine and default parameters:

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
The expected JSON uses `test/compare_save_continuation.py`'s `records()` parser
and hashes only records in `[30000, 30512)`.
