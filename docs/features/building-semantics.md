# Building semantics

Companion to [building catalogs](building-catalogs.md).

## Capabilities and units

Material cost objects use `wood`, `food`, `paper`, `stone`, `algae`, `cherries`,
`oranges`, `prunes`, `gold`, `metal`, `glass` and `fabric`. Costs are stock units.
`properties.maxMaterial` and `properties.materialMultiplier` are 15-element arrays:
the first twelve positions use that fixed material order, followed by three reserved
positions. Reserved capacities must be zero; every delivery multiplier must be at
least one. Harvesting supplies a material packet representing one raw unit; supplier
withdrawals retain their exact fraction of a raw unit. Delivery converts that fraction
using the recipient's multiplier, accepts available capacity and reports discarded
remainder explicitly.

`semantics.replenishMaterials` names materials workers should replenish and ordinary
deliveries may add. Storage capacity does not imply replenishment. The four
`semantics.market` sets `suppliesStockMaterials`, `suppliesDirectStockMaterials`,
`fetchesStockMaterials` and `fetchesDirectStockMaterials` independently select
materials for each supply or fetch mode; role and experiment switches still apply.
When both fetch modes are enabled, eligible providers form a union. Each set is an
array of material keys and rejects duplicates. Default permission masks include all
fixed materials; existing recipes and capacities still use their historical inputs.

Legacy resource-named fields and the keys `wheat`, `papyrus`, `cherry`, `orange`,
`prune` remain accepted import aliases. Canonical saved catalogs use material names;
authoring both aliases for one field or material is an error. See
[resource catalogs](resource-catalogs.md) for map deposits that supply these materials.
A configured supplier may expose existing inventory above its nominal capacity.
Outstanding construction and repair materials remain deliverable regardless of
the operating replenishment set.

Unit-class masks use worker=1, explorer=2 and warrior=4; 7 accepts all classes.
Arrays follow that same class order. Service durations advance on inside-unit
updates using `properties.insideSpeed`; healing also scales with missing health,
and training divides that speed by the number of ability levels being gained.
Training includes the completion action: a duration of N takes N+1 inside
actions. Parallel training waits for the slowest requested course under this
inclusive clock, including zero-duration courses, the entry phase, and integer
per-tick advances. Inside services advance by 1–256 delta units per tick,
including the minimum speed after diagonal entry. The upper bound matches the
one-action-per-tick clock and prevents long healing visits from overflowing the
phase counter. It also removes the old post-exit speed burst caused by surplus
phase accumulating during tiny-deficit healing, including stock hospitals;
healing completion still takes at least one tick per inside action. Production uses an inclusive
timeout: a recipe duration of N completes after N+1 eligible producer ticks
(duration zero completes on its first tick). Do not compare these clocks without
conversion.

All paths in this table are relative to one variant:

| Configuration | Behavior |
| --- | --- |
| `semantics.feeding`, `semantics.healing` | Enabled flag, class mask, duration, costs, partial-settlement policy, exit admission and configured outcomes |
| `semantics.training` | Ability-name object with explicit results, duration, class admission, material cost and optional independent construction qualification |
| `semantics.trainingInParallel` | One configured visit may grant its eligible training bundle |
| `semantics.production.recipes` | Unit-name object with independently enabled worker/explorer/warrior recipes, costs and durations |
| `semantics.projectileDamage`, `semantics.projectileBuildingDamage` | Unit-class damage array and independent building damage |
| `semantics.ammunitionMaterial`, `semantics.ammunitionCost` | Material index and amount per ammunition refill; capacity/cadence/range/speed remain explicit properties |
| `semantics.constructionCost`, `semantics.repairCost` | Materials independent of operating storage limits |
| `semantics.repairable`, `semantics.regenerationPerTick` | Permission to repair and passive regeneration are independent |
| `semantics.requiredWorkerLevel` | Construction qualification, separate from work speed and presentation tier |
| `semantics.assignmentLimit`, `semantics.admittedUnitMask` | Shared assignment bound and allowed interior unit classes |
| `properties.maxUnitInside` | Shared interior seats; every enabled interior service needs at least one admitted class and one seat |
| `semantics.placeable`, `semantics.instantPlacement` | Availability for placement and whether a completed variant can be placed directly |
| `semantics.occupiesGround`, `semantics.relocatable` | Independent occupancy and movement behavior |
| `properties.zonable` | Attraction enabled independently for each unit class |
| `properties.defaultUnitStayRange`, `properties.maxUnitStayRange` | Initial and maximum attraction radius |
| `semantics.market` | Shared/local inventory, direct or routed supply/fetch and inter-team fruit exchange |
| `semantics.workPriorityBias`, `semantics.sightSharing` | Worker task preference and visibility sharing policy |

