# Hills

## Play contract

Every colony owns a hill: a paved summit town inside a rock crown, ringed by concentric
crop and water contours, lobed like a real hill's rather than drawn with compasses, and
closed by a rock foot with scree below it. Revision 9 (Hills 2.0) draws all of this with
the terrain catalogue in one fixed look; revision 8 drew it in grass, sand and water.

- **The stairs are the gates.** The crown and the foot are Ridges walls, drawn on
  vertices and thickened to three so they render as rock. Towers shoot over them,
  units cannot cross them, and no swimmer gets round them. Each wall opens only
  where a stair passes, one vertex wider than the stair either side. With every
  stair shut, a colony can leave neither its summit nor its hill; the validator
  checks both against walkers and swimmers.
- **Stairs, the summit square and the valley roads are one stone paving.**
  - Paths are fast and buildable, and nothing grows on or across them.
  - Each stair continues as a road across the valley, a little past the edge of its
    hill's ground.
  - Clear of the hill's foot, the road meanders up to three tiles either side of
    its line.
  - Where a road crosses a stream, the ford is boardwalk.
- **Scree** (Rough) lies one to three tiles out from every hill's foot. It is slow and
  holds nothing, but it doesn't hold back the fields beside it. The outcrops keep a grass
  foot all round, where stone above the default amount grows beside the face.
- **Streams run along the hollows between hills.**
  - They follow the boundary between two hills' ground, sampled through a smooth
    warp of up to twelve tiles so they wind.
  - Each is two to five tiles wide, and they stay six tiles off every foot.
  - The streams are dry within eight tiles of the saddle between every two
    neighbouring hills, so walkers always cross dry-shod there. The warp fades
    towards each saddle, so the dry crossing stays where the design put it.
  - Fragments under 120 vertices are dropped rather than left as puddles.
- **Meadows** of fertile ground (spring meadow) lie four to eight tiles either side of
  the streams, kept four tiles off any rock. They are the valleys' contested
  farmland: they water crops and wood around them.
  - They start stocked: at the default amounts 35% of their tiles hold wheat and 15%
    hold wood.
  - The crops lie in clumps (noise cells 8 tiles across) and in stretches of one crop
    or the other (cells 20 across).
  - The wheat and wood amounts scale them.
- **Outcrops.** Every hill's valley has two rock outcrops, each at its most open ground
  left and at least 24 tiles from the other. Each has a three-tile quarry face looking
  back at its hill. These replace revision 8's scattered commons stone; one outcrop
  per hill held only half of it.
- **The summit** has radius 16, with a 4.5-tile paved square round its middle.
  - A two-tile quarry face sits at the crown's inner foot, midway between the first
    two stairs, in the same place in every summit's frame.
  - A small starter inn holds ten wheat for the first feeding cycle; ongoing meals
    come from the terraces.
  - One starting tower per stair covers its upper approach. Its magazine and finite
    stone reserve start full, leaving the opening workers free to establish food.
- **These are drawn contours,** not engine elevation: the whole climb is longer than a
  summit tower's range.
- **Wild crops.** Between every two stairs, seven tiles beyond the outer cap's furthest
  lobe, a small wild patch of four crop tiles alternates wheat and wood. The patches
  spread into the commons over a long game, and the amounts scale them to nothing.
- **Vacant hills** (`extra-hills`) are walled farms with a town to take through their
  stairs.

The look is plain grass everywhere (so every grass rule reads one way), ridge-rock crowns,
feet and outcrops, slate ashlar paving, boardwalk fords, scree and spring meadow. There is
no sand: Hills lays no beaches, so algae seeded in the water keeps but does not regrow.

**Feel changes from revision 8** (call them out when comparing):
- **Gated hills.** Hills are now sealed, so attacks and expansion go through the stairs.
- **More food overall.**
  - Meadows and the end of sand inhibition raise each colony's growth potential
    within 48 steps of its swarm by about half.
  - Food within 24 steps falls by about a tenth, because the near crop row is reached
    through the stairs rather than across a walkable cap.
- **Stone comes from quarry faces.** The commons' stone is now outcrop faces.

## Controls and fitting

- **Unoccupied hills:** 0–4 additional hills beyond the colony count.
- **Hill radius:** 44–100, a maximum budget; only complete bands are fitted.
- **Contour band width:** 80–120% of `bestFarmRows(0)` (10 crop / 8 water corners).
- **Stairs per hill:** 2–4, each five corners wide.
- **Starting tower level:** 0–3; zero omits the towers.
- **Valley river:** on/off. Turns the streams, their fords and their meadows on or
  off. A single hill has no streams.
