# Fjord continent

A frozen continent. Every colony holds a peninsula whose outer rim is green coastal meadow; inland
the ground turns to tundra, then snow, then the ice sheet at the heart of the continent. A fjord runs
between every pair of neighbours, open sea at its mouth and frozen from the ice line inward, between
glacier walls broken by a few landings. Ice is the front line: units can walk it, slowly, and it wears
them down, but nothing is built or grown on it. So every contact between colonies runs over a frozen
fjord or across the sheet, until a colony swims and can cross the open fjord mouths too.

Fjord continent 2.0 (revision 16) replaces the earlier temperate continent (central lake, sand ring,
lake-connected switch) and is drawn with the terrain catalogue in the polar climate
(`kPolarClimate` in `shared/Climates.h`). Glacier and icefall walls, penitente ridges, frozen-lake
fjords, a glacier-ice and snow sheet, tundra, arctic tundra, deep-snow drifts and pale beaches all
take their rules from the canonical terrains they are painted over.

## The ice and what it does
- Ice (the Ice terrain group) is walkable at half speed, costs ground units 8/256 HP a step, and
  holds no building and no crop. Routing prices it as a hazard, so units cross it but never idle on it.
- **The continent** is sized by `continent-size` on the map's shorter side, then scaled down if its
  widest wobble, stretched, would come within 6 tiles of the map's edge (the map wraps, so it would
  meet its own copy). The scale is recorded as `coast.fit`.
- **The sheet** is a disc of ice whose radius is 10% of the continent's at `lake-size` 0, rising to 28%
  at 90 (default 45, 19%). Its edge wobbles by up to 12%.
- **The rim**: penitente ridges (Ridges: units stop, towers shoot over) 3 tiles wide ring the sheet,
  broken only by each colony's own gap, about 8 tiles wide, on the colony's axis. Each gap runs 4 tiles
  past the rim so no fjord wall can close it.
- **The fjords**: one per pair of neighbours, an S-curve from just outside the coast to inside the
  sheet. The swing fades out three quarters of the way in, so every fjord meets the sheet straight on
  its axis, halfway between the two gaps. With two or three colonies the swing is capped at a
  four-colony map's, so a fjord never folds back on itself.
- **Ice reach** (`ice-reach`, 0-100, default 60): how much of every fjord is frozen, from the sheet
  outward. At 0 the fjords are open water as far as their landings reach (only the last stretch into
  the sheet is frozen), and the gaps are the only dry way onto the ice; at 100 they freeze up to the
  mouth's own stretch of open water. The ice line is ragged by a few percent.
- **Glacier walls**: every land vertex within 3 vertices of a fjord is cliff (Obstacles: they stop
  units and arrows), thickened so the glacier look draws. Walls near the head take the icefall look.
- **Landings** (`fjord-landings`, 1-4, default 2): gaps through each wall onto the fjord, the same
  number on both banks of every fjord at the same progress down it, spread over the frozen reach.
  Each landing is cut as the shortest tile path from the bank's own ground to the fjord's shore,
  widened by a tile, and kept only if a unit really walks through it. A bank with no ground of its own
  at the anchor (a bend can put the other bank on both sides) slides its landing along the fjord, up to
  15 tiles, to the nearest place it has; the slide is recorded as telemetry. A landing that finds
  no ground of its own anywhere refuses the design.
- **Spines**: the line from each home in to its gap stays clear of fjord walls and drifts, so where
  many colonies crowd the sheet, the walls of a colony's two fjords never meet across its root.

## Peninsulas and ground
- A colony's home is 11 tiles in from the coast on its axis, stepping inward past water.
- **Meadows**: grass only within 6 to 12 steps of the sea (ragged by noise) and within 7 tiles of a
  home. The open fjord mouths do not carry meadows inland: the inner continent is all ice themes.
- **Tundra**: everything else on a peninsula is barren ground (it builds, nothing grows), drawn as
  tundra just inland of the meadows and arctic tundra beyond.
- **Drifts**: deep snow (ice) on the tundra, more common the further inland. The drift threshold
  starts high past the meadows and falls with every step towards the sheet. Drifts never lie within 3
  of a wall, 5 of a door or 13 of a home, nor on a spine. A drift lies on its peninsula and counts as its ground.
- A colony's peninsula is the ground its units walk from home without touching fjord or sheet ice.
  Two homes on one peninsula refuse the design: there is never a dry route between colonies.

## Resources
- **Starter kit**: an 8-tile wheat patch and a 6-tile wood patch towards the sea, and a stone clump
  behind, the same way round every home.