Interior services share seats and inventory. A unit requests a service, and unpaid
material reservations are distinct from occupancy. Completion, cancellation,
expulsion and destruction settle each reservation once. Repair materials that
already restored health are consumed when a repair is canceled. Demolition keeps
construction commitments until removal is final, so cancellation remains safe;
final removal releases new/upgrade funding and consumes paid repair materials. Cumulative paid
materials earn the corresponding fraction of the initial health deficit; damage
received after repair begins remains. A repair returns
to its recorded original variant even when several variants share a repair site.
Current-format loads validate that this completed origin points to the active
site through the appropriate repair or upgrade edge.
Bombing training is independent of school tier, as are worker construction
qualification and work-speed training.

Production has two reusable scheduling policies:

- `weighted_late_choice` shares a timer and selects the class at completion. Its
  enabled recipes must have identical costs and durations.
- `weighted_committed_job` selects and reserves a recipe when work begins. Ratio
  changes affect the next job; a blocked exit retains completed work, spawning
  consumes reserved materials, and cancellation releases them.

Transitions retain production preferences for compatible recipes and initialize
newly enabled recipes from the target variant. Canceling construction restores the
original production preferences. Temporary construction jobs retain these values
in saved state.

A combined building participates in each applicable phase. Material supply,
healing, feeding, training, production and attraction are not mutually exclusive.
Mixed staffing first fills deterministic role quotas, then lends unused assignment
slots to roles with eligible units on the existing recruitment round.
Connected-segment rendering is configured by `crossConnectMultiImage`, a
`presentation.connectionGroup` and its cross-team connection policy; ordinary
sprite rendering remains available for the same capabilities. Ground and overlay
buildings share connection and damage-frame selection; non-segmented healthless
variants use the base sprite frame. `presentation.showLevel`
controls the level label directly, independently of upgrade availability. The stock
market therefore displays its configured level even with its upgrade experiment off.

Supplier selection excludes the recipient itself and any provider drawing from
the same shared inventory. Supplier lists contain only alive buildings; demolition
removes a provider immediately, and canceling demolition restores it. Loading
rebuilds the same membership. Ordinary routed recipients retain shared asynchronous
gradients. A fetch permission uses the ordinary material-source field when no
enabled definition can supply that material or the team has no suppliers of
that mode. Direct recipients share lazy fields; recipients needing supplier
exclusions use separate fields. These synchronous fields share a bounded cache:
64 MiB of cell buffers, or one complete field when a map requires more. Entries
refresh on stock/topology changes and after 128 simulation ticks. Least-recently
used entries are evicted at simulation request boundaries. Saved games retain
resident fields, refresh state and eviction order so continuation follows the
same routes. These consumer-aware requests run on the simulation thread; AI
workers use the existing shared-field interface.

## Building area effects

Completed physical building variants can define an optional `semantics.areaEffects`
bundle. Existing catalogs omit this object and retain their canonical bytes and
fingerprints. Area effects do not change the existing admission services: an inn
can still feed occupants while its area effect feeds outdoor units.
Omitted fields default to zero; zero disables that channel. Benefits use the
emitter's alliance mask, always including its own team. Enemy effects use the
existing attackability rules, including peaceful mode.

