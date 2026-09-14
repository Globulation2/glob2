# Map JSON report: format and metric definitions

`--json FILE` writes a UTF-8 JSON object with `schema_version: 1`. It works with
both map CLI modes and does not require OpenGL. It can be requested alone or
alongside map/PNG outputs:

```sh
build/src/glob2 --generate-map maze --seed 7 --width 128 --height 128 \
  --output artifacts/maze.map --json artifacts/maze.json
build/src/glob2 --preview-map artifacts/maze.map --json artifacts/reloaded.json
build/src/glob2 --preview-map /path/to/colony.game --json artifacts/colony.json
```

Add `--preview artifacts/maze.png` to the first command to write all three outputs.
On `--preview-map`, `--output` remains the PNG filename; `--json` is independent.
All input, config, map, PNG, and JSON paths must be distinct. Existing output files
are replaced. A failed write returns a nonzero exit status.

The complete machine-readable contract is
[map-report.schema.json](map-report.schema.json). Check `schema_version` before
interpreting a report. Objects use named fields, arrays use zero-based indices,
counts are integers, and measurements/percentages are JSON numbers, not strings.
`null` means unavailable, unreachable, or an empty denominator/sample as specified
below. Zero means an actual zero; it never means “unreachable.” The report contains
no NaN, infinity, per-tile grids, elapsed-time measurements, or simulation steps.

## Snapshot and provenance

| Field | Meaning |
| --- | --- |
| `engine.version_major`, `version_minor` | The reporting executable's engine/save-format version constants, not the input file's original version. |
| `map.name` | Stored map name for an input file; `null` for the freshly generated snapshot, before its output filename supplies a saved name. |
| `map.width`, `height`, `tiles` | Tile dimensions and their product. |
| `map.wrap_x`, `wrap_y` | Both true: this is a toroidal map. All reported paths and components wrap. |
| `map.player_slots` | Actual colony/team count. This is what “number of players” usually means when comparing generated maps. An editor-only generator may create fewer colonies than its request's `teams` value. |
| `map.controller_count` | Number of controller slots in `GameHeader`. A fresh generated map normally has zero; controllers are assigned later in the lobby. Multiple controllers can share one colony. |
| `map.saved_game` | Whether the loaded header describes a saved game rather than a premade map. |
| `map.tick` | Snapshot's simulation tick. Analyzing a save does not advance it. |
| `map.game_seed` | Seed stored in the game header. For a generated report it equals the requested generation seed. In a loaded file it is not evidence of the original generator or its settings. |
| `map.colonies[]` | One entry per colony, in team-index order: `team`, `alive`, `start`, live object counts in `units`, and `buildings_and_flags`. |
| `colony.start.x`, `y` | Stored colony-start tile coordinates; `null` if the start is unset. They are not the current centre of the colony. |
| `colony.start.source` | Engine start-position source: 0 unset, 1 unit, 2 building, 3 swarm. |
| `colony.units.workers`, `explorers`, `warriors` | Existing unit objects of each type, including units inside buildings. This differs from the pathfinding source count, which only counts ground-unit tiles currently on the map. |
| `colony.buildings_and_flags` | Existing building objects, including flag objects and construction sites. It is not an occupied-tile count. |
| `map.controllers[]` | Controller `slot`, assigned `team`, and raw `type`: 0 none, 1 being dropped, 2 lost, 3 network, 4 local, or 5 + the AI implementation ID. |

For generation in this invocation, `generation.available` is true and includes:

- `generator`: stable string ID; `legacy_id`: numeric ID; `revision`: generator
  revision; `seed`: exact unsigned 32-bit seed supplied to the service.
- `parameters`: **all resolved registered controls**, including defaults and config/
  CLI overrides. Width and height are tile counts. These values can be supplied
  again through `--set`; the separate `seed` goes to `--seed`.
- `legacy_terrain_type`: the request's internal terrain enum (0 water, 1 sand,
  2 grass). `legacy_resource_amounts`: its entire legacy resource-quantity array,
  indexed by resource ID. IDs 0–7 are the resource types listed below; remaining
  slots are reserved. These are recorded for completeness, not additional CLI
  controls. They are request quantities, not measured amounts in the final map.
- `selection_quality`: the production generator's candidate-ranking report, with
  that generator's own weights and scales. It has the same structure as
  `canonical_quality` below. This is the score used for that generator's design,
  so it is not necessarily comparable with another generator's selection score.

Existing map/save formats do **not** retain the complete generator request.
Reports from files therefore have `generation.available: false`, `parameters:
null`, and a `reason`. The tool does not guess from terrain, filenames, or the
stored game seed. Retain the JSON alongside a generated `.map` to keep provenance.
There is no save-format change. Identical seeds/parameters have the existing
engine's platform/revision reproducibility limits.

