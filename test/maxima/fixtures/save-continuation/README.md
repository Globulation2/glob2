# Maxima save continuation

`checkpoint-30000-v115.game.gz` contains two default Maxima players at tick
30000 on a 128×128 symmetric arena (map seed 42, game seed 19). The expected
JSON maps each of the 512 ticks from 30000 through 30511 to the SHA-256 of its
complete checksum record (tick, aggregate, and ordered team/entity fields) when
continuing that retained checkpoint with the current unit-property simulation and AI policy.
`expected-units-30000-30512.json` is the active trajectory;
`expected-resources-30000-30512.json` retains the simulation-revision-40 trajectory;
`expected-terrain-30000-30512.json` retains the simulation-revision-20 baseline;
`expected-scoped-gradients-30000-30512.json` and earlier expectations retain the
historical team/entity-only hashes for their earlier simulation policies.
The current baseline includes property-driven renewable Food seed protection,
renewable Wood reserves, the runtime registry/material cache checksum state,
scheduled building gradients, greedy fetching, persistent owner-specific RNG
streams, 30-tick normal timing, building area effects, and delayed resource
growth integrated with the landscape resource catalog, resolved unit definitions,
assigned job purposes and exact cargo state. Flying service selection retains historical geometric-distance ranking on maps
without air terrain constraints or air-blocking resources, and uses shared
air-route distances on constrained maps. The active trajectory includes this
conditional policy. CI compares platform
continuations against this same trajectory. The original v115 checkpoint
is retained to keep testing older-save loading. The test also saves at tick 30256,
reloads, and compares all remaining records against the uninterrupted continuation
to verify save/load continuity independently of the fixed baseline. Reload checks
adjust only the MapHeader format-version contribution to the aggregate checksum,
using the versions and team/player counts in both headers. All other aggregate
bits and all team/entity fields must match.
These are complete checksum records, rather than exhaustive serialized-state
comparisons. The unit phase-continuation fixtures separately compare serialized
caches, private RNG, service membership, reservations and exact cargo fractions.

Run the retained regression from the repository root:

```sh
python3 test/maxima/check_save_continuation_fixture.py build/src/glob2
```

To reproduce the original scenario with the current engine and default parameters
(the resulting checkpoint differs when AI policy changes):

```sh
build/src/glob2 map study 15 --seed 42 \
  --set teams=2 --set width=7 --set height=7 \
  --write-map --rotations 1 --output-dir /tmp/maxima-continuation-map
build/src/glob2 game run \
  --map-file /tmp/maxima-continuation-map/map-r0.map \
  --game-seed 19 --player maxima --player maxima --ticks 30512 \
  --save every:30000 --telemetry checksums --output-dir /tmp/maxima-continuation
build/src/glob2 game run \
  --load-game /tmp/maxima-continuation/checkpoint-30000.game \
  --ticks 30512 --telemetry checksums --output-dir /tmp/maxima-continuation-resumed
python3 test/compare_save_continuation.py \
  /tmp/maxima-continuation/game.replay.checksums \
  /tmp/maxima-continuation-resumed/game.replay.checksums
```

The generated checkpoint is compressed with gzip, with its timestamp set to zero.
The current expected JSON uses `test/check_javascript.py`'s `complete_ticks()`
parser and hashes complete records in `[30000, 30512)`.

To refresh only the current unit-era baseline, run the checker with `--update-fixtures`.
It validates the midpoint save/reload before writing the expectation. Then run
without that flag in both default and `--parallel-ai` modes. The legacy checkpoint
and historical expectations remain unchanged. `--output artifacts/NAME` retains
commands, logs, traces, saves and a hash manifest.

## Ground-looks 191 compatibility

`maxima-groundlooks-v191.game.gz` was written by the unmodified production
save writers at `f36de73aea50901a93d0c931bdd0fdb64c9d864c` (format 191,
simulation revision 113, 33 built-in terrains), using a temporary fixture-only
test. The 32×32 map contains one Maxima player, a school, inn, swarm and
level-2 worker. It paints all four grass/sand looks plus custom bog and mud
at saved IDs 33 and 34. The fixture sets explicit AI continuation markers:
timer 1234, tactical quarantine budget 2468, and pending development and
maintenance-clearing work. Its canonical strategy predates prestige controls.

The director regression loads the complete historical game, checks terrain
identity/properties and retained work, refreshes prestige migration through
`getOrder()`, then saves and reloads the current format. It checks the migration
before and after normal dispatch; it does not relabel current-format bytes as
historical output. The original writer verified both binary and text roundtrips.
Only the compressed binary is retained here; temporary creator/build evidence
belongs outside committed documentation.

Writer SHA-256 values at the producing revision:

- `src/ai/maxima/AIMaximaState.cpp`: `e61530ec56767fbcc1e3f4d5fae97c51dbbb80ec839c3cee60bc7f390ebb63b9`
- `src/map/io/MapIO.cpp`: `c4d752461550a3c99bd31be73c78f16dd46f62a392a1996464e1997b7c2f5b08`

The uncompressed fixture SHA-256 is
`40755760232451f26caca10a3ae6749555b5b6c63e8a83947cfb051240157b59`.
