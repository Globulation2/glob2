# Savannah

Golden grass to the horizon, red earth, granite kopjes and watering holes that game trails lead
to. Each colony has a contained home farm and a town to the west; watering holes between
neighbouring homes are the local frontiers. The open plain is tall grass and scrub that can be
walked through but must be cleared before building, so expansion costs work.

Generated maps use catalogue terrain (barren, paths, ridges, rough, deep water) and landscape
resources (tall grass, scrub, dead trees, maize, rice, potatoes, fish, camp-sites), so they carry
those experiments in their header like any map that contains them.

## On this page

- [Visual signatures](#visual-signatures)
- [Controls and supported requests](#controls-and-supported-requests)
- [Geometry budget and numerical choices](#geometry-budget-and-numerical-choices)
- [Reusable framework operations](#reusable-framework-operations)
- [Stages, error handling and fallbacks](#stages-error-handling-and-fallbacks)
- [Telemetry](#telemetry)
- [Playtest limitations](#playtest-limitations)
- [Verification](#verification)
- [Implementation source](#implementation-source)

## Visual signatures

| Signature | Construction |
| --- | --- |
| Golden grass with lone and dead trees | Tall grass over the dry plain in noise-ranked sweeps, thinning to scrub; trees, small clumps and dead trees dotted on it |
| Red earth | Every crop plot is sealed in dirt and clay instead of sand; dry patches become clay pans with gravel |
| Kopjes | A ridge-rock core with stone on its flanks, skirted by scree |
| Watering holes and game trails | Larger neutral ponds with a deep centre, a mud ring (maize holes) or sand bank (rice holes), fish in the shallows; dirt tracks from each home's approaches to the nearest hole |

## Controls and supported requests

| Control | Values | Default | Meaning |
| --- | --- | --- | --- |
| Watering holes | Sparse / Normal / Many | Normal | Targets 2 / 3 / 4 **neutral** watering holes per 128×128 area, in addition to home ponds. |
| Dry patches | 0–20%, step 1 | 8% | Share of eligible plains vertices turned into clay pans, outside home, approach and feature reservations. This excludes plot seals and beaches. |
| Grassland | 0–100%, step 5 | 70% | Share of the dry plain under tall grass; a further eighth of it, thinned, is scrub. |
| Kopjes | Few / Normal / Many | Normal | Share of plain features that are kopjes: 15 / 25 / 40%. |
| Game trails | On / off | On | Dirt tracks along each approach and on to the nearest watering hole. |
| Wheat, wood, stone, algae, fruit, fish, camp-site amounts | 0–300%, step 25 | 100% | Additional deposits; exact tile counts saturate when eligible plots fill. Wheat also scales maize, rice and potatoes. |

Sides are existing power-of-two sizes **128, 256, 512**, square or rectangles up to
**2:1**. At least **4,096 tiles per colony** and **48 tiles between home centres**
are required. The engine currently supports at most 16 colonies. Requests failing either actual spacing or the essential-feature fit
are rejected with an actionable error. Homes never shrink to accept crowding.
The generic controls retain their existing ranges for other generators.

Zero abundance retains each colony's pond, **20 renewable wheat tiles, 20 renewable
wood tiles and four stone tiles**. All other resources respond to their controls.
Ponds, plot outlines and earth seals remain at zero. Algae and fish have no starter floor.
Tall grass and scrub are terrain, set by Grassland, not an amount.

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
  has 20 guaranteed plus eight scaled. Long games exposed food pressure at the first
  calibration's radius-four plot, prompting the 40-tile wheat start; later four-colony
  games reduced 236 global initial wheat tiles to 66/93 at tick 24,000, so the plots
  carry unplanted growth room beyond those initial counts. Both plots keep their western
  seal edges where they were: a rejected tune that moved the wheat cap toward town made
  Numbi place no food inn and harvest zero wheat. Preserving nearby food-inn space is
  therefore a play constraint, even when static gathering paths say more wheat is
  reachable. Both plots fit the radius-23 reservation and leave the western approaches
  clear. The radius-two quarry sits 18 tiles east.
- **Seals:** every plot is ringed by two corner rows of dirt (stretches of it clay, on
  noise). Barren earth stops growth and inhibits the water probe exactly as sand did,
  so the containment proof is unchanged, but a pure earth tile can carry a building.
  Kopje plots are sealed in scree instead.
- **Algae:** one radius-one clump per 30 eligible pure-water tiles at 100%,
  scaled by abundance using the existing fertility-aware `seedAlgae` operation.
  It never creates water or changes land connectivity.
- **Watering holes:** radius 6.5, 25% ripple, with vertices three or more steps inside
  the shallows turned to deep water (no algae or fish grow there; a tile mixing shallow
  and deep corners is neither walkable nor swimmable, which only matters inside the pond).
  Crop plots radius five lie 11.5 tiles either side of the centre: one holds maize or rice
  (drawn per hole, 38 tiles at 100%, scaled by the wheat amount), the other wood (28).
  Three radius-two fruit plots lie on the local east side, 12 tiles out and seven apart,
  two deposits of each kind at 100%. The local west stays clear for a forward inn.
  Each hole receives a separately seeded heading (3,600 possible).
- **Hole banks:** a maize hole is ringed by trampled mud (rough: slow, unbuildable) from
  half a tile to three tiles beyond its water; a rice hole by a sand bank three vertices
  wide, which holds the pure sand tiles beyond the water that rice needs to regrow (a
  shore crop regrows where water lies at one probe offset and pure sand at twice it). Both
  avoid every plot corner and leave the inn side clear (±0.9 and ±0.6 radians).
- **Fish:** four per hole at 100%, on pure shallow water tiles beside walkable shore.
  Fish spread only through water. As an aquatic food they make water food habitat,
  which changes how the AIs read fertility.
- **Neutral placement:** enumerate every integer centre whose shared Euclidean
  distance transform clears the home and approach reservations. Shuffle eligible
  centres, then stable-sort by summed squared distance to the two closest homes
  (one home for solo maps). This favours neighbouring frontiers without targeting
  the map centre. An earlier budget of 1,600 darts per 128-square area missed a narrow
  legal gap at seed 1001 on a 128×128 four-colony map; exhaustive eligibility removes
  that sampling failure without shrinking homes or rerolling the seed. A radius-19
  reservation plus a one-tile gap must fit clear of every home, approach and earlier hole.
- **Game trails:** each approach carries a dirt track (fast, buildable, nothing grows)
  from 18 tiles out to its end, and a trail runs on from the end to the rim of the
  nearest watering hole, bowed up to four tiles. Track is laid only on grass corners of
  no plot, never inside any home reservation but its own approach, and never inside a
  hole's disc, which is its inn clearing and fields; a trail crossing water or a plot
  has a gap there. Trails are reserved three tiles wide so plain features avoid them.
  Track doubles walking speed along it, which changes pacing between homes and holes.
- **Dried pools:** shared dart throwing with spacing 40 proposes clay pans of radius
  five with a mud bottom on the open plain; a 9-tile clearance box must be free, and
  radius eight is reserved. They hold no water: the earlier revision's pools watered a
  31-tile square each and left the plain too fertile for grass that must not spread.
- **Plain features:** shared dart throwing with spacing 22 proposes features; an 8-tile
  clearance box must be free, and radius seven is reserved. Each is drawn as a kopje
  (the Kopjes share), a potato patch (15%) or a grove. A grove is radius three, seven
  wood at 100%, finite when dry. A potato patch is radius 3.5, eight potatoes at 100%:
  potatoes regrow without water, so the sealed patch is renewable food on the dry plain.
  A kopje is a radius-four stone plot sealed in scree with a radius-2.2 ridge-rock and
  outcrop core over its centre: four stone at 100% on its flanks, impassable rock that
  towers shoot over.
- **Lone trees and camp-sites:** shared dart throwing with spacing 12 proposes tree
  sites outside every reservation and plot; each is a tree, a clump of two or three, or
  (every fourth) a dead tree, scaled by the wood amount. Spacing 40 proposes camp-sites,
  half of them used at 100%, each in a two-tile clearing the grass leaves open. Only dry
  pure grass (crop growth chance zero) takes either.
- **Grassland:** the dry plain outside every reservation, plot and camp clearing, more
  than zero to four tiles (on noise) from watered ground so its edge never draws the
  growth probe's square, ranked by period-11 noise. The Grassland share takes tall grass
  and the next eighth thins to scrub. Both are land-ecology deposits on ground with no
  growth chance, so they never spread; both can be walked through but block building
  until cleared.
- **Dry patches:** periodic noise with cell period 14 makes coherent patches instead
  of speckles. `sprinkleSand` ranks eligible vertices and keeps three steps from water;
  the patches then become clay with gravel. Beaches are laid after all terrain
  operations, and deep water after them. Plot eligibility is then trimmed to final
  pure-grass tiles; beaches and kopje cores may remove plot tiles, but starter floors
  are never reduced.

## Reusable framework operations

- `Orbits::jitterSites`: bounded integer proposals on a torus, accepted only while
  preserving caller-supplied minimum spacing. Returns the accepted count.
- `Farmland::stampContainedPlot`: arbitrary grass-corner sets surrounded by a
  two-corner-wide square-stencil margin of sand or any terrain nothing grows on.
  Returns the actual pure-grass tile list, including across map seams. The caller
  reserves enough space first; stamping deliberately overwrites its footprint.
- `Farmland::plantContainedPlot`: deterministic fertility ranking within an explicit
  tile list, optional renewable-only eligibility, a hard count ceiling, and an
  actual planted count.
- `Farmland::containedPlotsMismatch`: checks final grass adjacency against plot labels
  and rejects any land-spreading deposit planted outside those labels (wheat, trees and
  catalogue crops and grasses), except a non-farmable land-ecology deposit (a tree, tall
  grass, scrub) on ground whose crop growth chance is zero. Uniform and shore crops
  (potatoes, rice) are never allowed outside a plot, since they regrow without the water
  probe. Any eight-neighbour growable connection out of a plot or into another fails.
- `Grid::walkableTiles`: deposits that do not block ground units (tall grass, scrub) are
  walkable, as in `stepCost` and the engine; only deposits that block ground close a tile.

The engine expands crops only onto neighbouring ground that allows growth, and neither
earth, scree, mud, track nor sand does. The final adjacency check therefore proves that
these crops cannot spread out of their plots under ordinary resource growth, for any
number of ticks. This proof does not cover an editor changing terrain or a player
deliberately planting resources outside the designed plots.

## Stages, error handling and fallbacks

`design(request, context)` reconstructs all terrain, homes and plot labels using
named streams for home layout, offsets, outlines, pond sites, headings, hole crops,
trail bends, pool sites, feature kinds, kopje cores, camps and dry patches. Resource percentages are read only during furnishing. Validation
uses a fresh context, so reconstruction cannot advance generation's random streams.

Generation resolves the landscape resources from the map's registry (failing if the
catalogue lacks them), writes terrain, creates teams, places swarms/workers on home grass
outside crop plots, then plants plots, camp-sites, lone trees, grassland, fish and algae. The finished-world validator runs after
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
| `savannah.holes.rice` | Watering holes drawn as rice holes; the rest are maize holes. |
| `savannah.holes.sand-bank-vertices`, `.deep-vertices` | Sand-bank and deep-water vertices laid at watering holes. |
| `savannah.holes.fish` | Fish deposits placed. |
| `savannah.mud-vertices` | Mud vertices at holes and dried pools together. |
| `savannah.trails.vertices` | Track vertices laid (repeats count once per pass). |
| `savannah.pools.placed`, `.proposed` | Dried pools accepted and proposed. |
| `savannah.plains.clumps`, `.proposed-clumps` | Accepted and proposed plain features. |
| `savannah.plains.groves`, `.kopjes`, `.potato-patches` | Accepted features of each kind. |
| `savannah.plains.omitted-clumps` | Candidate features intersected existing reservations. |
| `savannah.plains.tree-candidates`, `.tree-clumps`, `.trees`, `.dead-trees` | Lone tree sites and what was planted. |
| `savannah.plains.camp-candidates`, `.camps` | Camp-site candidates and camp-sites placed. |
| `savannah.plains.dry-tiles`, `.tall-grass`, `.scrub` | Dry plain eligible for grass, and tall grass and scrub laid. |
| `savannah.plots.beach-trimmed-tiles` | Pre-beach plot tiles excluded from final crop eligibility. |
| `savannah.plot.requested`, `.planted` | Deposit tile targets and placements, subject = plot index. |
| `savannah.plot.saturated` | A plot exhausted eligible tiles before its target. |

Saturation at high abundance is expected, not a repair. Count omissions per attempted
map and inspect the final-map metrics alongside them; multiple plot records on one
map must not be treated as independent maps. Final reports also retain the complete
request and generator revision.

## Playtest limitations

The contained crop plots can expose an AI farming limitation: a colony can retain food potential yet harvest too little to sustain growth. Compare supply, actual meals and hauling before changing pond size; human farming and pacing need playtests.

Revision 4 has not yet been tuned in AI games. Open questions for that pass: whether the
AIs clear tall grass to expand or stall at its edge; whether they farm maize, rice,
potatoes and fish; how trail speed changes first contact; and whether rice holes regrow
as well as maize holes.

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
