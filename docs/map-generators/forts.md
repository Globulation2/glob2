# Forts

**Forts** (`forts`, numeric ID 35) is a medieval, central-European-inspired landscape:
granite enclosures open onto a bocage of hedged fields, wooded uplands, granite tors,
winding river valleys, irregular lakes, market-town sites and orchards. It is an imagined countryside, not a geographic
reconstruction of Europe.

Each colony starts in a rectangular fort with two opposite gates, a broad construction
courtyard, separate irrigated wheat and wood plots, and a small household orchard. A
shallow moat runs all the way round each fort, broken only where the gate roads cross it,
and narrows round any tower that stands out past the wall.

The map has one fixed look of its own (`kFortsLook`, not a shared climate), drawn from the
terrain catalogue since revision 9:

| Feature | Terrain (group) |
| --- | --- |
| Curtain walls, upland crowns | Granite tors (Obstacles: block units and arrows) |
| Bastions, towers, curtain either side of each gate | Ridge rock (Ridges: towers shoot over) |
| Bailey square, gate roads, market-town streets | Brick path (Paths) |
| Country lanes | Overgrown road (Paths) |
| Fords | Boardwalk (Paths) |
| Field boundaries | Hedge (Obstacles) |
| Plot beds / plot rims | Loam (Fertile) / dirt (Barren) |

Every fort on a map is built to one design, so the colonies start alike, and the design
changes from map to map. The design combines:

| Part | Choices |
| --- | --- |
| Interior layout | **Bailey** (yard on one side, wheat and wood plots side by side on the other); **diagonal** (wheat and wood in opposite corners, yard in the other two); **long gardens** (a long wheat garden down most of one wall, wood along half the opposite wall); **chapter** (wheat in the two corners of one wall, wood in the middle of the other) |
| Walls | **Bastions** (square corner bastions); **gatehouse** (bastions, a square tower either side of each gate and one in the middle of each long wall); **round towers** (at the corners and beside the gates) |
| Orientation | Gates east–west or north–south, and the layout mirrored across either axis |
| Market towns | **Crossroads** (four plots); **green** (a ring street round a small green, cut by one through street); **high street** (a main street with a back lane crossing it either side), turned either way |

![Eight Forts maps, one fort from each: layout, wall style and town plan](images/forts-designs.png)

Plots and towers stay a tile clear of the ramparts, so every turn and mirror of a design is
legal. A plot watered along its long side takes its water on the side toward the gate road.
Barren rims contain those plots, and the paved gate roads connect the colonies by land,
crossing rivers at fords. The opening supports settlement inside the fort; larger economies
and additional fruit collection encourage expansion into the countryside. Swimming adds river
crossings but does not bypass the walls.

Ramparts are permanent terrain walls drawn on vertices (`wallVertices`, three vertices thick),
leaning away from the gate roads. They cannot be mined or demolished. Before revision 9 they were
stone deposits, which made them the forts' quarry; now each bailey has two quarry faces of 12
stone at the foot of its side walls, at every stone setting. Towers are built by the players;
none are granted at generation. Paved gate roads can carry buildings. Defence comes from the
ridge-rock gate fronts and bastions, which a defender's towers fire over. This favours fortified
openings with two approaches rather than fully closed castles.

**The bocage.** The open country is divided into fields about 20 tiles across, each tile belonging
to its nearest jittered field centre. `hedgerows` percent of the field borders carry a hedge, and
35% of the hedged borders keep a five-tile field gate near their middle. Roads, water, forts, towns
and crowns are never hedged. A repair pass then cuts a field gate into any field that hedges,
crowns and water close off from the roads, so every field can be walked to. The uplands' highest
crags (about 2% of the open country, from the upland noise broken by a finer crag noise) are granite
tors. Quarry faces at their feet hold as much stone as the uplands' exposed rock used to, scaled by
the stone amount.