- **Bank crops**: every bank's guaranteed 6-tile wheat and wood patches stand by the fjord's mouth
  in the meadows, sliding along the bank to fit. With `bank-deposits` on, rolled extras (wheat:wood:stone
  4:2:2, scaled by the amounts) stand along the mouth's stretch too.
- **Quarries**: each fjord bank has a 6-tile stone face at the foot of its wall near the mouth,
  inside the meadows (stone stands only on grass, and each bank keeps a small verge of grass there).
  It is laid before the crops, kept from the start repairs and restored at the end if the scatter
  took it. A colony still short gets one more face at the wall foot nearest its home. A lone colony
  quarries the rim either side of its gap.
- **Fruit grows only on the ice**: nunataks, rock islets standing out of the sheet, carry the map's
  only fruit. With `nunataks` ("Nunatak orchards") on and a sheet radius of at least 7 tiles, one
  stands between every pair of neighbours at 58% of the sheet's radius, no wider than 40% of the gap
  to the next, with 3 groves of one fruit (the fruits taken in turn) and a stone clump. A richer one
  stands in the middle of a sheet of radius 12 or more (or on a lone colony's sheet), with 2 groves
  of every fruit. The fruit amount scales the groves. Harvesting fruit means sending workers across
  ice. A very small sheet (low `lake-size` on a small map) has no nunataks, and so no fruit.
- **Algae**: one clump per 40 plain-water tiles 1-14 steps offshore at amount 100, on the
  best-growing 60% of that water. The open sea beyond has a deep-water core.
- **Islands**: `resource-islands` plus up to one more, each 8 tiles of water from any coast with one
  themed prize, for the first colony to swim.
- The ambient scatter (wheat 24, wood 12, stone 10, no fruit, per the usual map-area units) falls on
  grass: the meadows, the nunataks and the islands. Those densities were sized for a map about 45%
  grass, so they are scaled by the map's actual crop ground over 45% (about 0.13 at the defaults,
  recorded as `scatter.meadow-scale`); unscaled, the meadows filled solid and a colony could find no
  free ground beside its fields for an inn (Cortex starved on such a start).
- Landings, gaps and two tiles round them are kept clear of every deposit, and every colony keeps
  a walk off the ice from its workers to each of its doors (every landing and its gap), clearing
  only deposits and going round the quarry faces.

## Supported envelope
- The map's shorter side must be at least 128 tiles, and fit 32 tiles of it per colony: 4 colonies
  on 128 (a tight fit), 8 on 256, 16 on 512. The continent fills a rectangle as a stretched oval;
  the fjords' sideways swing, the bank crops and quarry faces and the fallback quarry search are
  measured against that stretch (revision 17), so on a long map a fjord still runs in towards the
  sheet rather than along the coast, and both its banks keep ground at the mouth.
- A seed whose design or finished world cannot keep these promises is refused, and the lobby rolls
  another candidate. Over 60 seeds: 256x256 with 4 or 8 colonies, 512x512 with 8 and 512x256 with 6
  never refused; 512x512 with 16 refused 1 in 6; 128x128 with 4 refused 1 in 5, mostly for quarry
  stone. Over 30 seeds on the 4:1 shapes (revision 17): 512x128 with 4 colonies refused 2,
  128x512 with 4 refused 1, 512x128 with 3 refused 3 and 128x512 with 2 none, for quarry stone or a
  landing that does not reach its fjord.

## Validation
`validateWorld` rebuilds the design and checks the finished world:
- The terrain matches the design, looks included, vertex for vertex.
- Every colony has the floor of crops and building room (`startingFloorFailure`).
- Colony 0 walks to every other colony with deposits blocking, which here means over ice.
- No colony walks off its peninsula without crossing ice (`leakWithShut` with every ice tile shut).
- Every landing opens: the owner's own ground reaches the fjord through it.
- Every landing is reached from its colony's workers off the ice, with deposits blocking.
- Every colony's walls carry at least 6 stone.

## Telemetry
Keys under `fjord-continent.`:
- Layout: `coast.rotation-radians`, `coast.elongation`, `coast.fit`, `sheet.radius`, and per fjord
  `fjords.mouth-width`, `fjords.wiggle-amplitude` and `fjords.ice-from`.
- Landings: `landings.per-bank`, and `landings.slide` per landing.
- Terrain counts: `terrain.ice-vertices`, `terrain.glacier-vertices`, `terrain.penitente-vertices`,
  `terrain.tundra-vertices`.
- Placement: `nunataks.placed`, `quarry.stone` per colony, `homes.retreat` per colony, `scatter.meadow-scale`,
  `outliers.requested` and `outliers.actual`.

## Implementation source

[FjordContinentGenerator.cpp](../../src/map/generator/generators/FjordContinentGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