**Saved-game reports describe the current snapshot.** Terrain/resource quantities,
reachable territory, and quality use its surviving ground-unit positions; they
are not a reconstruction of initial map balance.

## Reusable numeric structures

A **coverage** object has `tiles` and `percent = 100 × tiles / denominator`.
Unless specified otherwise, the denominator is `map.tiles`. Percentages use
0–100, not 0–1. An empty denominator gives `null`.

A **distribution** has `count`, `min`, `max`, `mean`, `stddev`, `median`, `p10`,
and `p90`. All measurements are `null` when `count` is zero. `stddev` is the
population standard deviation (divide squared deviations by count).
Percentiles use linear interpolation between sorted samples at index
`p × (count − 1)`, with p = 0.1, 0.5, or 0.9. A one-sample distribution has all
percentiles equal to that sample and standard deviation zero. Units are inherited
from the sampled value, such as tiles, path-cost units, or resource amounts.

A **components** object has:

| Field | Meaning |
| --- | --- |
| `components` | Number of connected regions in the selected tile mask; zero for an empty mask. |
| `passable` | Coverage of that mask. For resource patches this means resource-bearing tiles, not walkable tiles. |
| `largest_component_tiles` | Largest region's tile count; zero for an empty mask. |
| `largest_component_percent_of_passable` | 100 × largest-region size / mask size; `null` if the mask is empty. |
| `component_sizes` | Distribution of region sizes, in tiles, one sample per region. |

Terrain land/water regions use **four neighbors** (shared edges). Resource patches
and movement connectivity use **eight neighbors**, including diagonals. All wrap.

## Terrain, resources, space, and fertility

`terrain` partitions every tile into one exclusive category, by its actual terrain
sprite ID from `Map::lookup`: `grass` (0–15), `grass_sand_border` (16–127), `sand`
(128–143), `sand_water_border` (144–255), `water` (256–271), or `unknown` (other IDs).
Each value is coverage. Counts sum to `map.tiles`; percentages sum to 100 (subject
to floating-point rounding). Resource/building occupancy does not change this
classification. Borders are whole mixed tiles, not estimates of fractional land.

`underlying_terrain` separately partitions the engine's underlying terrain grid
into `grass`, `sand`, `water`, and `unknown`, using coverage objects. It need not
match the visible terrain percentages: visible tiles combine adjacent terrain
corners and therefore include border classes.

`resources.occupied` counts all resource-bearing tiles, including unknown types.
`unknown_type_tiles` counts resource IDs outside 0–7, excluding the no-resource
sentinel. `resources.types` always includes `wood`, `wheat`, `papyrus`, `stone`,
`algae`, `cherry`, `orange`, and `prune` (IDs 0–7 respectively):

| Per-resource field | Meaning |
| --- | --- |
| `coverage` | Tiles storing this resource type, including any zero-amount deposits. |
| `percent_of_resource_tiles` | Percentage of `resources.occupied.tiles`; `null` if no resources exist. |
| `stored_amount` | Sum of the resource tiles' stored amount fields. This is current inventory, not estimated lifetime production. |
| `harvestable_tiles` | Deposits with positive stored amount. |
| `eternal`, `clearable` | Flags from the engine's resource-type registry. Eternal stone/fruit deposits must not be interpreted as finite stockpiles. |
| `amount_per_deposit` | Distribution of stored amounts, one sample per occupied tile of this type, including zeros. |
| `patches` | Eight-connected components of this resource's occupied tiles. |

`space` contains:

- `building_footprint`: coverage of tiles whose map occupancy points to a building.
- `buildable`: coverage passing the engine's `isFreeForBuilding` predicate: pure
  grass, no resource, building, or ground unit. It does not test a particular
  colony's visibility, ownership, or construction orders.
- `build_sites_4x4`: number of valid top-left anchors for entirely buildable 4×4
  footprints, including ones spanning a map edge. **Anchors overlap**: this is a
  measure of building room, not how many buildings can be constructed simultaneously.
- `growth_disabled`: coverage of tiles whose `canResourcesGrow` flag is false.
- `land_regions`: four-connected regions of grass, sand, and their border tiles
  (sprite IDs below 256); unknown terrain is excluded.
- `water_regions`: four-connected regions of pure-water tiles (256–271).

`fertility` uses the engine's `Fertility::forMap` calculation. `scale` is 65536:
a raw value f corresponds to the terrain-dependent probability f/65536, before
other resource-growth conditions. This is not production per tick. Deposit amount,
room to spread, the wheat growth divisor, and no-growth flags also affect growth.
The field does not apply the `canResourcesGrow` flag; that is reported separately.

- `all_tiles`: distribution over the full map, with non-grass and grass unreachable
  by deposit spread set to zero by the engine's gating rule.