A broad Rhine-like river bends around the forts, with irregular lake basins and
tributaries. Roads favor valleys, reuse junctions and cross water at fords. Wooded
uplands and rocky crowns separate approaches. Market-town sites favor nearby ground on each fort’s side of the countryside,
with four protected building plots, crossroads and nearby fruit where space permits. They are reserved
before the river is routed, so water bends around them. Both gate destinations are
chosen together to limit total travel distance to distinct road hubs. These
are places for players to develop inns and outposts, not prebuilt neutral buildings.
The intended choice is whether to secure crossings and orchards early or consolidate
the fort first. Town sites may be omitted when a crowded map cannot fit them.

## Controls and limits

| Control | Default | Range and meaning |
| --- | --- | --- |
| Home size | 22 | 20–26, step 2; fort half-width in tiles, excluding projecting bastions |
| Gate width | 6 | 4–8, step 2; width of the paved gate road |
| River width | 9 | 3–13, step 2; nominal water-corner width, locally narrowed between forts and interrupted by fords |
| Lakes | 1 | 0–3 requested basins per fort; each targets roughly map area / (16 × colonies) water corners |
| Village size | 10 | 8–12, step 2; half-width of the market-town sites |
| Hedgerows | 70 | 0–100, step 10; percent of field borders hedged; 0 leaves the country open |
| Resource amounts | 100% | 0–300%, step 25; wheat, wood, stone, algae and fruit |

Every home retains at least 32 wheat and 32 wood tiles at zero abundance. Surplus
planting scales with the crop sliders and caps at the contained plots' capacity.
Each household orchard has three tiles of each fruit at 100%, scaling to zero or
nine at the slider extremes. The baileys' quarry faces remain at zero stone abundance; the
crowns' faces and scattered quarries scale. Ambient resources stay off the reserved roads and home buffer.

Shared sizes are 64, 128, 256 and 512 tiles on either axis, with 1–12 colonies and
1–8 starting workers. The actual envelope depends on spacing: each pair of lattice
sites must differ by at least `2 * home-size + 16` in one wrapped coordinate, and
the shorter map side must also meet that minimum. Unsupported combinations are
rejected explicitly. Examples at defaults: 64×64 supports one colony, 128×128
supports four, and 256×256 supports twelve. Odd counts and rectangles are supported
when the same spacing test passes. Increasing fort size can invalidate a crowded map.

Sites are dealt randomly to colony indices. Homes share a construction and crop
budget; surrounding terrain and routes are asymmetric. This is not an exact-symmetry
or competitive-balance guarantee. The map wraps: forts can cross the preview edge.
The smallest maps devote much of their area to forts; 256×256 gives the countryside
more prominence.

## Implementation and validation

`FortsGenerator.cpp` reconstructs its layout for validation. It uses shared lattice
sites, periodic noise, beach sketches, cheapest walking routes, the terrain-wall helpers
(`wallVertices`, `thickenRidges`, `paintClimate` with its own look, `paveCore`), quarry faces,
settlements, fertility fields, planting and building-footprint helpers. No shared primitive
changes. Existing save/replay/network formats and generator IDs remain unchanged.

Finished-world checks require every vertex as `sketchTerrain` designed it, resource-free roads,
no way out of a fort except its gateways, walking or swimming, with the gates shut
(`leakWithShut`), at least 64 clear 4×4
building anchors inside each fort, walking connectivity between colonies, and at least 16 clear 4×4 anchors per
placed market town.
These anchors overlap; they are not a count of independent buildings.

Telemetry records the design (`forts.design.layout`, `.walls`, `.town`, `.gates`), fort dimensions/counts, river orientation and fitted width, lake target/actual area and capacity limits,
town placement/omissions, plot target/actual
planting and summed growth potential, household orchard counts, plot-capacity limits, forest tiles, the
bocage (`forts.hedges.*`: fields, borders, hedged borders, tiles, tiles opened by the repair pass),
crowns placed, bailey and crown quarry tiles against their target, alongside shared
settlement and starting-resource observations. It makes no random draws or extra
map scans for collection.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
sim-diff map fingerprints, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[FortsGenerator.cpp](../../src/map/generator/generators/FortsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
