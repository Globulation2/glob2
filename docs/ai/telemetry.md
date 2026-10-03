# AI telemetry

AI telemetry is collected automatically alongside gameplay measurements and exported
when `GLOB2_TEAM_TIMELINE=1`. Open **AI telemetry** from the in-game menu to inspect
current values, filter fields, and select a controller. Scene extraction includes
only own/mutually allied teams during play, and all teams for spectators and replays.
The dialog reads immutable Scene values. The values describe the AI's own state,
calculations and requests; emitted orders are not evidence that the engine accepted
or completed an action.

## Collection contract

Each `AIImplementation` exposes `telemetrySchema()` and `captureTelemetry()` and
receives an `AITelemetry::Sink`. The sink writes to stable, team-owned controller
storage. Its indexed writes and increments allocate nothing, perform no string
lookup and do not inspect the world. Storage is allocated when a controller binds.
The schema is initialized once, independently of simulation state and RNG.

Current values refresh every 32 ticks; `TeamStats` retains historical samples at
the existing 512-tick measurement boundary. Snapshot hooks
copy existing scalar members, fixed-size aggregate arrays and constant-time
container sizes. Transient results are recorded at the original computation or
return statement. No observations, scores, predicates, pathfinding or entity/map
scans are recomputed for telemetry. Saving and final export refresh the scalar
snapshot without polling an AI.

The field catalog in `src/ai/AITelemetryFields.inc` generates both numeric indices
and field descriptions. Each descriptor contains its name, numeric type, kind,
unit and meaning. Signed/unsigned integers have 64-bit storage; doubles preserve
their bit representation in saves. These values are excluded from gameplay,
AI decisions, order payloads, RNG and simulation checksums.

## Identity, freshness and counters

A series is identified by team, player slot, implementation and controller
generation. Names are display metadata, not identifiers. Two copies of an AI on
the same team remain distinct. Replacement and reassignment close an old series
and begin another; loading reconnects an existing series when its schema matches.
A different schema preserves the historical series and begins new coverage.

Every value has validity and an update tick. `na` means that calculation has not
been observed. `state.*` and module snapshot fields describe cached state read at
the capture boundary; their timestamp is that read, not a claim that the AI
recomputed the state then. Calculation results, input arguments and call counters
carry their original update ticks. In particular, the strategic snapshot's own
`tick` and the decision-family call timestamps expose slower AI cadences.

- `polls`: calls actually made to the implementation, excluding paused/dead-team
  early returns in the wrapper.
- `null_orders`, `emitted_orders`, `orders_N`: returned engine orders. Order type
  numbers are the existing `OrderTypes` enum; `ORDER_NULL` is 51. No-op returns
  count only in `null_orders` and their per-type column.
- `*.calls`: invocations of the named subsystem function, not successful actions.
- `*.input_*`: numeric arguments already supplied to that function.
- `*.result`: its last returned integer, boolean or order type. An empty internal
  shared pointer is -1; an actual NullOrder is 51.
- `*.true`: true returns from a boolean function. Meaning follows that function;
  a true predicate is not automatically a completed action.
- `shared_runtime.*` / `runtime.*` event counters: named queue, application, invalidation,
  placement and posture boundaries. Requests queued, requests applied and orders
  returned remain separate measurements.

Counters are cumulative from `coverage_start`; derive rates from sample differences.
Timestamps are simulation ticks. AI-local timer values retain the AI's own cadence.

## Built-in coverage

| Implementation | Readouts |
| --- | --- |
| Inactive | Common poll/order counters; no invented strategy state. |
| Numbi | Phases and attack timers, critical mass/time, economic and military helper inputs/results, food estimates, placement and upgrades. |
| Castor | Food/workforce/swimming/war state, building aggregates, project inputs, subphase/priority/retries/staffing, final placement scores, subsystem calls/results. |
| Warrush | Delays, population/skill/food/building predicate results, rule inputs, placement, farming and guard/exploration subsystem calls/results. |
| Econo | Fruit/enemy flag state, economic rule calls and shared AI runtime queue/lifecycle measurements. |
| Nicowar | Phase flags, recovery and labor state, construction queues, upgrade/target selection results, attack/defense/explorer/farming/fruit subsystem calls and shared AI runtime lifecycle measurements. |
| Maxima | Cached strategic snapshot/trends, environment, demands, director budget, all policy bids/posture utilities, fixed opponent assessments, campaign/offense state, strategic/recon/farming calls, runtime requests, management/placement outcomes and posture changes. |
| Cortex | Cached controller/wave state, existing observation/fact scalars, nineteen hand-policy candidate scores, gates/masks, hand/ML choice, actual economy action, independent combat scores/action, policy evaluations. ML choice is recorded without rerunning inference. |
| Cabino | Scheduler, queue and installed-module scalar/container-size snapshots; module invocations and existing boolean/integer outcomes, covering defense, attack, construction, upgrades, swarm allocation, exploration, inns, towers, clearing, happiness and farming. |
| JavaScript profile 2 | Named script values and controller status, queued actions, native query work, and placement failures. |

