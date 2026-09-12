# Maxima strategy configuration

Maxima uses the strict schema-v2 contract and resolves a complete immutable
strategy once per match. Its sources,
from lowest to highest precedence, are the complete base file, the inferred or
explicit format file, ordered `--maxima-layer` files, inline
`--maxima-overrides`, and `GLOB2_MAXIMA_OVERRIDES`.

Files contain one `section.key = value` assignment per line and use `#` for
comments. Sparse layers may be empty. Assignments are strict: unknown keys,
duplicates in one source, malformed values, hard-range violations, and unsafe
relationships are errors; the resolver never clamps values.

Schema-v1 and Original Nicowar keys have no aliases. Supplying one is an
unknown-key error; optimizer warm starts must explicitly record strategy schema
version 2.

The binary is the authoritative optimizer interface:

```text
glob2 --dump-maxima-schema
glob2 --dump-maxima-strategy --maxima-format 2v2
```

The first command emits types, units, groups, descriptions, hard bounds, and
recommended search bounds. The second emits fully resolved values, ordered
sources, and source/line provenance for every key.

## Decision groups and impact

The base file is ordered in the same direction that a decision flows through
Maxima. This keeps related parameters adjacent and makes sparse experimental
layers easy to review:

| Group | What changing it affects |
| --- | --- |
| `model` | Engine-capacity estimates used by the director. |
| `staffing` | The per-building staffing control loop, plus worker counts for construction, upgrades, towers, and clearing flags. |
| `environment` | Translation of terrain, resources, food service, topology, momentum, and attacks into normalized observations. |
| `trends` | Responsiveness of population, labor, army, food-pressure, and colony-pressure direction estimates. |
| `demands` | Translation of observations into survival, growth, access, technology, military, and aggression demand. |
| `economy` | Building targets, birth ratios, service safeguards, and large-economy adaptation. |
| `upgrades` | Upgrade eligibility, concurrent capacity, labor, and per-building priority. |
| `construction` | Construction concurrency and its population, utility, and emergency gates. |
| `military` | Force targets, reserves, campaign readiness, training throughput, hospitals, and tower escalation. |
| `postures` | Utility and readiness rules choosing recover, defend, expand, develop, mobilize, campaign, or finish. |
| `placement` | Candidate utility components, placement weights, routing costs, labor estimates, and action timeout. |
| `defense`, `tactics`, `raiding` | Reactive defense and execution policy after strategic authorization. |
| `explorer_campaign`, `fruit`, `recon` | Explorer production, mission count, target valuation, retasking, and fruit work. |
| `farming` | Review cadence, fertility policy, wood pressure, buffers, and proactive clearing. |
| `food` | Protected farm capacity claimed by inns and swarms, and the placement, upgrade and retirement decisions taken from it. |
| `scoring` | Cross-policy construction priorities and target/posture switch margins. |
| `scheduling` | Director and executor cadence, phase offsets, commitment, warnings, and cooldowns. |
| `emergencies` | Food and combat conditions that override normal posture selection. |

Food-site potential uses nearby farmable productive capacity: the sum of exact
soil fertility over grass currently growing corn with a positive resource amount.
Empty fertile grass contributes nothing. Changing a positive corn amount does
not change the score, but removing the crop does. The bounded search (12 tiles by default,
`staffing.swarm_supply_radius`) can traverse corn and empty land, excludes
buildings, other resources, unexplored land and inaccessible water, and includes
Maxima's protected farm cells while respecting other forbidden areas. Capacity
measures fertility of existing wheat, not current harvest throughput or stored food.

A sole completed swarm receives the full allowed colony worker budget, even
with no nearby wheat. Multiple completed swarms share that budget proportionally
to this capacity,
with building ID breaking integer-allocation ties. There is no stock-history
warm-up or equal-share floor. Inn staffing uses the same capacity: for capacity
`C`, normal level staffing `N`, and the larger configured level staffing envelope
`M`, the target is `ceil(M*C/(C+65536*N))`. Legacy low-corn thresholds and the
swarm low-corn multiplier are retained for config compatibility but no longer
control these assignments. Disabling adaptive inn staffing retains fixed normal
staffing. Actual hunger safeguards still control the colony birth budget.

