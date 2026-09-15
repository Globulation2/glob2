# Gameplay measurements

`TeamStats::measurements` is a diagnostic block alongside the existing live,
smoothed and end-game statistics. It does not participate in AI decisions, RNG,
orders or simulation checksums. Starting, loaded, scripted and editor-created
entities are not production events. Existing statistics and AI accessors retain
their definitions and sampling cadence.

## Definitions

All event counters are unsigned 64-bit totals. Collection is always active in
normal, headless and replay simulation. Units and buildings are attributed to
their current team at the event; meals and healing belong to the serving
building's team, training to the visiting unit's team.

| Field | Meaning and units |
| --- | --- |
| `births[type]` | Successful swarm production, by worker/explorer/warrior. Failed exits and exhausted unit slots do not count. |
| `deaths[type][cause]` | One event at the death transition. Causes: combat, starvation, clearing injury, trapped on building destruction, unknown. The first transition below the engine's lethal HP threshold retains its cause until processing, including across saves. |
| `conversionsIn/Out[type]` | Successful ownership changes, separate from births/deaths. The legacy conversion counters are unchanged. |
| `harvested[resource]` | Completed harvested **loads**, including renewable resources. A carried load is one item; building multipliers apply only on delivery. |
| `cleared[resource]` | Successful clearing **operations** that reduce/remove a resource, separate from harvest. |
| `delivered[resource]` | Accepted **resource units**, after the destination multiplier and capacity clamp. |
| `withdrawn[resource]` | Actual resource units removed by market withdrawals, after the zero clamp. |
| `transferredIn/Out[resource]` | Resource units deposited into/withdrawn from markets. These overlap market deliveries/withdrawals; they are not additional harvest or consumption. Market storage is shared across a team's markets; the exchange masks do not currently implement a separate inter-team transfer event. |
| `consumed[purpose][resource]` | Resource units used by meals (including fruit), successful spawning, stone-to-ammunition conversion, and completed new construction/upgrades. Construction consumption is recorded at completion, not at each delivery. Canceled sites are not completions. |
| `repairDelivered[resource]` | Accepted deliveries during repairs, separately recorded. Repair-site prefill represents existing structure and is not consumption. |
| `meals`, `healingVisits`, `hpRestored` | Meals served (including a charged partial meal), completed healing visits, actual positive HP restored. Partial treatment on expulsion adds HP but not a completed healing visit. |
| `damageDealt/Received[source][target]` | Actual positive HP removed after armor/minimum damage, capped to the target's remaining positive HP. No overkill. Sources: melee, explorer magic, tower; targets: unit/building. |
| `shots[source]`, `impacts[source][target]` | Melee strikes against targets, successful magic activations (one activation can affect several targets), tower projectiles launched; positive-damage target impacts. A miss has no damaging impact. |
| `completed[kind][building][level]` | Completed new construction, upgrades and repairs, by type and destination level. Repeated completion checks do not add events. |
| `removed[cause][building][longLevel]` | Combat destruction, demolition/cancellation, other removals. Virtual flags are excluded. |
| `trainingVisits[type]`, `abilityGains[type][ability]` | Completed training visits and positive levels gained. A parallel visit counts once; every raised ability counts its own level difference, including both worker abilities. |
| `stock[resource]`, `carried[resource]` | Current building/shared stocks and carried items. Shared team stocks count once, even with several markets. |
| `buildings[type][longLevel]` | Current finished buildings and sites, excluding flags and dead buildings. |
| `hungry`, `critical`, `feeding`, `healing` | Current hungry units using `isUnitHungry()`, hungry units below maximum HP, and units currently inside for feeding/healing. These do not replace AI hunger metrics. |
| `trappedUnits[blockage][type]`, `trappedBuildings[blockage][swim][type]`, `trappedTick` | Sampled counts of entities with no legal neighboring move or usable building exit. Blockage 0 ignores unit occupancy; blockage 1 includes it. Buildings are evaluated for non-swimmers and swimmers separately. The counts overlap. |
| `lowHP[band][type]`, `lowFood[band][type]` | Sampled units at or below 25%, 50%, or 75% of maximum HP or food reserve. Bands overlap. |
| `growthGlobal[kind][resource]` | Cumulative natural new tiles, positive amount changes, and natural amount reductions. Only changes from gameplay `growResources` are counted. |
| `growthTiles/Amount/Reduction[range][resource]` | The same natural growth within wrapped Chebyshev distance 8, 16, or 32 of the team's physical buildings and sites. Ranges and teams overlap. |

These are useful event counts and snapshots, **not a complete conservation
ledger**: loads, operations and resource units are distinct; transfers overlap
movement through storage; abandoned stock, carrying losses and repair prefill
are not production-chain accounting.
The map-wide growth totals are repeated in each team's record for a simple shared
schema. Do not sum `growthGlobal` across teams. Near-range growth may count for
several teams and its three distance bands are cumulative.