- **Resource amounts:** all five ambient layers scale.
  - The outcrops are structural; only the stone stocked on their faces scales.
  - Each occupied hill keeps, even at zero, a small inner-terrace wheat patch beside
    every stair, one wood patch and its summit quarry face.
  - The starter inn and tower reserves are also finite guarantees.

The supported envelope is geometric: the nearest pair of lattice hills and each
map side must fit two hills plus 14 corners of valley. A hill needs a radius-16
summit, two caps, and at least one complete band. Requests outside this envelope
are rejected before world mutation. Rectangles and odd colony counts use the same
fit rule. Starts are randomly dealt; this is comparable spacing, not exact symmetry.
At standard width, a band needs radius 38; two bands need radius 56. At 256×256,
four colonies normally fit two bands, whereas dense layouts fit fewer. A 64×64
map cannot fit the contract. Increasing the radius control only changes terrain
when an additional complete band fits inside the available spacing.

## Implementation and invariants

**Two sketches.** `design()` draws the mechanics with one canonical terrain per rule
(`GRASS`, `WATER`, `RIDGE_ROCK`, `TRAIL`, `MUD`, `LOAM`). It then paints the fixed
`kHillsLook` with `paintClimate`, so a look never changes a rule. `generate()` writes
the finished sketch before any colony is settled, and no later stage repaints terrain.

**Reused pieces:**
- **Farm geometry.** The lattice, the contour farm (`layContourFarm` with
  `ContourWobble`) and the seed streams are revision 8's, so the hills' layout is
  unchanged. The farm's caps become the walls and its crossings the stairs.
- **Walls.** They are thickened with `thickenRidges`. The crown grows inwards over the
  town's outermost two tiles, never over the first crop row. The foot grows outwards
  into the valley. Neither grows within a vertex of paving or water.
- **Outcrops.** Each is `rockVertices` over a lobed knot, with a face from `quarryNear`
  kept in plain grass by `quarryApproach`. The summit quarry is `plantQuarryFaces`.
- **The rest.** Colony placement, the stocked inn and towers (`placeTower` with
  mouth coverage), the masked crop rescue and the building-room helpers are unchanged
  from revision 8.

**Seed streams.** New layers draw from their own streams (`hills-streams`, `hills-roads`,
`hills-outcrops`, `hills-talus`, `hills-meadow`, `hills-meadow-crops`), so revision 8's `rice-*` draws are
unchanged.

**Validation** rebuilds the design and checks:
- **Terrain.** The finished terrain against the design, vertex for vertex. Every
  crown, foot and outcrop vertex is Ridges.
- **Stairs.** Every stair's paved core walks, and each colony can reach the full
  length of every stair of its own hill.
- **Seals.** Each colony's crown and foot hold with that wall's gates shut, against
  walkers and swimmers (`leakWithShut`).
- **Summit.** It keeps 100 building anchors. No terrace grass component reaches it,
  and no crop repair enters it.
- **The valley.** No rock knot parts the walkable ground (`partingBarrier`), and the
  colonies can walk to each other.

**Telemetry** (`hills.*`) records:
- the fitted radii, band widths and counts;
- stair angle and count;
- unthickened wall vertices;
- stream, ford, meadow and scree corners;
- meadow tiles and the wheat and wood planted on them;
- outcrops placed and their stone;
- the summit quarry per colony;
- crop budget saturation.

## Reproduction

```sh
scons release=1 server=0 -j6 engine-tests map-generator-golden-test build/src/glob2
build/src/glob2 map generate hills --seed 7 --width 256 --height 256 --teams 4 --output artifacts/hills/seed-7.map --preview artifacts/hills/seed-7.png --report-file artifacts/hills/seed-7.json
python3 tools/map_telemetry.py collect --generators hills --seed-start 20001 --count 8 --set width=256 --set height=256 --set teams=4 --jobs 2 --output-dir artifacts/hills/held-out
python3 tools/map_telemetry.py summarize artifacts/hills/held-out
```

Keep verification results, previews, native maps, replays and study evidence in
the ignored `artifacts/hills/` workspace or in pull-request attachments.
The primitive regressions cover translated/wrapped contours, diagonal crossing cores,
future growth components, and nearest-site tie/sentinel behavior. The generator
regression includes resource extremes, rectangles, vacant hills and 12,000 unattended
resource-growth calls.

Static connectivity, generation repeatability and AI survival are separate checks;
human play is still needed to assess stair defense strength and valley incentives.

## Playtest limitations

Check retained crossings, town room and renewable resources on both rectangles and crowded requests. Generation success alone does not verify usable late routes.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
sim-diff map fingerprints, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[HillsGenerator.cpp](../../src/map/generator/generators/HillsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