Every completed swarm uses the director's explorer production weight while the
colony is below its explorer target, before and after prestige. Explorer
production has no designated swarm or building-ID gate. The explorer weight is
at least one whenever the colony is below its full target; utility can increase
the weight but cannot block production. A zero colony birth budget pauses all
unit types. Individual zero-worker shares retain the
common production mix and may use stored corn.

Colony food-resource potential sums the union of the nearby farm cells (overlap
is counted once) in fully fertile tile equivalents. Swarm retirement and food
placement opportunity also count only existing wheat.
The former `staffing.swarm_fertility_bias_percent` setting has been replaced by
`staffing.swarm_supply_radius`.

Active reconnaissance missions (including economic watches) wait until every map
cell has been discovered. Default explorer behavior performs initial exploration;
passive observation and intelligence memory continue throughout. After full
exploration, the existing mission and emergency limits still apply.

## Economic and tactical independence

Food emergencies affect economic recovery and production, not attack or defense
authorization. Falling population combined with rising hunger no longer triggers
a food emergency; absolute food-service thresholds still do. Economic posture
does not veto warrior missions, explorer attacks, or reconnaissance. Actual colony
defense emergencies, eligible forces, reachability, target safety and cooldowns
still govern military decisions.

The siege progress lock applies only while its target opponent is alive. Once
that opponent is eliminated, both siege selection and route-clearing selection
can consider the remaining enemies without waiting for the lock timer to expire.

`emergencies.population_trend_threshold`, `emergencies.food_trend_threshold` and
`military.campaign_sustainable_food_percent` remain accepted for compatibility
but have no runtime effect.

## Relentless offense

Warriors attack continuously. Every `tactics.review_interval_ticks` Maxima
counts the warriors that satisfy the flag level in `tactics.flag_minimum_level`
(1 admits untrained warriors) and are free or already on its offensive flag.
Training comes first: every open barracks slot is reserved for a warrior, and
only the surplus beyond that capacity is offered to the flag. When at least
`tactics.min_force` surplus warriors can reach a remembered enemy building or a
visible worker cluster, one war flag is placed directly on the best target and
requests that surplus, up to `military.attack_unit_cap`; the request follows
the surplus as warriors enter and leave training. Buildings score by type
value minus tower penalty and route distance; clusters use the raid cluster
score minus route distance. The flag only moves when its target disappears or,
after `tactics.dwell_ticks`, when a rival target beats it by
`tactics.retarget_margin`. There is no muster, rally,
casualty withdrawal, cooldown or defensive reserve gate. A visible siege target
that takes no damage for `tactics.stall_ticks` is quarantined for
`tactics.failed_target_quarantine_ticks`. A colony emergency recalls the flag.
When every known building is sealed behind resources, `tactics.dig_out_enabled`
opens a corridor with the clearing workers the director allocates.

## Policy switches

Every independently recognizable Maxima policy now has a Boolean switch in
addition to its tuning parameters. All switches default to `true`, preserving
the prior base-strategy behavior. A group-level switch suppresses all of its
children; child values may remain configured and are simply ignored while the
parent is off. Maxima also removes policy-owned transient state where relevant,
including reconnaissance missions, tactical and reactive-defense flags,
farming forbidden areas, maintenance clearing areas, and active land-clearing
flags.

The complete switch inventory is:

