# Ai verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Maxima

See [Maxima tests](../../../test/maxima/README.md) for policy, configuration, integration and
saved-game continuation coverage.

## Cortex placement regression

The `CortexGeometry` suite (`python3 test/run_tests.py --filter 'CortexGeometry/*'`) compares placement geometry against the tile-scan helpers and checks the
building-proximity mask against per-building edge distances. Coverage includes
wrapped corners, upgrade reservations, construction sites, map-only occupants,
dead buildings, empty colonies, and footprints or distance limits spanning the map.

## AI helper gradient regression

`Map::updateGlobalGradient(Uint8*)` supplies the Castor/Warrush helper maps.
Run its independent byte-for-byte oracle from the repository root:

```sh
python3 test/run_tests.py --binary unit --filter 'GlobalGradient/*'
```

The harness covers 3,000 random fields, mixed seed strengths, inert inputs,
toroidal seams, thin dimensions, obstacles, distance cutoff and idempotence.
It runs in the Linux CI jobs; the weighted pathfinder has separate `Gradient`
coverage in `glob2-unit-tests`.

## AI save portability

`AISavePortabilityHarness` checks the shared AI runtime's serialized fields before its first tick,
round-trips asymmetric AddArea/RemoveArea coordinates through the binary stream,
and executes the restored orders on their intended tiles. It also loads legacy
Maxima telemetry with values in the nonexistent seventh-policy columns and checks
that capture marks those columns unavailable while still sampling the six real
policies. The legacy columns and historical samples remain readable.

```sh
python3 test/run_tests.py --filter 'AISavePortability/*'
```

The Linux CI regression job runs this harness. For portability changes, also run
it with Clang and GCC and compare full-game per-tick traces using the same saved
input. Correct coordinate loading preserves the x/y order written in existing
saves; a save containing pending area orders can resume differently from older
GCC builds that transposed those coordinates. The saved layout is unchanged.

## Shared AI runtime/Nicowar save continuation

```sh
python3 test/run_tests.py --filter 'RuntimeContinuation/*'
```

The harness preserves stale fields, uncomputed gradients, ages and duplicate
refresh-queue entries through binary and text manager round trips. It also checks
that shared-runtime controllers own independent managers and that a legacy shared
manager can be copied without retaining mutable gradient or entity references.
Linux and Windows CI run it.
`check_parallel_compute.py` also resumes the version 121 four-controller fixture in
`test/fixtures/echo/` at 1, 2, 4 and 8 workers and compares checksums and saves.
That fixture was saved at tick 256 from `maps/FourSquares1.map.gz`, game seed
123, with Econo, Nicowar, Econo and Nicowar in player order.
The same check starts a fresh four-controller game to cover concurrent cache creation.
It also starts four Castor controllers to exercise concurrent lazy map-gradient
requests.

`python3 test/check_shared_runtime_save_continuation.py PATH/TO/glob2` also checks full
Maxima/Nicowar and Nicowar/Nicowar games through two reloads using the retained
[arena fixture](../../../test/fixtures/shared-runtime-continuation/README.md).

Save format 119 stores these fields plus the previous construction id, fruit
observation and local initialization timer. Recomputing the cache on load can
otherwise change the tick a pending building becomes ready. Earlier saves remain
readable through their historical reconstruction path; missing historical cache
state cannot be recovered from them. A subsequent format-119 save preserves the
reconstructed state. Network protocol 42 gates transfers containing the new fields;
the replay floor for that version remained unchanged. Version 122 gives each
controller its own manager, loads and copies legacy shared-manager state, and
raises the replay floor to 123 and network protocol to 46.

## Shared AI runtime building-order id save compatibility

`RuntimeBuildingOrderSaveLoadTest` covers `AISharedRuntime::Construction::BuildingOrder::id`,
the `BuildingRegister` key handed out at runtime by `Runtime::add_building_order`.
`save()` and `load()` never moved the field and the member had no initialiser, so
every pending building order restored from a save carried an uninitialised heap
value into `BuildingRegister::issue_order` and `AssignWorkers`: an AI game resumed
from a save was not reproducible run to run, and a resumed multiplayer game could
desync without packet loss or a version mismatch. Version 96 serialises the field.
Older saves do not carry it and load leaves the member at `-1`, the sentinel
`Runtime::load` replaces with a fresh `register_building()` key.