- `grass_tiles`: the same gated field, sampled only on pure grass.
- `potential_grass_ignoring_deposit_reachability`: terrain potential on pure grass
  without the existing-deposit reachability gate. Useful for spotting fertile
  ground that currently lacks a wheat/wood source.
- `positive_grass`: positive gated fertility on pure grass; **its percentage
  denominator is pure-grass tiles**, not all map tiles.

These are computed fields, not a summary of potentially stale serialized fertility
cache values. Scoring's cache writes are restored exactly after analysis.

## Fairness and its inputs

`canonical_quality` calls the existing `scoreStarts` with the **same default
weights and scales for every generator**. Compare this score across maps.
`generation.selection_quality`, when available, records the generator's own
weights/scales and service result. Both contain:

| Field | Meaning |
| --- | --- |
| `measured` | True only when at least one colony exists and every colony has at least one ground-unit source tile. |
| `unavailable_reason` | Explanation if unmeasured; otherwise `null`. |
| `weights` | Six coefficients in the weighted mean: wheat, wood, fertility, depth, room, isolation. |
| `scale` | All normalization references, catchment/threat thresholds, crowd penalty, and fairness exponent actually used. |
| `colonies[]` | Entries in team order, each with `team`, `raw`, `normalized`, and `total`. Empty if unmeasured. |
| `worst`, `best` | Smallest/largest colony total, or `null` if unmeasured. |
| `fairness` | worst/best when best > 0; otherwise 0. **Equal but nonviable zero-score starts yield 0, not 1.** |
| `score` | worst × fairness^`scale.fairness_exponent`. Rewards both viability and equality. |
| `colony_totals` | Distribution of colony totals. |

The `scale` fields use these units and canonical defaults:

| Field | Default | Meaning |
| --- | --- | --- |
| `catchment_steps` | 24 | Inclusive walking radius for local measurements. |
| `wheat_reference`, `wood_reference` | 24, 32 | Gathering distances at which the corresponding normalized factor reaches zero. |
| `fertility_reference` | 8000 | Mean raw fertility giving a normalized fertility factor of one. |
| `depth_reference` | 450 | Stored wheat-plus-wood amount giving a depth factor of one. |
| `room_reference` | 900 | Overlapping 4×4 anchors giving a room factor of one. |
| `isolation_reference` | 60 | Rival walking distance giving maximum spacing credit. |
| `threat_radius` | 40 | Inclusive walking distance for counting nearby rivals. |
| `crowd_penalty` | 0.15 | Fraction of isolation credit removed per nearby rival beyond the first. |
| `fairness_exponent` | 1 | Dimensionless exponent; zero ignores the fairness multiplier, larger values penalize unequal starts more strongly. |

For example, wheat distance 6 with reference 24 gives a wheat factor of 0.75.
Totals of 0.6 and 0.8 give fairness 0.75 and score 0.45 at exponent 1.
These are comparative design scores, not probabilities of winning.

For each colony, the `raw` measurements are:

| Field | Meaning and units |
| --- | --- |
| `wheat_distance`, `wood_distance` | Minimum walking cost to a neighbor of that resource, plus one gathering step; `null` if unreachable. |
| `catchment_tiles` | Tiles within `scale.catchment_steps` walking steps from current ground-unit sources, inclusive. |
| `build_sites_4x4` | Buildable 4×4 anchors whose top-left tile is in the catchment. Other footprint tiles need not be in the catchment. |
| `wheat_and_wood_amount` | Sum of stored wheat/wood amount reachable from a neighboring tile in the catchment. Each deposit is counted once. |
| `mean_fertility` | Mean gated fertility of catchment tiles, in the raw 0–65536 scale; zero for an empty catchment. |
| `nearest_rival_distance` | Minimum walking distance to another colony's ground-unit tiles; `null` if no rival is reachable. |
| `rivals_within_threat` | Number of reachable rival colonies at distance ≤ `scale.threat_radius`. |

Let clamp(v) mean `max(0, min(1, v))`. Normalized factors are:

- `wheat = clamp(1 − wheat_distance / wheat_reference)`; zero if unreachable.
- `wood = clamp(1 − wood_distance / wood_reference)`; zero if unreachable.
- `fertility = clamp(mean_fertility / fertility_reference)`.
- `depth = clamp(wheat_and_wood_amount / depth_reference)`.
- `room = clamp(build_sites_4x4 / room_reference)`.
- For `isolation`, spacing is 1 if no rival is reachable, otherwise
  `clamp(nearest_rival_distance / isolation_reference)`. Multiply spacing by
  `max(0, 1 − crowd_penalty × max(0, rivals_within_threat − 1))`.

