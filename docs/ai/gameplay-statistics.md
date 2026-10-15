# Gameplay measurements

## On this page

- [Definitions](#definitions)
- [Sampling and saves](#sampling-and-saves)
- [Existing timeline output](#existing-timeline-output)
- [What the player sees](#what-the-player-sees)
- [Verification](#verification)

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
| `labour[activity]`, `filling[job][phase]` | Worker time use in worker-ticks: every live worker adds one count per simulated tick, to exactly one bucket. Activities: idle; eating (walking to an inn, inside, no inn available); healing (walking, inside, no hospital); training (walking, inside); flag work; other. A worker filling a building counts in `filling` instead, by job (swarm, inn, construction site, other building) and phase (walking to a resource, harvesting, carrying back, other). Interval use is the difference between two samples. |
| `harvestDistance[job]`, `harvestSamples[job]` | Sum and count of the wrapped Chebyshev distance from a harvesting worker to the centre of the building it fills, once per harvesting worker-tick. Divide for the mean haul distance. |
| `eatWalkDistance`, `eatWalkSamples` | The same for hungry workers walking to their inn. |
| `combatDeathPlace[type][place]`, `combatDeathAssignment[type][assignment]` | Combat deaths (the `deaths` combat cause) by where the unit died and what it was attached to: none, war flag, clearing flag, exploration flag, other building. |
| `warriors[place]`, `warriorLevels[place]`, `warriorsHurt`, `warriorsFlagged`, `warriorsInside`, `defenceTick` | Defence snapshot at `defenceTick`: live warriors by place, the sum of their attack speed and strength levels, warriors seeking healing, attached to a war flag, and inside a building. |
| `intruders`, `intruderLevels` | In the same snapshot, warriors of enemy teams whose place, seen from this team, is home, and their summed levels. |

These are useful event counts and snapshots, **not a complete conservation
ledger**: loads, operations and resource units are distinct; transfers overlap
movement through storage; abandoned stock, carrying losses and repair prefill
are not production-chain accounting.
The map-wide growth totals are repeated in each team's record for a simple shared
schema. Do not sum `growthGlobal` across teams. Near-range growth may count for
several teams and its three distance bands are cumulative.

A **place** is home when one of the team's own buildings or construction sites is
within 16 tiles (wrapped Chebyshev distance to the footprint), else away when an
enemy's is, else field; contested ground counts as home. Flags are not buildings
here, and allies' buildings count as neither. Places read the same per-tile team
masks as the 16-tile growth range, so they use the buildings of each team's last
512-tick sample and cost one tile read.

Array indices follow engine enums: unit types in `UnitConsts.h`, resource types
in `Ressource.h`, building types in `IntBuildingType.h`, and diagnostic axes in
`GameplayMeasurements` (`TeamStat.h`). Levels are zero based. Building long
levels are `2 * level + 1 - isBuildingSite`, separating sites and finished
buildings into six bins.

## Sampling and saves

Statistics read the shared between-tick world snapshot. One deferred batch per
advancing tick computes all teams in team order on one dedicated executor lane.
The simulation owner publishes each sample at `sourceTick + teamStatsDelay` in
source-tick/team order, joining late work at that deadline. Worker completion
order never selects visible statistics. Zero workers use the same calculation
at the deadline; configured delay zero uses the next observation boundary for new samples.
AI, scripts, victory calculations and presentation all read published values.

The match-wide `teamStatsDelay` rule accepts 0 through 8 ticks, defaulting to 8.
Per-tick samples retain the 32-sample smoothing/aggregate cadence and 512-tick
history cadence. Histories carry source ticks, not publication ticks. The standard simulation
snapshot contains map inputs; statistics jobs retain those buffers only for the
512-tick diagnostics. Growth and combat
attribution continue to use the last published building coverage.

Aggregate smoothing uses the current raw sample plus the latest 31 published raw
samples captured at submission. K=0/1 retain exact consecutive windows; K>1 skips
K-1 recent pending rows and uses older rows instead. Cadence stays 32 samples,
and histories stay at source ticks every 512. Raw ring entries remain consecutive,
so this approximation does not compound. Delayed maxima can change AI decisions.

Live event hooks write an owner-only collector. Snapshots capture its cumulative
counters, while workers compute instantaneous measurements and labour deltas.
The statistics batch job then accumulates those deltas and retains the latest
periodic diagnostics in a complete frame. Publication installs that frame by
swapping ownership; readers receive a const measurement accessor. Labour totals
remain exact despite the approximate smoothing window.
The live collector keeps events from later ticks independently of publication.
Snapshot input buffers are pooled, and publication takes ownership of completed
measurement frames. Replaced frames reuse existing batch storage only when no
reader retains them. Routine checksums cover queue identities and published
simulation-driving statistics/smoothing; full checks
join and hash complete pending results, published diagnostics and live counters.
Consequently routine checks detect differing pending simulation statistics when
they publish; diagnostic measurements are covered by full verification.
AI telemetry collection remains on the owner. Ordinary pauses do not admit new
samples, and repeated reads of a boundary do not duplicate them.

Save format **155** introduced collected and published measurements, smoothing
state, publication timestamps and completed pending samples with their original
deadlines. Format **156** removes the redundant snapshot input digest; format-155
loads read and discard it. Format **157** distinguishes worker-finalized aggregates
from older raw aggregates. Raw pending aggregates from formats 155/156 retain their
original exact publication reduction, including after resaving. Format **158**
persists complete measurement frames; loading formats 155–157 completes their
measurement deltas in source order without publishing early. Saved aggregate
modes and deadlines are preserved. Saving joins private computation without
publishing future samples.
Older saves migrate to the shared snapshot boundary with delay zero, preserving
loaded published/smoothing state; their continuation can change. The minimum
readable save version stays 58. Match termination does not finish reporting:
results use published samples and pending samples are discarded during teardown.

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
the same proximity masks. Blockage and health bands are checked at published 512-tick samples; building coverage changes at the next sample. The masks are
derived, tile-indexed bit arrays rather than saved map fields.

Save format **133** adds worker time use, combat-death places and assignments and
the defence snapshot to the totals, current snapshot and every history sample, with
a separate `labour_coverage_start`. Earlier saves load with these fields at zero,
their history samples unavailable for them, and coverage starting at the loaded
tick. Their defence snapshot is unavailable (`defenceTick` before
`labour_coverage_start`) until the next sample.

Save format **137** records per-variant building counters alongside the embedded
catalog. Earlier saves retain their sampled history through the frozen stock
catalog mapping. Old blockage counts lacked a level: import preserves each family
aggregate once, for the total blockage metric. New metrics and their labels use
the game's concrete catalog, including in saved histories.

The current replay floor is **158** and network protocol **70**. The save loader
still supports the durable format-58 floor.

## Existing timeline output

Set `GLOB2_TEAM_TIMELINE=1` to include measurements with the existing output.
`GLOB2_TL` and `GLOB2_FINAL` keep their existing fields. `GLOB2_ECON` emits
`variant_<id>` counts for every concrete catalog variant. Its historical named
columns remain model/log aliases for capabilities: `swarm` is production, `inn`
is feeding, `hospital` is healing, `school` is worker construction training,
`barracks` is warrior combat training, `racetrack`/`pool` are walking/swimming
training, and `tower` is projectile fire. Sites use their explicit completed
target. A hybrid may count in several columns; do not sum these service columns
as a physical building count. The `barracks` feature matches the runtime victory
model's unique warrior-training provider count. The defense-power scalar uses
damage against warriors; the engine resolves actual damage by target class.

```
GLOB2_MEASURE team=0 tick=512 coverage_start=0 final=0 births_0=... deaths_0_0=... stock_1=... ...
GLOB2_MEASURE_HISTORY team=0 tick=512 coverage_start=0 final=0 births_0=... ...
GLOB2_MEASURE team=0 tick=777 coverage_start=0 final=1 births_0=... ...
```

Records contain all named fields listed above, flattened with underscore-separated
zero-based indices. `GLOB2_MEASURE` is emitted at each sample. The automatic
ending summary exports retained history as `GLOB2_MEASURE_HISTORY` (including
history from loaded saves), then the last published measurements. Pending
samples are not published early at termination. Distinguish record types rather than summing them: history repeats samples
already emitted during the run. Formatting/output occurs only when enabled.
`extended_coverage_start` marks when the new growth, blockage and threshold fields
first became available, and `labour_coverage_start` when worker time use, combat
places and the defence snapshot did. The current `trappedTick` and `defenceTick`
mark the last blockage and defence checks.

## What the player sees

Everything shown to players comes from one table, the metric catalog
(`src/team/stats/MetricCatalog.cpp`). Each entry names a metric, the group it belongs
to (population, food and wellbeing, resources, buildings, military, land, score),
where its numbers come from in the recorded history, and how it is shown. The
arithmetic is in `src/team/stats/MetricSeries.cpp`. The catalog only reads history
that is already recorded; adding a metric there changes neither the simulation
nor the save format.

How a metric is shown follows from what it is:

- A **counter** (units born, wheat harvested, damage dealt) is shown as a rate
  per minute, averaged over the trailing 1, 2 (default) or 5 minutes of samples.
  The running total is one toggle away.
- A **level** (population, wheat in storage, attack strength) is shown as it was
  sampled.
- A **condition of the population** (hunger, health, units looking for food,
  units unable to move) is shown as a percentage of the colony's units. The
  recorded low-food and low-HP thresholds are nested (at or below 25%, 50%,
  75%), so the catalog turns them into four disjoint bands that add up to the
  population.
- **Parts of a whole** (causes of death, what resources were spent on) are shown
  as stacked bands normalised to 100%, one panel per team. Metrics with a
  natural breakdown (births by unit type, harvest by resource, damage by weapon)
  offer the same split in absolute units.
- **Net** metrics (births minus deaths, damage dealt minus taken) run above and
  below zero.
- **Worker time** is counted in worker-ticks, one per worker per tick, so its
  rate per minute divided by the ticks in a minute is the number of workers doing
  something, on average. It is shown that way, or as shares of the workers' time,
  and has no running total. Time spent hungry or hurt with no inn or hospital
  free is its own band rather than part of eating or healing.
- **Means** (distance to resources, distance from the inn, average warrior attack
  level) divide what one counter summed by what another counted over the
  averaging window, and leave a gap where nothing was counted. The warrior level
  is recorded as two skills counted from 0 and shown as their average on the
  game's 1 to 4 scale.
- **The defence snapshot** (warriors by place, what they were doing, enemy
  warriors at home) is charted as sampled, averaged over the window where it
  jumps from sample to sample. Ground within 16 tiles of two colonies counts as
  home for both, so close neighbours count each other's defenders as intruders;
  the explanations say so.
- Population, buildings, attack, defence and damage dealt can also be shown as
  each team's share of all teams.

Samples from before a save gained measurement coverage (or the later extended
and worker-time coverage, each with its own start tick) are left out rather than
drawn as zeros, and a metric with no covered samples says it was not recorded.

The results screen after a match (`EndGameScreen`) opens on an overview of every
team's whole-match figures, lists the metrics by group beside the chart (two
drop-downs, group then metric, on narrow layouts), and explains the selected
metric under the chart. Team chips above the chart show or hide teams; one team
can be highlighted. Pointing at the chart reads out every shown team's value at
that sample. On line charts, ticks on the time axis mark the first sample at
which a colony lost its last unit, lost a unit in combat, or had a building
destroyed; these are derived from the 512-tick samples, so they are accurate to
about 20 seconds. The chart painter is `TeamStatChart`.

In a match, the text-statistics panel pages through the colony summary and then
one page per catalog group (click the page line to advance), giving each
metric's current value: counters as their rate per minute over the last two
minutes, conditions as a percentage of the colony's units. The score group has
no page, since prestige is on the top bar; live spectators get the win chances
as a last page. The pages use the selected team in replay and spectator views. The touch statistics sheet steps
through the same metrics as charts of the player's own colony.

## Verification

The fast unit suites `ComputeExecutor` and `ReadOnlyPhase` cover normal serial and
threaded batches, ordered lanes, empty groups, external completion/resumption,
failures and lifecycle barriers. They require no world or catalog setup. Build
with `python3 tools/dev_build.py unit-tests`, then use `test/run_tests.py` with
`--binary unit --filter 'ComputeExecutor/*' --filter 'ReadOnlyPhase/*'` and the
selected build directory. The engine suite `TeamStatsPipeline` covers calculation
equivalence, publication deadlines, smoothing, retained readers and save migration,
including immediate and deferred calculation failures.

The existing `team-stats-save-test` and `savegame-safety-test` targets cover the
new fields; both already run in Linux and Windows CI. `src/team/stats/TeamLabourStatsTest.cpp`
(suite `TeamStatsSave`) checks that each worker-tick lands in one bucket, compares
place lookups with a scan of every building tile, and covers combat attribution,
the defence snapshot and save round trips. Run locally with disposable
profiles as described in `test/README.md`. The statistics harness also accepts
`--screenshots OUTPUT_DIRECTORY` to render graph pages and a live-panel fixture
at 640×480 and 1024×768 with the supported maximum of 16 teams, large totals and partial legacy history.
`src/team/stats/MetricSeriesTest.cpp` (in `glob2-unit-tests`) covers the catalog and its
arithmetic: rates, bands, percentages, coverage gaps and axes.
`python3 test/check_telemetry_simulation.py build/src/glob2` compares the complete
1,024-tick four-AI and 2,048-tick 12-team checksum sidecars against compressed
version-121 fixtures, plus a 2,048-tick Numbi/Castor scenario. It also reloads a
format-108 mid-run checkpoint and compares
256 team/entity checksum records against the version-121 reference. Aggregate
checksums after save/load include `MapHeader::versionMinor`, so a format-108 save
cannot have the same aggregate checksum as a format-107 save. CI runs the
same check on Linux and Windows (with `.exe` on Windows).
Actual results and platform/performance coverage are recorded with the change's
validation artifacts; CI compilation alone is not deterministic execution proof.

Related: [AI documentation](README.md).

The opt-in `TeamStatsPipeline` benchmark uses a fixed seed, 256 stationary units,
shared snapshot capture and no orders for 1,024 ticks. Run the release test binary
with an isolated `GLOB2_USER_DATA_DIR`, dummy SDL drivers,
`GLOB2_STATS_BENCHMARK=1`, `GLOB2_PERF_DISABLE=1`,
`--test-suite=TeamStatsPipeline --test-case='pipeline versus*'`. It prints
`STATS_BENCH` rows for synchronous, immediate-snapshot and delayed-snapshot modes.
These isolate scheduling/capture costs; they do not establish whole-game speedup.
Never use a fast-development build for the performance comparison.