The fixture checks the version-96 round trip, that an unregistered order's `-1`
survives the `Uint32` on the wire rather than returning as a huge positive key,
and that a pre-96 stream leaves the sentinel with every following field still
decoding from the right offset. The `RuntimeBuildingOrderSaveLoad` suite runs in
`glob2-engine-tests`, linking the real catalog, placement and runtime components.

## Tournament execution and configuration

`python3 test/test_duel_ratings.py` checks the standard-library batch duel fit:
known odds, ties, input order, finite-fit boundaries, and paired bootstrap integrity.

`python3 test/test_tournaments.py` exercises leases, duplicates, resumable transfers,
worker queues, immutable builds and offline statistical policies using stdlib fixtures.
`python3 test/test_map_fairness_tournament.py` retains the fairness estimator and repeat-selection regressions.
`python3 test/test_map_generation_study.py` checks structured map-study result classification,
timeouts, temporary-profile cleanup, catalog lookup and per-subject telemetry preservation.
It also covers a persistent daemon hot-reloading `host.json` after a `configure` RPC
(no restart required); `audit`/`reap` cross-host worker discovery and staleness
flagging (dead daemon, or alive but idle past `--stale-hours`); and `ai_comparison`'s
`sample_games` mode (a bounded random sample -- each game independently drawing its
own format/matchup/generator/size via inline generation -- as an alternative to the
exhaustive cross product, reusing the same Planner and analysis pipeline).
`python3 test/test_fairness_model.py` checks the fitted [fairness model](../../map-generators/fairness-model.md):
that the fit recovers coefficients from a tournament simulated out of the model itself, that a
measurement deciding nothing is fitted near zero, that the fairness definition reads the same at
every colony count and ignores the offset softmax leaves unidentified, and that every measurement
the model may select has a C++ expression waiting for it. The fitting checks need numpy and scipy
and skip without them; the rest is stdlib.
The `TournamentCompatibility` engine suite (`python3 test/run_tests.py --filter
'TournamentCompatibility/*'`) covers real per-player Cortex/Maxima and partial
game-header round trips. `python3 test/tournament_cli_integration.py --output DIR`
runs production CLI cases and retains saves, traces and logs. Use a fresh output
directory. `--initial FILE --ticks N` runs a retained initial state on another platform.

`test/tournament_reliability_pilot.py` is an opt-in localhost/SSH integration pilot.
It requires immutable macOS/Linux bundles and explicitly configured disposable
worker directories, and kills only processes belonging to that pilot. See
[the tournament guide](../../tools/tournaments.md) for commands and validation policy.
## Nicowar farming wood clearance

`NicowarFarmingHarness` executes the farming scan and its real area orders. It
checks the wood/wheat zone boundaries, eight-way wheat adjacency (including inside
the wood zone), both wrap seams and diagonal corner wrapping,
removal of conflicting farming protection, cleanup after wood and neighboring
wheat disappear, and preservation of building clearing strips.

```sh
python3 test/run_tests.py --filter 'NicowarFarming/*'
```



## Castor saved-game continuation

`CastorContinuationTest.cpp` compares emitted order bytes, per-tick simulation
checksums and RNG state across saves during boot, map computation and active
colony management. It also checks binary/text snapshot round trips and the
historical timer-only Castor AI formats (versions 1 and 2).

```sh
python3 test/run_tests.py --filter 'CastorContinuation/*'
```

Save format 126 writes Castor AI format 3, preserving project order, boot progress,
strategy, control timers and map-cache history. Older saves remain readable with
their historical restart behavior; omitted state cannot be recovered from them.
New games retain the existing decision sequence. This save change does not raise
the replay acceptance floor.

## Legacy AI state and full-game continuation