`total = sum(weight × factor) / sum(weights)`, or zero when weights sum to zero.
It is forced to zero if either wheat or wood is unreachable. All normalized
factors and totals are dimensionless, normally 0–1. The default weights are
0.22, 0.22, 0.25, 0.12, 0.10, and 0.09 respectively. Always read `scale` and
`weights` from the report rather than assuming those values remain fixed.

## Distances, access, and territory

`start_position_euclidean_distances` is a square matrix of geometric distances in
tiles between **stored start coordinates**, taking the shorter displacement across
each wrapped axis. It ignores obstacles. Entries involving an unset start are
`null`. This differs from travel matrices, which start at current ground units.

`movement` contains three analyses of the same snapshot:

| Model | Entry costs and blocked tiles |
| --- | --- |
| `walking` | Engine ground-unit passability with swimming disabled and no team-specific forbidden areas. Every permitted step costs 1. Water, buildings, and resources block travel. |
| `walking_and_swimming` | The same engine predicate with swimming enabled. Water also costs 1; buildings and resources, **including algae on water**, still block travel. |
| `walking_and_clearing` | The existing analysis `StepCosts::chopping(4)` model. Open ground costs 1; wood, wheat, papyrus, and algae on non-water tiles cost 4. Water, buildings, stone, and fruit block travel. This is a comparison cost model, not an estimate of actual clearing time. |

Paths use eight neighbors and equal-cost diagonal steps. Diagonal corner cutting
is allowed by the engine's flood rule. Transient unit collisions, fog, diplomacy,
forbidden-area orders, and a particular unit's speed/upgrades are ignored.
Sources are every currently mapped ground-unit tile of a colony (not just workers;
not air units or units inside buildings). Sources begin at cost zero even if
otherwise blocked. This measures map access, not travel time in ticks or seconds.

Every model has these fields:

| Field | Meaning |
| --- | --- |
| `step_costs` | Explicit entry cost for open/water/clearable-resource/eternal-resource/building tiles; `null` means blocked. Resource/building blocking still applies on water for the engine walking/swimming models. |
| `between_colonies` | N×N matrix: row = from colony, column = to colony. Minimum cost from any source of the first to any ground-unit tile of the second. `null` if no route; diagonal is always 0, even for a source-less colony. |
| `nearest_rival` | Smallest reachable off-diagonal cost in each row; `null` if none exists. |
| `nearest_rival_spread` | Largest minus smallest nearest-rival cost; `null` if any colony lacks a reachable rival, 0 for no colonies. |
| `unreachable_directed_pairs` | Number of null off-diagonal entries. Each direction counts separately. |
| `directed_pair_costs` | Distribution of finite off-diagonal costs. Unreachable pairs are omitted; consult their count too. |
| `connectivity` | Eight-connected components of the model's passable tile mask, independently of unit locations. |
| `territory.unreachable_tiles` | Tiles reached from no colony source. This includes blocked terrain and resources. |
| `territory.tied_tiles` | Tiles whose minimum cost is shared by at least two colonies. Counted once globally. |
| `territory.cost_to_closest_colony` | Distribution of each reached tile's minimum cost; excludes unreachable tiles. |
| `colonies[]` | Per-colony accessibility and resource measurements below. |

Per-model colony fields:

- `team` and `source_tiles`: team index and the number of starting ground-unit tiles.
- `reachable`: coverage of all tiles with a finite cost from this colony.
- `catchment_tiles`: tiles at cost ≤ `definitions.catchment_cost` (24). For the
  clearing model these are **weighted costs**, not 24 ordinary walking steps.
- `reachable_build_sites_4x4`, `catchment_build_sites_4x4`: currently buildable
  anchors whose top-left tile is reachable, or reachable within cost 24. They do
  not assume that clearing resources has actually made new construction space.
- `exclusive_nearest_territory_tiles`: tiles strictly closer to this colony than
  to any other; unreachable tiles excluded.
- `tied_nearest_territory_tiles`: tiles where this colony ties for minimum cost.
  These are inclusive per-colony counts, so their sum can exceed global tied tiles.
  Sum of **exclusive** counts + global tied tiles + unreachable tiles = map area.
- `resources.<type>.nearest_gather_cost`: minimum cost to a neighboring tile plus
  one, or `null`. Only positive-amount deposits count in these access summaries.
- `reachable_deposit_tiles`, `reachable_stored_amount`: accessible deposit count
  and sum of stored amounts, each deposit counted once.
- `catchment_deposit_tiles`, `catchment_stored_amount`: the same, restricted to
  deposits with an approach tile at cost ≤ 24. Thus gather cost can be 25 at the
  catchment boundary. Fields are zero if there are no accessible deposits.

The walking quality scorer and the movement/access tables reuse production
helpers but remain distinct measurements: generator-specific quality can use a
different catchment radius, and the access table explicitly requires positive
stored amounts. No new pathfinding or simulation behavior is introduced.