| Policy area | Switches |
| --- | --- |
| Economy adaptations | `economy.swarm_retirement_enabled`, `economy.large_economy_adaptation_enabled`, `economy.amphibious_network_maintenance_enabled`, `economy.food_service_safeguards_enabled`, `economy.worker_birth_throttle_enabled` |
| Development actions | `upgrades.enabled`, `repairs.enabled` |
| Military responses | `military.explorer_defense_enabled`, `military.warrior_training_backlog_throttle_enabled`, `military.preemptive_defense_enabled`, `military.preemptive_amphibious_enabled` |
| Strategic postures | `postures.recover_enabled`, `postures.defend_enabled`, `postures.expand_enabled`, `postures.develop_enabled`, `postures.mobilize_enabled`, `postures.campaign_enabled`, `postures.finish_enabled` |
| Placement heuristics | `placement.food_preservation_enabled`, `placement.defensive_siting_enabled`, `placement.spacing_compactness_enabled`, `placement.artery_routing_enabled` |
| Colonization and defense | `colonization.enabled`, `defense.reactive.enabled` |
| Combat tactics | `tactics.enabled`, `tactics.siege_enabled`, `tactics.dig_out_enabled`, `tactics.failed_target_quarantine_enabled`, `raiding.enabled` |
| Explorer work | `explorer_campaign.enabled`, `fruit.enabled` |
| Reconnaissance | `recon.enabled`, `recon.scouting_missions_enabled`, `recon.economic_watch_enabled`, `recon.force_memory_enabled` |
| Farming and clearing | `farming.enabled`, `farming.farm_protection_enabled`, `farming.maintenance_clearing_enabled`, `farming.proactive_clearing_enabled` |
| Emergency overrides | `emergencies.food_enabled`, `emergencies.colony_enabled` |

At least one `postures.*_enabled` value must remain true. The resolver rejects a
strategy that disables all seven postures because the director would otherwise
have no valid state to select.

The functional group explains *where* a parameter acts. Every parameter also
has an orthogonal `StrategyParameterImpact` rank explaining how broadly a
meaningful change is expected to affect overall behavior:

| Impact | Rank | Meaning |
| --- | ---: | --- |
| `critical` | 4 | Can redirect posture, survival behavior, or colony-wide population, construction, or force allocation. |
| `high` | 3 | Materially changes a full policy, capability gate, or major production/service budget. |
| `medium` | 2 | Tunes an operational priority, timing rule, local allocation, or execution policy. |
| `low` | 1 | Controls narrow geometry, phase offsets, fallback values, or uncommon edge-case behavior. |

Impact is expected strategic blast radius, not confidence in the current value
and not an instruction to tune all high-impact parameters together. The enum is
required on every registry entry, exported as both `impact` and sortable
`impactRank`, and checked by tests so new parameters cannot be left unranked.

Every key's schema description states its direct behavioral effect. Units make
the arithmetic domain explicit, and hard bounds reject unsafe input. Recommended
search bounds are conservative, unit-aware windows centered on the current base
value, so iterative optimization explores a useful neighborhood instead of the
entire safety domain. Promoting a new base value automatically recenters the next
search. Parameter relationships that
cannot be expressed by individual bounds—ordered thresholds, population
windows, and construction caps—are checked after all layers resolve.

## Optimization scope

The complete schema is a behavior inventory, not a recommendation to optimize
every dimension in one experiment. Impact ranking helps choose review priority. Whatever sets a parameter, the
value is sent back through the runtime resolver before a match, so both
individual hard bounds and cross-parameter relationships are enforced.

Search windows are deliberately local starting ranges, not claims about an
optimal domain. After evidence supports a promoted value, updating the base
recenters the next experiment around it while preserving the wider hard safety
domain for deliberate manual overrides.

## Preemptive defense

Maxima analyzes known map topology every
`scheduling.preemptive_defense_recompute_ticks` (500 ticks by default). The
executor checks every 150 ticks, so an unchanged layout is normally refreshed
on the first executor pass after the interval, every 600 ticks with the default
cadences. Changes to completed-building footprints, the force-scaled zone cap,
or swimming eligibility invalidate the cached result immediately for the next
executor pass. Fog-of-war and resource changes are incorporated by the periodic
refresh.

The land analysis accepts discovered, resource-free, non-water tiles. When
`military.preemptive_amphibious_enabled` is true and at least
`military.preemptive_swimming_warriors_min` trained warriors can swim, a second
analysis accepts discovered, resource-free land and water tiles. Both modes use
eight-neighbor toroidal paths, known completed-building footprints, the same
near-shortest defensive band, and one representative for each connected
qualified component. Candidates are ranked by enemy-team membership, narrowest
cross-section, closeness to the middle of the defensive band, movement mode
(land wins an otherwise exact tie), and row-major tile index. Mutually
overlapping land/amphibious candidates are collapsed while lower-ranked
candidates continue to be considered.

