# Simulation verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Wrapped building footprint regression

The `BuildingFootprint` suite (`python3 test/run_tests.py --filter 'BuildingFootprint/*'`)
links the real engine and
round-trips generated fixtures through binary saved games. It checks exact map
occupancy, missing/stale-cell repair, repeated integrity checks, and ground exits
for interior, negative-origin and positive wrapped footprints. It protects the
runtime fix in `fafb5e9a`: the old predicate erased valid wrapped cells on load and
could subsequently abort in `Building::findGroundExit`.

It needs no display, AI tournament tooling, or external save files. Linux CI runs
it on both supported Ubuntu versions.

Saved state and step-by-step before/after reproduction: [PR #165 fixture](../../../test/fixtures/wrapped-building/README.md).

## Immobile unit gradient regression

The `ImmobileUnitGradient` suite (`python3 test/run_tests.py --filter 'ImmobileUnitGradient/*'`)
uses a fresh 64x64 map
and real engine orders to check empty immobile-unit bookkeeping, exact blocked
cells, and immediate building-route invalidation after painting and erasing a gap.
It exercises all seven swim classes on weighted full-map gradients. No display or
external save fixture is needed; normal game data must be available.

Each scenario is its own case (`fresh map has no immobile units`, `immobile unit blocks
its own tile`, `painting forbidden area refreshes gradients`); the latter two clear the
initial occupancy explicitly, so failures in painting or occupancy can be reproduced
independently of the fresh-map initialization bug. Linux CI runs all of them.

## Building gradient invalidation regression

The building invalidation harness also checks that public distance and movement
queries resolve their own inputs, that the array API returns a complete field,
and that idle and invalidated field storage and search queues can be reused
without stale routes.

Building propagation is always lazy. The `PathGradient` unit suite
(`python3 test/run_tests.py --binary unit --filter 'PathGradient/*'`) has an independent heap oracle that
covers all seven swim classes, paused water snapshots, equal-cost movement neighbors,
repeated and reordered requests, interleaved maps, disconnected/capped distances,
toroidal geometry and reused frontiers. The existing eager-kernel cases remain.

The building invalidation and immobile-unit harnesses exercise lazy callers without
configuration switches. The invalidation suite also pauses a field, closes a rival
building ring, then checks that the field retains its old obstacle snapshot only
until the normal refresh deadline. The savegame safety harness covers completion
on save in the standard game path.
CI runs the oracle on Linux and Windows, the lazy invalidation/immobile-unit suites
on Linux, and lazy save continuation on both.

The `BuildingGradientInvalidation` suite (`python3 test/run_tests.py --filter
'BuildingGradientInvalidation/*'`) links the real engine and places every building through `OrderCreate` / `OrderDelete`, so it
exercises `Game::addBuilding` and `Team::syncStep` rather than a test double. On a
fresh 64x64 grass map with two teams, it checks that a cached route field notices
the ground moving under it: a ring of inn sites closed around a site whose field is
already cached stops offering that site to a unit outside, a site placed inside a
standing ring is never offered, and clearing the ring restores it.

Each scenario is its own case (`centre placed inside an existing ring`, `ring placed around
an existing field`, `a rival teams ring cuts off a cached field`, `a ring cuts off a virtual
flags field`); Linux CI runs all of them.

The last two cover what the proximity walk this replaced structurally could not
reach. `ring-other-team` builds the ring as team 1 around team 0's site: the old
invalidation only dirtied the buildings of the team that made the change. It also
pins that the owner's field comes back on the next rebuild the interval allows
rather than on the next lookup, because `Team::syncStep` frees only the demolishing
team's fields. `ring-flag` puts an exploration flag, with its goal disc kept inside
the ring, at the centre: a flag is never written into the building tile grid, so
walking the changed footprint could not discover its field at any distance.

Each scenario has to let `GRADIENT_DIRTY_REBUILD_TICKS` (`src/engine/EngineTiming.h`)
elapse before it can judge a field, and takes the constant from that header rather
than copying it — when the interval was raised from 25 to 100, a local copy here
silently stopped covering it and the regression passed stale fields.

The scheduled cases run the suite's worlds with the default scheduled building
pipeline: worker kernels match the synchronous seeding and search for every route,
swim class and terrain-cost branch; fields publish at fixed deadlines across workers
0/1/2/4/8 and delays 1/4/8; requests are captured at the observation boundary;
partial fields resume from transferred buckets with every depth setting; synchronous
rebuilds, resets, evictions and reused destinations supersede; pending results
survive saves at every phase; access metadata follows the newest capture, in request
order within one tick; area and team-wide resets keep stale fields serving; a
team-local forbidden edit carries pending generations forward; and slow or failing
workers never move publication. `GradientPipeline/*` covers the pipeline template
alone, and `python3 test/check_gradient_pipeline.py BINARY` adds a building pass
forked with `--fork-rule buildingGradientDelay=N` (delays 1/4/8, workers 0/1/2/4/8,
save/resume at every phase, full/table/lazy depth, rejected delays 0 and 9).

To see the harness fail, drop `gradientGeneration[swimClass] != topologyGeneration`
from `Map::buildingGradient`: `ring-after`, `ring-other-team` and `ring-flag` all
fail. `ring-before` passes either way by construction — nothing is cached before
the ring exists — which is why it is not on its own sufficient.

## Forbidden-zone invalidation and escape recovery

The `MapGradientInvalidation` suite (`python3 test/run_tests.py --filter
'MapGradientInvalidation/*'`) checks all seven swim classes against freshly rebuilt fields after forbidden
brushes, including resource-only edits, clearing goals, other teams and previously
stale caches. It also checks resource, terrain, building and immobility transitions,
a depleted escape exit, unreachable pockets and the refresh budget across tick wrap.
Its gradient-stats case checks the lifetime rows of `BuildingGradientStats` (cold,
generation, drop, eviction and end rows with their previous-search figures), the CSV
and JSON exports, and that the statistics leave fields unchanged. `python3 test/test_gradient_depth_fit.py` checks the
[depth model](../../ai/architecture/building-gradient-depth-model.md) fitter on synthetic rows,
that regenerating `BuildingGradientDepthPolicy.h` from the committed summary is a
no-op, and, when a C++ compiler is present, that the header's lookup agrees with the
fitter.

Forbidden edits preserve unaffected walking fields and pending searches; own-team
harvest round trips and clearing destinations still invalidate. Escape fields have
an independent slot every eight ticks, cycling across team/swim combinations.
Uniform-cost classes first check input markers to skip unchanged propagation.
The normal visit interval is `8 * teams * SWIM_CLASS_COUNT`; unsigned tick wrap
can extend one interval to less than twice that bound. This schedule uses the saved
game tick and preserves saved fields, with no new scheduling state.

This changes simulation decisions: replay floor 123 and network protocol 46 separate
it from previous clients. The supported save-format floor remains 58.

## Building expulsion regression

The `BuildingExpel` suite (`python3 test/run_tests.py --filter 'BuildingExpel/*'`)
links the real engine and
checks that a destroyed building puts the units inside it, entering it, or
waiting to leave it back on the map alive (footprint first, then the ring around
it; a unit with no free tile dies), and that the expelled units keep the share of
the meal or healing they had already received while a started meal still costs
the building one wheat. Every scenario then runs real simulation steps and
re-checks `Game::integrity`. It needs no display, AI tournament tooling, or
external save files.

### Savegame safety

Runs as the `SavegameSafety` suite of `glob2-engine-tests`
(`python3 test/run_tests.py --filter 'SavegameSafety/*'`). No display is required; the
runner supplies the disposable profile and working directory.

The harness checks the headless autosave-off default, explicit opt-in, autosave
cadence, the production autosave path, byte equivalence with direct
serialization to a file, successful reload, disabled autosave, truncated map data
from file and memory streams, recovery after failed loads, and oversized map-area
strings. Atomic replacement tests cover callback/open/rename failures and
temporary-file cleanup; background writes cover superseded snapshots and their
finish steps, completion on destruction and failed writes. Autosave bytes, SHA1
included, must match an inline-hashed save, including when the header backpatch
changes hashed bytes, and `Engine::haveMap` must trust a local save only when its
SHA1 matches the host's header.
On POSIX, child processes impose file-size limits to exercise short writes and
buffered flush errors while checking that the previous save survives unchanged.

## Hunger and defeat detection

Runs as the `HungryDefeat` suite of `glob2-engine-tests`
(`python3 test/run_tests.py --filter 'HungryDefeat/*'`) in a disposable profile; the
case owns a live headless GameGUI with preference saving disabled.

A hungry worker or warrior reserves the final inn place during Team::syncStep,
then walks, enters, completes its meal, and exits without being declared defeated.
Each unit type is tested with one wheat (the final food) and ten wheat; the test
continues for 300 ticks after eating and checks refreshed medical status. Checks
include the feeding timer's zero boundary, the actual death winning condition,
and controls for an empty colony, healthy worker, no food, explorer-only reservation,
a fed unit needing unavailable healing, and missing controlling players. All four
feeding cases run twice with seed 110 and
compare every team checksum; printed trace digests support platform comparisons.
The fixture initializes map occupancy and race
data before exercising the real unit activity and movement code.

## Clearing flag resource bounds

The `ClearingFlagGradient` suite (`python3 test/run_tests.py --filter
'ClearingFlagGradient/*'`) runs in an isolated profile whose preferences must stay
unchanged. The regression covers
weighted building gradients, basic-resource switches, fruit, empty tiles,
allocation padding and every swimming class. CI executes it on Linux and Windows.

### Trapped colony elimination

The `TrappedUnitLifecycle` suite (`python3 test/run_tests.py --filter
'TrappedUnitLifecycle/*'`) runs in a disposable profile whose preferences must stay
unchanged; Linux and Windows CI run it.

Normal simulation ticks exercise completed feeding/training behind wood or
wheat, elimination without indoor starvation, active service, open exits,
free units, allied rescue, and hatchery recovery. A stocked hatchery protects
the colony even with production sliders at zero, since the player can change
them; both food and an available exit are required. Repeated seeded runs and
save/load continuations compare per-tick unit state and win/loss results with
an explicit RNG checkpoint. This is a focused regression, not whole-game replay
compatibility. Version 93 rejects older replays because elimination timing changed;
older saves remain loadable.

## Resource-fetch target regression

From the repository root:

```sh
python3 test/run_tests.py --filter 'ResourceFetchTarget/*'
```

The real-engine movement-method fixture checks every swim class: a valid resource
target remains unchanged, and a depleted target is refreshed after its resource
gradient is rebuilt. It invokes the movement method directly, rather than running
an entire match. The runner isolates the profile and working directory and checks that preferences
remain unchanged. Linux CI runs this regression. Fetching is greedy: the unit
heads for the resource nearest to itself, even when another is a cheaper carry.

`FetchHiringScore/*` checks hiring a fetcher: the hunger check measures the walk
to the resource rather than the whole trip, and the score estimates the walk out
plus the carry home.

`LegacyRoundTripSave/*` loads
[`greedy-fetching/round-trip-143.game.gz`](../../../test/fixtures/greedy-fetching/README.md), a
mid-game save written when fetching still routed by round trip, with round-trip
fields live. The loader discards those fields; the game plays 1,000 more ticks
against a golden per-tick trace and continues identically after a binary or text
save in the current format.

## Hiring bucket iteration

`HiringBucketHarness` uses the real engine to check that two competing inns
receive one worker each before either retries. Hiring the first worker reorders
the live bucket; iteration must keep following building identity. The old loop
fails this fixture with two workers at the first inn and zero at the second. Run with:

```sh
python3 test/run_tests.py --filter 'HiringBucket/*'
```

It runs headlessly in disposable profile directories in Linux and Windows CI.

## Shared compute executor

Build `scons release=1 server=0 unit-tests path-gradient-test
building-gradient-invalidation-test`. The `ComputeExecutor` unit suite checks exclusive
slots, barriers, nested batches, exception propagation, reuse and reconfiguration.
For deferred batches it checks earliest-due ordering (ties and lane order by
submission), that the owner only waits at a join whenever a worker exists, that an
executor with no workers runs the jobs due no later than the join inline, and that
with one worker shared with presentation a join waits out the running chunk and then
completes in due order with no owner jobs. Producers that opt out of sharing compute
inline at submission (`GradientPipeline` owner-only case). It also gates presentation
work while simulation batches and deadline joins finish, and verifies pending
replacement, cancellation, serial pumping and capture release.
The path oracle also exercises independent eager/lazy searches at 1/2/4/8 threads;
the building invalidation harness compares real area/building seed fields and
frozen hiring advancement. Select the executor and path oracle for changes to shared computation, and retain platform-specific results.

`python3 test/check_parallel_compute.py BUILD/GLOB2 --baseline BASELINE` compares
per-tick traces, replay bytes, final saves and uninterrupted save continuation.
Omit `--baseline` to compare the candidate's default execution; `--output DIR`
retains all evidence. This subprocess runner uses Unix `wait4`; native Windows
uses the C++ harnesses. See the existing performance guide for corpus preparation
and paired CPU/wall-time benchmarking.


### Simulation-thread equivalence

`python3 test/check_sim_thread.py CANDIDATE --baseline BASELINE` runs new games
(RNG seeding), a generated map, a version 121 save and save continuation (RNG
restore) with both executables, and requires identical checksum sidecars, final
saves and replay bytes. `--candidate-args` passes extra engine arguments to the
candidate only; `--output DIR` retains the evidence. Each scenario also runs the
baseline twice: replay bytes that differ between those two runs (known
run-varying header fields) are reported and excluded. Run it with
`GLOB2_SYNC_RAND_STRICT=1` to abort on any synchronized draw that is not bound to
the simulated game's stream. Unix only (`wait4`), like `check_parallel_compute.py`.


### Delayed gradient pipeline

The `GradientPipeline` unit suite (`python3 test/run_tests.py --binary unit --filter
'GradientPipeline/*'`) varies completion order across
0/1/2/4/8 workers and publication delays, checks synchronous supersession, bounded
buffers, partial thread-creation failure, exception delivery and teardown. It can
also be compiled directly with ThreadSanitizer without SDL.

`BuildingGradientInvalidationHarness` exercises real resource, guard and clear
fields, including a synchronous update while an older snapshot is pending.
`benchmark_gradient_pipeline.py --verify` compares real-game per-tick traces across
worker counts under the same delayed schedule. `check_gradient_pipeline.py` also
checks the one-worker/eight-tick defaults and save/resume at each of the eight
deadline phases with zero, one and two workers. Pending fields, supersession and
remaining deadlines are versioned save state; worker count is not.

## Experimental features and guard-area balancing

`ExperimentalFeatures` (`glob2-unit-tests`) covers the experiments registry and
the set a game carries: stable keys, the preferences text form, binary and text
stream round trips, unknown keys dropped, and the `GameHeader` forms with a
version 123 header reading no experiment. `SettingsExperiments` and the
`experiments` case of `CustomGameSetup` (`glob2-engine-tests`) cover the
preferences round trip, the string tables and the baked-in rule: every
registry entry's label and help are listed keys matching the English table, a new
game takes Settings → Experiments, its save keeps that set after the setting is
turned off, a fresh game then carries nothing, and a campaign mission never takes
the set. `test/tournament_cli_integration.py` covers `game run --experiment`. The `Settings` display cases toggle the switch on the
settings page. See [experimental features](../../features/experimental-features.md).

`GuardAreaBalance` (`glob2-engine-tests`, `python3 test/run_tests.py --filter
'GuardAreaBalance/*'`) runs the real engine on a blank 64x64 map with 24 warriors
for the `guard-area-balancing` experiment: spawn, drain, patches, size, three
areas, erase, settled-guard movement, a save/load continuation and the crowding
box sum against brute force, plus a `[benchmark]` timing case. Its first case
runs spawn and drain without the experiment, expects the old outcome, and compares
the default game's per-100-tick checksums with
`test/fixtures/guard-area/off-path-checksums.txt` (`[golden]`). Games start with the experiment through
`glob2test::GameOptions::experiments`; `GameOptions::header` installs the
one-local-player header and seed they need. Design and numbers:
[guard-area balancing](../../features/guard-area-balancing.md).

`FarmAreas` (`glob2-engine-tests`, `python3 test/run_tests.py --filter
'FarmAreas/*'`) covers the `farm-areas` experiment on the real `Map` and engine:
the ripest-tile source, empty gaps, exhausted fields, the seed grain, wood and
algae, another team's area, the original harvest off a farm, clearing targets,
growth ignoring the mask, the brush refusing ground that cannot grow, the order
being rejected and a painted mask being inert without the experiment, workers
keeping every tile of a farmed field alive, and a save/load round trip. Design:
[farm areas](../../features/farm-areas.md).

## Market fetching

`MarketFetch` covers hiring and arrival at stocked markets, preference for a
nearer natural resource, stock exhaustion, a retained worker's next delivery, and binary/text preservation of market
fields and pending gradient publications. Non-market buildings use these fields;
markets themselves fetch from natural resources. The market fields participate
in the existing one-field-per-tick round robin and optional fixed-delay gradient
pipeline. Stock transitions invalidate pending market snapshots and request a
refresh. Format 135 saves these fields and their scheduling flags; older saves
load without them and allocate them on first use. Run
`python3 test/run_tests.py --filter 'MarketFetch/*'`.

`MarketFetch` also checks the three market levels: existing type IDs 49–50
remain stable, higher-level sites and buildings append as IDs 51–54, and their
stock and type IDs survive binary/text game saves. Level 2 accepts wood and
wheat in addition to fruit; level 3 accepts all eight resource types. The experiment’s current gameplay and asset constraints are described in [Markets V2](../../features/markets-v2.md).

`MarketsV2` checks both sides of the `markets-v2` experiment: disabled fetch
entry points and construction gates, per-tick deliveries against a retained legacy-market reference, all level/resource/swim-class combinations, upgrade cancellation and
completion and repair with shared stock, legacy travelling workers, forbidden
routes, and binary/text continuation. Simulation traces compare every checksum
part except the MapHeader part, which includes the deliberately changed file-format
version. The benchmark cases report identical market delivery workloads (including
heavy checksums and save/load) and isolated resource-gradient refresh CPU/time and
field memory. `browser/tests/determinism.spec.js` runs the same Markets V2 cases
and frozen traces in serial and threaded Wasm builds.


See [save and file compatibility tests](persistence.md).