The schema output is the authoritative field-by-field catalog. Per-entity lists,
map surfaces, variable-length candidate sets, debug strings and expensive new
aggregations are deliberately excluded from the indexed numeric schema. JavaScript
controllers may additionally publish bounded numbers, booleans and short text with
`ctx.telemetry.set`; see the [API reference](../development/javascript-api.md#telemetry).
Existing independent AI debug options
remain independent; this mechanism does not enable them or parse their output.

## Output and persistence

New line types coexist with unchanged gameplay timeline records:

```text
GLOB2_AI_PLAYER team=0 player=8 ai=1 generation=0 schema=1 coverage_start=0 name="Numbi"
GLOB2_AI_SCHEMA ... field=polls type=1 kind=1 unit="calls" meaning="Actual AI implementation polls"
GLOB2_AI_SAMPLE ... tick=512 available=1 active=1 polls=512 polls@tick=511 ...
GLOB2_AI_HISTORY ... tick=512 available=1 active=1 ...
GLOB2_AI_FINAL ... tick=777 available=1 active=1 ...
GLOB2_AI_VALUE ... tick=777 field="strategy.phase" value="expansion" updated=768 unit="" meaning=""
```

Schema descriptions are emitted once per matching implementation/schema in a game;
player metadata is emitted per series. Field types are signed=0, unsigned=1,
real=2; kinds are gauge=0, counter=1, enum=2, mask=3. Strings use quoted, escaped
text. Each valid field includes `field@tick`. Values remain exact decimal integers;
consumers must use 64-bit/big-integer parsing rather than converting everything to
double. Non-finite floating-point values export as `na`.
Named presentation values use separate `GLOB2_AI_VALUE` records, with a quoted
value and its original update tick. They are emitted for current/final snapshots.

The final summary exports retained history and current readouts, including games
ending between samples. History repeats previously emitted samples: distinguish
record types rather than summing them. Closed series remain available in final
history. Formatting and schema comparisons for output occur only when enabled.

Save format 106 stores descriptors, controller identities, current values,
validity/timestamps, cumulative counters, coverage and sampled history. The minimum
save version remains 58. Older saves begin new AI coverage at the loaded game tick.
The loader bounds record/field/string counts, validates types, identities and
sample cadence/order, and rejects truncated binary fields. A field's saved meaning
survives schema evolution; changed schemas begin a new series rather than
reinterpreting historical columns.

Format 129 adds bounded named values to saves and a versioned diagnostic stream to
replays. Replays record sampled changes independently of gameplay orders, up to
16 MiB; values become unavailable after a truncated stream's coverage ends.
Playback displays recorded values without executing AI decisions. Older accepted
replays show telemetry as unavailable. Replay presentation schemas also survive a
headless final-state save. This telemetry is local diagnostic state, not network
traffic, and is excluded from gameplay checksums.

## Offense target team

To study how much a free-for-all or 2v2 result depends on who was attacked first,
each AI that attacks exposes the enemy team its offense is aimed at. These fields
are diagnostics like every other field here: no AI decision reads them, and
neither does the [win probability model](../win-probability-model.md).

| AI | Field | Meaning |
| --- | --- | --- |
| Maxima | `state.budget.tactical_target_team`, `state.campaign.target_team` | Set only by offense planning; cleared during defensive emergencies |
| Nicowar | `state.target` | Offense target; defense flag placement never changes it |
| Castor | `state.strikeTeam` | Valid while `state.strikeTeamSelected` is 1 |
| Cabino | `module.PrioritizedBuildingAttack.target_team` | The attack module's current enemy; defense modules never pick an enemy team |
| Cortex | `offense.target_team` | Team owning the flag target the offense last committed to; `-1` once the offense is stood down for any reason |
| Numbi | `AINumbi.mayAttack.enemy_team` | The highest-numbered enemy team, which `mayAttack` always searches: not a ranked choice |
| Warrush | `AIWarrush.placeGuardAreas.last_team` | Team of the last enemy building given a guard area; one call can guard several teams, so this is not a single target |

Econo builds no warriors and has no offense field. `-1` means no current target;
`na` means the AI has not reached the code that sets the field. The Cortex value
is written when the decision is made, so it is saved with the series and survives a
reload. Cabino, Cortex, Numbi and Warrush use schema version 2 for these fields;
series from older saves close and new ones begin, as described above.

## Adding fields or an AI

For an existing AI, append an `AI_FIELD` declaration to its section in the catalog.
Use the generated `AITrace::AI<N>::field` constant with `telemetry.set`,
`setUnsigned`, `setReal` or `count`. Copy a transient calculation where it already
exists; use `captureTelemetry()` only for initialized scalar state and bounded
arrays. Keep unavailable results invalid until the corresponding calculation runs.

A new implementation inherits the common schema by default. Override
`telemetrySchema()` to return a process-lifetime descriptor vector beginning with
`AITelemetry::schema(0)`, followed by its own fields. Its numeric indices begin at
`AITelemetry::Specific`. Override the snapshot hook as needed. This requires no
TeamStats, serializer, exporter or order-format changes. Econo and Nicowar delegate
through the shared AI runtime wrapper; Cabino modules can override the module snapshot hook.

Override `telemetrySchemaVersion()` to advance an implementation schema version.
Treat field names/types/meanings as a versioned contract. Preserve historical
columns, update schema descriptions when adding fields, and test save/load and
unchanged order/checksum execution. Do not use a telemetry field as AI input.

The existing `team-stats-save-test` target includes AI schema, numeric persistence,
identity, generation, replay-availability and corruption regressions.

### Existing AI load behavior

Diagnostic values and histories are restored exactly. Subsequent observations describe the
AI that actually executes after loading. Some existing controllers do not save their full
internal state: Numbi resets its decision timer and Castor reconstructs projects/caches.
Their post-load decision-call counts can therefore differ from an uninterrupted run. This
change deliberately does not repair those gameplay-affecting behaviors. Validation compares
exact diagnostic restoration and repeatability of two continuations of the same save, while
retaining the existing simulation continuation checks.

Tournament workers retain these records in verified logs; their normal offline
analysis exports typed JSONL and CSV. See [distributed telemetry](../tools/tournaments.md#gameplay-ai-and-performance-telemetry).

## Maxima placement fields

Structured offline runs can capture Maxima's per-tile placement inputs:

```sh
"$GLOB2_BIN" --run-game --load-game initial.game.gz --ticks 3000 \
  --output-dir artifacts/placement --diagnostic-fields maxima \
  --diagnostic-interval 1000 --diagnostic-png true
```

`--diagnostic-fields maxima` enables capture; the interval defaults to 2500
simulation ticks, and PNGs default off. The first fresh placement world is
captured, followed by the first fresh world after each interval. Incremental
selection reuses older worlds and does not cause duplicate captures. Diagnostics
never force a planner refresh or change its policy.

Each completed `diagnostics/tick-NNNNNNN.playerP.teamT/` directory contains
`threat`, `protectedness`, `foodOpportunity`, `farmCapacity` and `protectedYield`
`.field` files, optional matching PNGs, and `capture.json`. Metadata records the
simulation tick, AI-local planner tick, dimensions, controller, team, units and
colours. AI-local ticks are not simulation ticks. PNGs use one owned Scene
extracted after AI workers join, before the next orders execute.

Capture buffers and their Scene have a 128 MiB budget. Oversized captures are
skipped and reported; the interval also limits retries after failures. The
threaded offline runner waits at a tick boundary while the graphics owner drains
one batch. This can slow the run but does not advance simulation or change orders.
Writes occur outside AI updates. A capture directory is published only after all
requested outputs close successfully; failures are reported without stopping the
game. `summary.json` counts completed, failed and skipped captures. Files from an
interrupted write are never marked complete. Capture state is session-local and
is neither saved nor included in simulation checksums.

These options apply to `--run-game`, including loaded games, and require a Maxima
controller. Interactive captures and environment-variable aliases are not
provided. See [whole-map rendering](../map-generators/CLI.md#render-a-whole-game).