`LegacyAIStateTest.cpp` checks binary and text records for Warrush and Numbi clocks,
Nicowar explorer phases and full-width construction counters, Cabino queued orders
and specialist/cache state, Cortex policy selection and weights, and shared-runtime
replacement of pending work on direct reload. Timer cases include boundaries,
malformed values, truncation, constructors and wrapper loading. Genuine historical
Warrush and Numbi layouts, followed by a sentinel, check versions 58, 121 and 127.
The pre-fix writer records in `fixtures/legacy-ai-127/` additionally check Nicowar,
Cabino and Cortex alignment and defaults. None relabel current-format records.

`AIStateContinuationTest.cpp` compares order types and payloads, every controller's
RNG, the simulation RNG, and detailed simulation checksum components for 256 ticks
after each checkpoint. Checkpoints span boot, construction cooldowns, maintenance
and phase transitions; colonies must stay alive and emit substantive decisions.
It exercises every native AI, mixed opponents, shared-team controllers and repeated
reloads at 64 and 128 ticks. Saves and per-tick TSV traces are retained under
`artifacts/tests/AIStateContinuation/`. JavaScript's saved state, RNG and disabled
state are covered by `JavaScriptIntegration`, alongside the Maxima archive fixtures.

```sh
python3 test/run_tests.py --filter 'LegacyAIState/*' --filter 'AIStateContinuation/*' \
  --filter 'CastorContinuation/*' --filter 'JavaScriptIntegration/*' \
  --filter 'Maxima.Continuation/*' --filter 'RuntimeContinuation/*'
```

Save format 132 gates the added fields by AI. The save compatibility floor stays
58; omitted values in older saves retain their historical defaults. Exact old
continuation cannot be recovered, and Cabino's historically damaged nonempty queue
records cannot be repaired from missing bytes. Fresh-game strategy is unchanged,
so the replay floor stays 127. Protocol 54 carries the new saved-game data;
compatibility tests cover 127, 128, 132 and rejection of future format 133.

For platform verification, run the same seeds and fixtures on macOS and Linux and
compare the emitted TSV traces. A successful local run establishes local resumed
equivalence; cross-platform equivalence requires both platform traces.

## Custom-rule AI behavior

`AIRules` exercises every native controller with disabled training, hunger and
combat, checks emitted orders, and compares save/load continuation. It also
covers authoritative upgrade rejection versus repairs, preserved starting levels,
restored training subscriptions, JavaScript rule observations in both profiles,
Cortex scoring gates, rule parsing, and harvesting the last finite farm seed.
Run `python3 test/run_tests.py --filter 'AIRules/*'` with the appropriate build directory.
Tournament job adapter tests cover repeatable rule arguments and saved-game override
rejection in `test/test_tournaments.py`.

For retained tournament qualification, set `GLOB2_TEST_AI_RULE_AUDIT=1`. This
opt-in assertion checks each AI order when selected and reports unavailable work
without filtering it. A repair can finish while its order waits in the network
queue, so replay-time building health alone cannot classify upgrade intentions.

## AI strategy profile captures

The `CustomGameSetup` suite (`src/game/screens/CustomGameSetupHarness.cpp`) has one case per
mode of the old command line:

```sh
python3 test/run_tests.py --filter 'CustomGameSetup/*'
python3 test/run_tests.py --filter 'CustomGameSetup/generated map snapshot*'
python3 test/run_tests.py --filter 'CustomGameSetup/AI strategy profiles*'
```

The snapshot case checks that a freshly serialized map launches through the lobby's in-memory load path. The strategy profile cases capture the Players & Teams strategy button and every AI profile at its top and bottom, at 640×480 and 1000×700. They check that profile/summary keys resolve, including Maxima. The player control widgets case exercises opening the strategy screen from the lobby and choosing an AI before launching each controller mode. Every case uses its own disposable `glob2-custom-setup-tests` profile and leaves captures in its artifact directory.

The strategy profile cases run in English; to capture another catalog with its
localized section headings, pass that language code to `setupOptions` in a local
copy of the case. Text wrapping for unspaced CJK text and long words is covered by
the `UILayout` unit suite.