Preemptive defense is inactive below four trained warriors, even if a custom
minimum is lower. Above that floor, the shared land/amphibious capacity is
`max(1, trained warriors / military.preemptive_warriors_per_zone)`, limited by
`military.preemptive_zone_max` when that value is nonzero. The default values
therefore allow one zone at 4–5 warriors, two at 6–8, three at 9–11, and six at
18 or more. A zero configured maximum means no additional configuration cap;
force scaling still applies. Each selected center paints a movement-reachable
radius controlled by `military.preemptive_zone_radius` (3 by default), and all
accepted footprints are unioned into Maxima's owned guard-area tiles.

Food and colony emergencies do not clear or suspend these zones. Reactive war
flags still take precedence when warriors are assigned, while the guard layer
continues to provide passive enrollment. Disabling the feature or dropping
below the four-warrior activation floor clears only guard tiles owned by this
system; unrelated manually painted guard areas are not adopted or removed.

Schema-v2 base files are complete rather than sparse. Existing strict custom
base files must add `military.preemptive_warriors_per_zone`,
`military.preemptive_amphibious_enabled`, and
`military.preemptive_swimming_warriors_min`. They must also add
`recon.force_memory_hold_ticks`, `recon.force_sample_interval_ticks`, and
`recon.force_sample_phase_offset_ticks`, plus the policy switches listed above;
ordinary sparse strategy layers do not need to add them.

Enemy force sightings use a lightweight pass separate from the strategic
director. With the defaults, Maxima samples currently visible enemy warriors
and explorers every 10 ticks at phase 1, while the complete reconnaissance and
strategy pass remains every 100 ticks at phase 17. The lightweight pass does
not rescan buildings, workers, exploration coverage, gradients, or mission
objectives. Phase 1 avoids the default strategic, construction, tactical,
defense, fruit, explorer-attack, and every-eight-tick placement work.

The largest recent simultaneous force sighting is retained at full strength
for `recon.force_memory_hold_ticks` (2,500 ticks by default), then decays
linearly to zero at `recon.memory_horizon_ticks` (10,000 ticks by default).
At the original 25 ticks per second, those defaults are a 100-second hold and a
total memory of 6 minutes 40 seconds. General intelligence confidence continues
to describe overall contact freshness; it is separate from the force-specific
hold.

## Compatibility baseline

Newly exposed keys in `data/maxima/base.strategy` use the exact literal that
previously appeared in the implementation. Moving a value into the schema must
not by itself change a match. Intentional tuning remains a separate change to
the base file or a sparse layer, with provenance visible through
`--dump-maxima-strategy`.

Placement upgrade labor is deliberately sourced from
`upgrades.level1_workers` and `upgrades.level2_workers` in both candidate
scoring and execution. This prevents the planner from evaluating a 4/8-worker
upgrade and then issuing the configured 8/12-worker assignment.

New inner-settlement construction also pays a food-zone opportunity penalty.
`placement.food_zone_penalty_weight` scores a configurable halo around existing
corn. Schools, barracks, racetracks,
and pools use `placement.inner_food_zone_multiplier`; hospitals and towers use
their dedicated, lower multipliers. Inns and swarms are exempt from the halo
penalty because their workers need short routes to food, but all buildings still
pay `placement.farm_loss_weight` when their reserved parcel would directly
reduce productive farming capacity. The penalty is limited to new parcels and
does not discourage upgrades to preexisting buildings.

## Literal coverage policy

A numeric value may affect Maxima strategy only when it is represented by a typed
member in `MaximaStrategy`, registered in the schema, and supplied by the
complete base file. Remaining runtime literals are restricted to serialization
and version identifiers, engine enum/terrain/level representations, neutral
arithmetic identities and unit scales, invalid-id or time sentinels, and fixed
deterministic algorithm geometry (for example the exact fertility kernel).

`test/MaximaLiteralManifest.json` records the reviewed numeric-token
inventory and its allowed classifications for Maxima, farming, recon, and the Maxima
runtime. `MaximaStrategyPolicyTest.py` rejects any inventory change until the
new literal is either moved into the strategy registry or explicitly reviewed
as an architectural invariant and the manifest is updated.

### Unified swarm capacity controller

Swarm staffing and construction share one controller in `AIMaximaSwarmController.h`:

```
B0 = min(W, a * sqrt(W))
B  = B0 * F / (F + q * B0) / (1 + s * H)
N  = max(1, ceil(round(B) / k))
```

- `W`: living workers.
- `F`: deduplicated, discovered, reachable corn/wheat acreage weighted by its
  exact fertility, within the configured supply radius of completed food
  buildings and swarms. Empty fertile ground is not an existing crop.
- `H`: `max(critical_food, unserved_food) / population`. The two counts overlap;
  ordinary hunger is not a food-service failure and is not included.
- `a`: `economy.swarm_labor_scale_percent / 100`, growth speed.
- `q`: `economy.swarm_food_per_worker_percent / 100`, food required per birth
  worker; funding is halved when `F = q * B0`.
- `s`: `economy.swarm_pressure_sensitivity`, continuous hunger response.
- `k`: `economy.swarm_workers_per_building`, funded workers per desired swarm.

`B` is the entire colony's birth-worker budget. It grows without a fixed global
cap, but cannot exceed the actual workforce. Integer square root and thousandths
of a worker make evaluation deterministic; only the final actuator outputs are
rounded. With no reachable supply, funding is zero. The starting producer target
remains one. Existing useful swarms are not demolished when funding falls.

No survival utility tier, food-emergency threshold, recovery phase, abundance
exception, or executor hunger multiplier overwrites this budget. Existing swarm
count is not an input, so constructing another swarm cannot manufacture its own
staffing demand. Colonization has independent construction permission for genuinely
new food territory; completed colonies still count toward this production target. The existing supply-weighted
allocator distributes the budget over completed swarms. Construction scheduling,
inn policy, unit production mix, and retirement of unusable remote swarms remain
separate responsibilities.

The old global caps, committed swarm target, per-headroom staffing tiers, birth
survival thresholds, and their unused bonuses have been removed from the schema.
Old strategy overlays containing removed keys are rejected rather than silently
ignored. The full migration list is saved with the controller calibration report.
All formats initially share the four coefficients; old 2v2 swarm-count overrides
have been removed.

The coefficients were fitted with equal map weights, holding out entire maps
and seeds. They initialize an interpretable policy; they do not claim an
optimal strategy.


## Independent colony construction

A colony is an investment in additional food access, not another production-count
exception. A separate intent may request one swarm even when the production target
is satisfied. It consumes the normal construction capacity and needs at least the
configured swarm construction crew in net free labor (free workers minus open jobs).
There is no population, food-headroom, recovery, emergency, or cooldown gate.

Eligibility is strict: at least `colonization.minimum_anchor_distance` (24 wrapped
Manhattan tiles) from existing or planned swarms **and inns**, at least
`colonization.minimum_new_food` (8) additional fertility-weighted food units, and
parcel threat no greater than `colonization.maximum_threat` (35). Pending food
buildings reserve their food claims. Claims use discovered corn, reachable from
building edges within `staffing.swarm_supply_radius`, with swimming and forbidden
terrain respected. Claimed food is subtracted, not counted twice. Issue-time
validation repeats eligibility and excludes only the action's own reservation.

Candidate value is `100 * new_food / (1 + construction_materials + builders *
anchor_distance)`. This is a simple establishment-cost proxy, **not measured worker
time**. Require `colonization.minimum_value` (5); priority uses the existing placement
score scale. Initial thresholds are provisional and need multi-map calibration.
Distance and conquered-land bonuses have been removed: distance is a cost, not a
benefit. Old parameter overlays using removed gates or scoring knobs must migrate.
The two cleared-site memory parameters remain for diagnostics/save compatibility.

Only one colony can be under construction or starting up. Completion does not release
that allowance until the swarm has workers enrolled and enough corn for a birth;
destruction also releases it. A runtime latch remembers successful provisioning,
so later idleness does not block future expansion. Loading a save conservatively
rechecks provisioning. Colonies use the same supply-weighted staffing budget as all
other swarms, and never create an extra staffing allowance. New accessible food can
increase that shared budget through the existing smooth controller.

Telemetry includes `colony_new_food`, `colony_value`, and the eligibility gate.
Legacy serialized utility slots are retained for compatibility, with their new
meanings documented in the placement code.