```json
"areaEffects": {
  "radius": 8,
  "cost": {"food": 1},
  "healingQ8": 128,
  "feedingQ8": 256,
  "attackBuffBps": 1000,
  "fertilityBuffBps": 2000
}
```

Services pulse every 16 simulation ticks (0.64 seconds at normal speed). Their
unsigned Q8 amounts are 0–65535 per pulse: 128 heals half an HP, 256 restores one hunger
point, and `damageQ8` damages enemy units. Fractions accumulate separately on
recipients; healing and feeding discard surplus fractions at their caps. Damage
resolves before healing, including death. Units entering or inside buildings are
excluded; exiting units become eligible after their attachment is released. Air
and ground units are eligible. Feeding grants no fruit bonuses and follows the
no-hunger rule; damage follows the no-permadeath rule.

Positive percentage modifiers use `attackBuffBps`, `armorBuffBps`, and
`fertilityBuffBps`; weaknesses use the corresponding `*WeaknessBps` names. One
basis point is 0.01%, so 1000 gives 10%. Buffs allow 0–30000 and weaknesses
0–10000. Each channel selects the strongest bonus and strongest weakness
independently, then computes `10000 + bonus - weakness`, bounded to 0–40000.
Attack and armor bonuses benefit allied units and buildings; combat weaknesses
only affect enemy units. Armor scales before existing fruit penalties and retains
magic's armor bypass. Existing combat minimum-damage rules still apply when an
attack modifier reaches zero. Turret attack is captured when firing and target
armor is read at impact. Building recipients use the northwestern central footprint tile:
`position + (size - 1) / 2` on each axis.

`radius` is 0–65535 tiles of square distance from the footprint, including the
footprint at radius zero. Coverage wraps at map edges and visits a tile only once,
even when the radius exceeds the map dimensions. Combat coverage updates each
simulation tick. Fertility is a temporary multiplier on existing land-resource
growth opportunities, capped by the existing four-opportunity limit. It does not
change terrain, habitat permissions, aquatic growth, or zero-growth land.

`cost` accepts 0–1,000,000 units per material and is paid atomically at each pulse,
independent of recipient count. All
emitters pay, including overlapping emitters whose effects are weaker. Shared
inventory payments follow ascending building GID. Configure existing inventory
capacities, replenishment permissions, and staffing to supply these materials.
Insufficient unreserved materials disable the whole bundle until the next pulse;
there is no partial payment or refund. Spending is counted as area upkeep.
For local upkeep, configure matching storage capacities, `replenishMaterials`,
and delivery workers. Shared upkeep uses the existing
`semantics.market.sharedStock` inventory. Upkeep competes with other reservations
through the same material-accounting rules as ordinary building services.
Newly completed buildings wait for the next pulse. Construction, upgrades, and
pending deletion suspend emission; repair retains the original completed
building's bundle and footprint. Type and ownership changes invalidate funding. Relocation and
diplomacy changes update coverage while retaining an otherwise valid paid interval.

Coverage is reconciled before teams step and remains fixed for that tick. Changes
during simulation take effect on the next tick. Growth jobs retain the modifier
captured in their immutable input, including jobs published after an emitter
stops. Saves preserve funding and service fractions; loading reconstructs fields
without charging upkeep or applying services. Format 150 adds this state; earlier
supported saves load with no area-effect funding.

### Compute and memory

The simulation maintains dense, team-major planes only for channels used by
active emitters. Target lookups are constant time. Building lifecycle events mark
16×16 chunks; rebuilding examines only emitters intersecting those chunks and
correctly restores weaker coverage after removing a stronger emitter. Stable
coverage does not visit emitters between upkeep pulses. The disabled catalog path
allocates no coverage fields and consumes no extra random numbers.

