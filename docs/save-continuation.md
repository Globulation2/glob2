# Save continuation repair (format 113)

The Maxima attack investigation exposed engine state that was lost or changed
when loading a game. This affects units and buildings shared by all AIs; the
repair does not retune Maxima or change uninterrupted attack decisions.

The original reproducer was two Maximas, generator 15, map seed 42, 128×128,
game seed 731, normal rules. Loading its tick-30,000 checkpoint first changed
unit 1219's movement at tick 30,001. Preserving unit state exposed a second
mismatch at tick 30,137: unit 1076 selected a different service building.

## Causes and repair

- The map saved clearing reservations, but units discarded their matching claim
  coordinates and arbitration distances on load. A worker could therefore find
  its own old reservation still occupied and choose random movement instead.
- `Unit::load` read `jobTimer` and then reset it to zero. That timer controls
  when an idle unit may seek training or healing.
- Team service lists were rebuilt in building-id order instead of retaining
  their live order. Food, healing and training searches break ties using list
  order. Swarm, turret, flag, construction and hiring lists also have observable
  scheduling order and are now retained.
- Building unit lists were saved forwards and loaded with `push_front`, reversing
  the order of workers, occupants and harvesters.
- Loading called `Building::update` to rebuild derived state. This can reset
  conversion timers, change staffing and advance construction. New saves retain
  service membership flags, hiring bookkeeping and cached resource demand, so
  saved-game loading no longer needs to run that simulation update.

Format 113 stores the missing state and retains ordered lists, including existing
repeated flag references. Counts and references are checked when loading new team
lists. Raw building construction initializes the newly saved resource-demand
cache. The readable save floor stays at 58. Formats through 112 keep their
historical reconstruction behavior: missing historical state cannot be recovered
exactly. This gating also preserves how old recorded-order replay headers load;
the replay floor stays at 99. Network/YOG protocol 38 excludes clients that cannot
read the new continuation state when joining a saved game.

## Validation and evidence

The original two-player game matches every detailed team/entity record over
2,048 resumed ticks at each checkpoint: 10,000, 20,000 and 30,000. A separate
four-Maxima game (map seed 84, game seed 2026) matches 2,048 ticks after its
20,000 checkpoint. These are 8,192 whole-game continuation records in total.

macOS arm64 and Linux x86_64 produce identical full sidecars for ticks
30,000–32,047 from the same repaired checkpoint:
`f39e7f2550b15d96f485ba15dc9a5993f467e2ccb87074bd8f119fe9a7009297`.
Windows runtime equivalence has not been verified locally; CI runs the same
retained checkpoint regression on Linux and Windows.

The focused `UnitContinuationHarness` compares five save points followed by 256
simulation ticks each, ordered service/worker state, clearing claims, idle timers
and the resulting RNG state. All 17 native Maxima suites, 14 configuration tests,
12 policy checks, team-statistics save safety, network/replay acceptance and the
entering-unit compatibility regression passed on macOS. Retained production
version-109 continuation still matches all 1,300 records; version 108 loads, and
a version-112 checkpoint matches its original 64-tick resumed trace.

Retained artifacts are deliberately small:

- [Initial version-112 state](../test/fixtures/save-continuation/initial-v112.game.gz)
  reproduces the complete original match.
- [Repaired tick-30,000 checkpoint](../test/fixtures/save-continuation/checkpoint-30000-v113.game.gz)
  exercises active armies and service routing.
- [512 per-tick SHA-256 values](../test/fixtures/save-continuation/expected-30000-30512.json)
  hash the full detailed team/entity records from **uninterrupted** execution,
  excluding only the aggregate checksum that contains save-header/version data.
- [Validation metadata and artifact hashes](validation/save-continuation/results.json).

Run the retained regression:

```sh
scons -j4 release=1 server=0 build/src/glob2 unit-continuation-test
build/src/UnitContinuationHarness
python3 test/check_save_continuation_fixture.py build/src/glob2
```

Reproduce the longer comparison in a fresh output directory:

```sh
mkdir -p /tmp/glob2-continuation-check
gzip -dc test/fixtures/save-continuation/initial-v112.game.gz > /tmp/glob2-continuation-check/initial.game
build/src/glob2 --run-game --load-game /tmp/glob2-continuation-check/initial.game \
  --ticks 32048 --save every:10000 --telemetry checksums \
  --output-dir /tmp/glob2-continuation-check/whole
build/src/glob2 --run-game \
  --load-game /tmp/glob2-continuation-check/whole/checkpoint-30000.game \
  --ticks 32048 --telemetry checksums \
  --output-dir /tmp/glob2-continuation-check/resumed
python3 test/compare_save_continuation.py \
  /tmp/glob2-continuation-check/whole/game.replay.checksums \
  /tmp/glob2-continuation-check/resumed/game.replay.checksums
```

The four-player scenario uses `--generator 15 --map-seed 84 --param teams=4
--param width=7 --param height=7`, four `--player maxima` arguments,
`--game-seed 2026 --ticks 22048 --save every:20000 --telemetry checksums`.
Resume its 20,000 checkpoint through 22,048 and compare the same way. All commands
use normal rules. These checks establish continuation for the tested scenarios,
not a new win-rate claim or a proof covering every historical AI/save format.