Array indices follow engine enums: unit types in `UnitConsts.h`, resource types
in `Ressource.h`, building types in `IntBuildingType.h`, and diagnostic axes in
`GameplayMeasurements` (`TeamStat.h`). Levels are zero based. Building long
levels are `2 * level + 1 - isBuildingSite`, separating sites and finished
buildings into six bins.

## Sampling and saves

Ordinary snapshots share existing unit/building scans. Blockage and health bands
use a separate bounded entity scan only on ticks divisible by 512, save refresh,
and final export.
Growth events read one packed tile word with all three team distance masks.
Per-team overlap counts update only the changed building footprints at a sampled
boundary. There are no new pathfinding calls, per-event allocations or
per-event log records. History allocation occurs at sample time. At most one
sample is retained per 512-tick slot in the engine's
32-bit tick range (8,388,608 slots); loading checks count, cadence, ordering and
coverage before accepting samples, and binary fields use checked reads.

Save format **108** stores totals, current snapshots, sampled history, coverage
start and pending lethal/projectile attribution. The minimum readable save
version remains **58**. Earlier saves retain their legacy statistics and begin
new measurement coverage at their loaded game tick. Earlier measurement history
is unavailable, not a sequence of zero samples. Loading itself creates no
production/death events. Old projectiles have unknown source-team attribution;
target damage is still counted, but source-team damage is not guessed.

The expanded fields have a separate `extended_coverage_start`. Older saved samples
remain unavailable for these fields, including a sample at the old save's load tick.
Save format 108 stores the building footprints
used for the current 512-tick growth interval, so a mid-interval load reconstructs
the same proximity masks. Blockage and health bands are checked at 512-tick samples
and final export; building coverage changes at the next sample. The masks are
derived, tile-indexed bit arrays rather than saved map fields.

The replay acceptance floor remains **99** and network/YOG protocol gates remain
**33**. New-format saves require a reader that understands format 108; these
fields do not change simulation execution or order formats.

## Existing timeline output

Set `GLOB2_TEAM_TIMELINE=1` to include measurements with the existing output.
Legacy `GLOB2_ECON`, `GLOB2_TL` and `GLOB2_FINAL` formats are unchanged.

```
GLOB2_MEASURE team=0 tick=512 coverage_start=0 final=0 births_0=... deaths_0_0=... stock_1=... ...
GLOB2_MEASURE_HISTORY team=0 tick=512 coverage_start=0 final=0 births_0=... ...
GLOB2_MEASURE team=0 tick=777 coverage_start=0 final=1 births_0=... ...
```

Records contain all named fields listed above, flattened with underscore-separated
zero-based indices. `GLOB2_MEASURE` is emitted at each sample. The automatic
ending summary exports retained history as `GLOB2_MEASURE_HISTORY` (including
history from loaded saves), then an exact final snapshot even between sample
ticks. Distinguish record types rather than summing them: history repeats samples
already emitted during the run. Formatting/output occurs only when enabled.
`extended_coverage_start` marks when the new growth, blockage and threshold fields
first became available. The current `trappedTick` marks the last blockage check.

## Player views

Click the page selector at the top of the existing text-statistics panel to cycle
legacy statistics, gameplay measurements, and the expanded diagnostic page.
The pages use the selected team in replay/spectator views.
Wheat harvest and meal-consumption rates use the difference between the last two
samples, in loads/minute and wheat resource units/minute at 25 ticks/second.
They show unavailable until two samples exist. Large live-panel counts use
scientific notation; saved totals and exports remain exact integers.
The expanded page shows blocked counts as structural/current (`S/C`). The health
lines show the cumulative 25/50/75% bands in that order; nearby growth shows
positive amount changes within the three distance cutoffs.

Post-match graphs keep the six original metrics on page one. **P** cycles pages;
**1–6** selects a metric on the current page. Team toggles stay associated with
the same team across sorting/page changes. New graphs use timestamped samples,
cumulative counters or stock values, and leave missing early history blank.
Hover a recorded point for its exact 64-bit value. New labels use translation
keys; untranslated labels fall back to English.

## Verification

The existing `team-stats-save-test` and `savegame-safety-test` targets cover the
new fields; both already run in Linux and Windows CI. Run locally with disposable
profiles as described in `test/README.md`. The statistics harness also accepts
`--screenshots OUTPUT_DIRECTORY` to render graph pages and a live-panel fixture
at 640×480 and 1024×768 with the supported maximum of 12 teams, large totals and partial legacy history.
`python3 test/check_telemetry_simulation.py build/src/glob2` compares the complete
1,024-tick four-AI and 2,048-tick 12-team checksum sidecars against compressed
pre-change fixtures. It also reloads a format-108 mid-run checkpoint and compares
256 team/entity checksum records against the parent build's reload. Aggregate
checksums after save/load include `MapHeader::versionMinor`, so a format-108 save
cannot have the same aggregate checksum as a format-107 save. CI runs the
same check on Linux and Windows (with `.exe` on Windows).
Actual results and platform/performance coverage are recorded with the change's
validation artifacts; CI compilation alone is not deterministic execution proof.