Seven optional two-byte planes per team cover healing, damage, feeding, unit
attack/armor, and building attack/armor. One additional two-byte plane covers land
growth. Maximum field payload is `tiles × (14 × teams + 2)` bytes: 226 MiB for a
1024×1024 map with the current maximum of 16 teams. Chunk indexes, emitter records,
scratch storage, and retained growth snapshots are additional. Fields remain
allocated after their last emitter stops and are released when the world or
catalog resets. Fertility snapshots reuse unchanged coverage and copy changed
chunks when refilling pooled buffers.
The same formula would require 296 MiB at 21 teams. On Linux x86-64, optional
funding and fraction bookkeeping adds eight bytes to each authoritative building
and unit record; heap object padding and allocator overhead are separate.

Build `engine-tests` and run `BuildingAreaEffects/*` for reference coverage,
services, combat, snapshot, and save-continuation checks. Run
`BuildingAreaEffectsBenchmark/*` with `--tag benchmark` explicitly for CSV measurements of steady ticks,
upkeep pulses, removal spikes, field payload, and field-buffer allocations. Use
identical seeds and build inputs for comparisons. The performance goal is at most
5% overhead with 128 stationary emitters; evaluate measurements on the intended
workload rather than treating this as a guarantee for every map.
The populated fixture accepts `GLOB2_TEST_AREA_BENCH_MODE=disabled` or `enabled`
to run matched selections independently; leaving it unset runs both.

The dense-field fixture measures coverage maintenance independently of unit
updates. Reference Linux x86-64/GCC 15 release measurements for 1024×1024,
16 teams, all channels, and overlapping radius-eight emitters were:

| Emitters | Steady maintenance | Funding pulse | Diplomacy rebuild | Mass removal |
| --- | --- | --- | --- | --- |
| 32 | about 0.1 µs | 7 µs | 3.7 ms | 3.0 ms |
| 128 | about 0.1 µs | 25 µs | 6.5 ms | 4.7 ms |
| 512 | about 0.1 µs | 106 µs | 16.5 ms | 9.1 ms |

Each nonempty row allocated the same 226 MiB field payload; initial allocation
and publication took about 145–156 ms. The process high-water RSS reached about
381 MiB across the fixture matrix, including map state and bookkeeping; it is
not an isolated measurement of aura memory. An additional retained fertility
snapshot at this map size needs about 2 MiB plus chunk stamps. No-field catalog
runs allocated zero coverage buffers. The fixture asserts that stationary ticks
and successful renewal pulses do not revisit emitters for coverage rebuilding
or allocate new field buffers. Its allocation counter covers field/scratch
buffers, not every allocation in the engine or chunk indexes.

These are workload measurements, not whole-match overhead guarantees. Compare
the populated-match fixture with the same original-commit workload and compiler
on an otherwise idle machine for the acceptance targets; record steady ticks,
pulse ticks, and rebuild spikes separately. Large radii or dense overlaps can
make a rebuild expensive even though steady reads remain constant-time.

A matched populated fixture with 256 workers on a 256×256 map measured the
following median tick costs, using five alternating runs and five seeded repeats
per emitter count on the same Linux/GCC release build:

| Emitters | Disabled steady | Active steady | Active pulse |
| --- | --- | --- | --- |
| 0 | 140 µs | 131 µs | 140 µs |
| 32 | 151 µs | 141 µs | 173 µs |
| 128 | 159 µs | 161 µs | 209 µs |
| 512 | 241 µs | 246 µs | 376 µs |

Active coverage at 128 emitters added about 1% relative to the disabled framework
in this fixture, which uses healthy, nonhungry workers and walls. Combat, enemy
damage and active resource growth are outside this whole-match measurement;
paid upkeep is covered by the separate field-maintenance fixture. These
shared-host measurements contain timing noise; negative differences in the
smaller rows do not establish speedups. The earlier original-engine comparison
retained identical entity checksums and random state with effects disabled.

Related: [features and content](README.md).
