# Savannah

## On this page

- [Controls and supported requests](#controls-and-supported-requests)
- [Geometry budget and numerical choices](#geometry-budget-and-numerical-choices)
- [Reusable framework operations](#reusable-framework-operations)
- [Stages, error handling and fallbacks](#stages-error-handling-and-fallbacks)
- [Telemetry](#telemetry)
- [Playtest limitations](#playtest-limitations)
- [Verification](#verification)
- [Implementation source](#implementation-source)

## Controls and supported requests

| Control | Values | Default | Meaning |
| --- | --- | --- | --- |
| Watering holes | Sparse / Normal / Many | Normal | Targets 2 / 3 / 4 **neutral** ponds per 128×128 area, in addition to home ponds and the plain's pools. |
| Dry patches | 0–20%, step 1 | 8% | Share of eligible plains vertices converted to sand, outside home, approach and feature reservations. This excludes structural sand margins and beaches. |
| Wheat, wood, stone, algae, fruit amounts | 0–300%, step 25 | 100% | Additional deposits; exact tile counts saturate when eligible plots fill. |

Sides are existing power-of-two sizes **128, 256, 512**, square or rectangles up to
**2:1**. At least **4,096 tiles per colony** and **48 tiles between home centres**
are required. The engine currently supports at most 16 colonies. Requests failing either actual spacing or the essential-feature fit
are rejected with an actionable error. Homes never shrink to accept crowding.
The generic controls retain their existing ranges for other generators.

Zero abundance retains each colony's pond, **20 renewable wheat tiles, 20 renewable
wood tiles and four stone tiles**. All other resources respond to their controls.
Ponds, plot outlines and sand margins remain at zero. Algae has no starter floor.

## Geometry budget and numerical choices

These are design heuristics, except where an engine rule is identified. They are
not universal survival thresholds. The comments beside the constants and placement
operations explain their units; the evidence section records actual verification.

- **Homes:** radius 20 with an 8% radial ripple for ownership; a radius-23 reservation
  keeps decoration off their margins. Sites come from the roomiest existing lattice,
  receive at most four tiles of jitter per axis, then are randomly dealt to colonies.
  A proposed move that breaks 48-tile spacing leaves that site in place. One proposal
  per site keeps runtime bounded; this is not a search for an optimal layout.
- **Town:** the swarm lies six tiles west of home centre, from the shared `Homes`
  calculation. The eastern half carries food, wood and water. The western half stays
  available for ordinary buildings and upgrades. The final check requires at least
  16 reachable **8×8** anchors within 24 walking steps, each large enough for a 4×4
  building with two tiles of margin. Anchors overlap; this does not mean 16 separate
  buildings fit.
- **Approaches:** two opposed five-tile strips run north and south from the western
  town side, 5–24 tiles from home centre. Reservations extend two tiles farther and
  have two extra tiles on either side to absorb sand-corner effects. Final checks
  also require both ends to join the same five-tile-wide plains network: erode the
  walkable mask by two and require cardinal connectivity of its remaining core. They
  require every strip tile to be walkable and reachable from the colony's workers.
  Moving units do not count as permanent route obstacles.
- **Home crops:** wheat has radius 6.5 at (+6, −11), wood radius five at (+6, +10),
  both with 18% ripple. The pond is at (+9, 0), radius 5.5 with 25% ripple. Crop
  planting ranks exact engine fertility; dry tiles cannot count toward starter
  floors. At 100%, wheat has 20 guaranteed plus 20 scaled initial deposits; wood
  has 20 guaranteed plus eight scaled. The first calibration used radius-four
  wheat, eight scaled deposits and a radius-3.5 pond. Long games exposed food
  pressure, prompting the 40-tile wheat start within the same home reservation.
  Later four-colony games reduced 236 global initial wheat tiles to 66/93 at
  tick 24,000. The final tune enlarges *unplanted* crop growth room while leaving
  those 40/28 initial counts unchanged. It extends both plots away from town:
  the wheat western cap stays at approximately `5 − 5.5 − 2 = 6 − 6.5 − 2 = −2.5`
  local x, and the wood cap at `5 − 4 − 2 = 6 − 5 − 2 = −1`. The two-corner sand
  stencil and radial roughness make these nominal bounds approximate on final tiles.
  A rejected tune shifted the wheat cap toward town and planted 50 initial tiles;
  Numbi then harvested zero wheat on both test seeds and starved before combat.
  Preserving nearby food-inn space is therefore a play constraint, even when static
  gathering paths say more wheat is reachable. Both final plots fit the radius-23
  reservation and leave the western five-tile approaches clear.
  The radius-two quarry sits 18 tiles east, leaving its grass beyond the pond’s
  beach and its sand cap within the home reservation.
- **Algae:** one radius-one clump per 30 eligible pure-water tiles at 100%,
  scaled by abundance using the existing fertility-aware `seedAlgae` operation.
  It never creates water or changes land connectivity.
- **Neutral ponds:** radius 5.5, 25% ripple; north/south crop plots radius five,
  offset ten tiles, with targets 38 wheat and 28 wood at 100%. Three radius-two
  fruit plots lie on the east side, seven tiles apart, with two deposits of each
  kind at 100%. The west in this local frame remains clear for a forward inn and its margins.
  Each neutral module receives a separately seeded heading, so these directions
  rotate across the map in seeded tenth-degree increments (3,600 possible headings).
- **Neutral placement:** enumerate every integer centre whose shared Euclidean
  distance transform clears the home and approach reservations. Shuffle eligible
  centres, then stable-sort by summed squared distance to the two closest homes
  (one home for solo maps). This favours neighbouring frontiers without targeting
  the map centre. Stable ties retain the seeded shuffle order. An earlier budget
  of 1,600 darts per 128-square area missed a narrow legal gap at seed 1001 on a
  128×128 four-colony map; exhaustive eligibility removes that sampling failure
  without shrinking homes or rerolling the seed. A radius-19 feature reservation plus a one-tile gap
  must fit clear of every home, approach and previously accepted feature.
- **Plains:** shared dart throwing with spacing 22 proposes small groves. An 8-tile
  clearance box must be free before a feature is accepted; radius seven is reserved
  afterwards. Four of every five accepted features are radius-three wood groves
  with seven tiles at 100%; the fifth is a radius-two quarry with two stone tiles.
  These optional wood deposits may be finite dry reserves. Home wood is renewable.
- **Pools:** shared dart throwing with spacing 40 proposes small pools on the open
  plain; a 9-tile clearance box must be free of every reservation, and radius eight is
  reserved afterwards. A pool has radius 3.5 with 25% ripple: a few pure-water tiles
  after beaches, water to be seen rather than farmed. It waters the plain round it,
  but the plain holds no crop, so containment is unaffected. Pools are not counted
  among the ponds the final check requires pure water at.
- **Lone trees:** shared dart throwing with spacing 12 proposes tree sites on the
  plain outside every reservation and plot, each a tree or a clump of two or three
  (the candidate's index says which). Only tiles of dry pure grass (crop growth chance
  zero, so the engine's water probe never lets them spread) are planted, scaled by the
  wood amount; at zero there are none. The final containment check admits a tree
  outside a plot only on such dry ground.
- **Dry patches:** periodic noise with cell period 14 makes coherent patches instead
  of speckles. `sprinkleSand` converts the highest-ranked eligible vertices and
  keeps three steps from water. Beaches are laid after all terrain operations.
  Plot eligibility is then trimmed to final pure-grass tiles; beaches may remove
  pond-facing edge tiles, but starter floors are never reduced.

## Reusable framework operations

The change adds operations to existing modules, with no changes to existing defaults:

- `Orbits::jitterSites`: bounded integer proposals on a torus, accepted only while
  preserving caller-supplied minimum spacing. Returns the accepted count.
- `Farmland::stampContainedPlot`: arbitrary grass-corner sets surrounded by a
  two-corner-wide square-stencil sand margin. Returns the actual pure-grass tile
  list, including across map seams. The caller reserves enough space first;
  stamping deliberately overwrites its footprint.
- `Farmland::plantContainedPlot`: deterministic fertility ranking within an explicit
  tile list, optional renewable-only eligibility, a hard count ceiling, and an
  actual planted count. It excludes occupied tiles and supports finite groves and
  permanent quarries as well as crops.
- `Farmland::containedPlotsMismatch`: checks final grass adjacency against plot labels
  and rejects spreading crops planted outside those labels, except a tree on ground
  whose crop growth chance is zero. Any eight-neighbour grass connection out of a plot
  or into another plot fails.

The engine expands wheat and wood only onto neighbouring grass. The final adjacency
check therefore proves that these crops cannot spread out of their plots under
ordinary resource growth, for any number of ticks. It also keeps wood from invading
wheat. This proof does not cover an editor changing terrain or a player deliberately
planting resources outside the designed plots. No tile growth flags, growth mechanics,
simulation scheduling, save format or replay/network versions change.

## Stages, error handling and fallbacks

`design(request, context)` reconstructs all terrain, homes and plot labels using
named streams for home layout, offsets, outlines, pond sites, pond outlines, groves
and dry patches. Resource percentages are read only during furnishing. Validation
uses a fresh context, so reconstruction cannot advance generation's random streams.

Generation writes terrain, creates teams, places swarms/workers on home grass outside
crop plots, then plants resources and algae. The finished-world validator runs after
all those mutations. It checks dimensions, retained pure water at every pond, crop
containment, renewable gathering access (wheat within 24 steps, wood within 32),
reachable construction margins, forward-inn room, both approaches, a five-tile-wide
shared plains network and colony-to-colony land contact.

Optional ponds may be omitted when they cannot fit. **At least one neutral pond is
essential**; if none fits, the request fails. Optional grove candidates are skipped
when crowded. Resource targets saturate inside their plots. An unmet starter floor
fails generation instead of scattering emergency resources into the town.

There is deliberately no unrestricted crop top-up or route-clearing repair. Those
operations could break the containment contract or conceal a layout error. A failed
candidate remains disposable under `GenerationService`'s existing failure contract.
Error messages suggest a larger map or fewer colonies where geometry is the cause.

## Telemetry

All counts are observations of work already performed; no RNG draws or additional
map analysis are added when telemetry is enabled.

| Key | Meaning |
| --- | --- |
| `savannah.homes.jitter-accepted` | Site proposals satisfying the spacing constraint. |
| `savannah.homes.spacing` | Minimum wrapped home-centre distance, tiles. |
| `savannah.ponds.requested`, `.placed` | Neutral pond targets and accepted features. |
| `savannah.ponds.eligible-centres` | Integer centres clearing the initial home/approach reservations. |
| `savannah.ponds.omitted` | Actual omission due to exhausted reserved space. |
| `savannah.plains.clumps`, `.proposed-clumps` | Accepted and proposed optional grove/outcrop sites. |
| `savannah.plains.omitted-clumps` | Candidate clumps intersected existing reservations. |
| `savannah.plots.beach-trimmed-tiles` | Pre-beach plot tiles excluded from final crop eligibility. |
| `savannah.plot.requested`, `.planted` | Deposit tile targets and placements, subject = plot index. |
| `savannah.plot.saturated` | A plot exhausted eligible tiles before its target. |

Saturation at high abundance is expected, not a repair. Count omissions per attempted
map and inspect the final-map metrics alongside them; multiple plot records on one
map must not be treated as independent maps. Final reports also retain the complete
request and generator revision.

## Playtest limitations

The contained crop plots can expose an AI farming limitation: a colony can retain food potential yet harvest too little to sustain growth. Compare supply, actual meals and hauling before changing pond size; human farming and pacing need playtests.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[SavannahGenerator.cpp](../../src/map/generator/generators/SavannahGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
