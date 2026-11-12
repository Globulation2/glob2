# Building with the terrain catalogue

The first generators were written when the engine had grass, sand and water only, so they borrowed
whatever behaved closest to the idea. A wall was the stone resource, a road or a no-build marker
was sand, and a coast was a beach with stone behind it. The terrain catalogue
([TerrainGroup.h](../../../../src/map/TerrainGroup.h),
[TerrainGroupTable.h](../../../../src/map/TerrainGroupTable.h)) now has real terrain for each of
those ideas. This guide records what the City states 2.0 rebuild and the arena family's (Carousel,
Switchbacks, Gauntlet, Amphitheatre) taught. Use it when you upgrade an older generator or design a
new one with the full palette. Its lessons are grounded in those five generators' code and in the
maintainer's review of their renders (2026-11-07); recheck the code before relying on a detail.

## Say what you mean with terrain

Pick terrain by the rule the design needs, then let a climate choose the look. The groups that
matter most:

| Idea | Group | Rules | Typical use |
| --- | --- | --- | --- |
| A wall that stops arrows | Obstacles (cliffs, hedge, granite tors, obsidian ridge, sandstone benches) | Impassable; blocks projectiles | Colony borders, sealed coasts, mountain mass, a citadel |
| A wall towers shoot over | Ridges (ridge rock, outcrop, hoodoos, petrified stumps) | Impassable; projectiles pass | Siege walls, causeway parapets, a mountain's leg walls, arena tiers |
| A road | Paths (road, brick path, slate ashlar, basalt causeway, boardwalk) | Twice as fast; buildable; nothing grows; holds no field back | Streets, towns, trails, decks, bridges |
| Ground crops never take | Barren (dirt, clay, shale, laterite, volcanic ash) | Buildable; inhibits growth like sand | Bases whose food grows elsewhere, farm caps, plot rings, scrub |
| No-build, still walkable | Rough (mudflat, hardpan, scree, pumice) | Slow; unbuildable; nothing grows | Ramps, talus, no-man's-land; not gates or lanes (below) |
| A barrier fliers fear | Lava | Impassable; burns fliers | A volcanic centrepiece or moat |
| Slow swimming water | Deep water | Slower swimming; no algae | Only where the design wants slower swimming (a lagoon core, an opt-in control); never every vertex some distance from land |
| Frozen ground | Ice (frozen lake, glacier ice, deep and wind-swept snow) | Walkable at half speed; wears ground units down (8/256 hp a step, about 2 hp over 30 tiles); unbuildable; nothing grows | A crossing with a price: Spider web's frozen cells |
| Sand | Sand | Unbuildable; inhibits growth; shoreline for algae | Only where the subject is sand: a desert, an arena floor, a real beach |

**Sand only where it is sand.** On the older maps sand stood in for roads, gates, no-build markers,
farm caps and plot rings. Revisit every use when you upgrade a generator (maintainer feedback,
2026-11-07: "we should only be using sand where it actually makes sense to express the terrain
concept"):

| Old use of sand | Use instead |
| --- | --- |
| A road | Paths |
| A farm cap or plot ring | Barren (`dressFarmSand`) |
| A farm's bridges and crossings | Paths (`dressFarmSand`) |
| A gate or lane that must stay unbuildable | Paths, plain from end to end. A rough strip in a paved gate or lane read as two terrains and slowed the road; the maintainer chose plain road and accepted that a gate can be built in (City states, Carousel, 2026-11-11) |
| A scrub patch | Barren, with a rough core on open ground |

**Beaches are no longer required.** Grass directly against water is legal. A generator using the
catalogue need not run `layBeaches`, and the five above no longer do. Lay a beach only when the
design wants one; a pale-sand beach where grass meets open water reads well, and the maintainer asked
for it back on Spider web (a grass-only beach pass there, so tundra and ice still meet the water
bare). Two consequences:
- **Algae.** Algae regrows only where sand lies at its probe point, so a coast without sand keeps
  whatever algae was seeded but grows no more.
- **Order.** `layBeaches` reacts only to `WATER` and turns every other land vertex beside it into
  sand, including paths, cliffs and lava. If you do keep it, run it before any catalogue terrain is
  drawn, and draw walls, decks and bridges after it.

## Design on mechanics, paint the climate last

Keep the design to one terrain per rule (`GRASS`, `SAND`, `CLIFFS`, `RIDGE_ROCK`, `TRAIL`, `DIRT`,
`MUD`, `LOAM`, `WATER`, `LAVA`). Measure, validate and plant on that sketch. Paint the looks as the
very last step with `paintClimate` ([Climates.h](../../../../src/map/generator/shared/Climates.h)):
- **The rules can't move.** A static_assert checks every climate's looks against their mechanic's
  group, so a look never changes a rule, and one tuning pass covers every climate.
- **Draw the climate inside `design`.** Use its own stream (`drawClimate(context, "<id>-climate")`) so
  the validator rebuilds the same one, and record it with `telemetry.choice`. `drawClimate` chooses
  among the first four (`kDrawnClimateCount`); a climate appended after them, such as polar
  (`kPolarClimate`, used by Fjord continent), is only ever taken by name, so appending one moves no
  existing map.
- **Painting has to know where it is.** It needs roles only the design knows: which grass is a farm
  or a home, which paving is a town, a street, a deck or a bridge, which water is deep, and which
  rock is a second tier. Pass them through `LookRoles`.
- **One look per rule group per map, no speckle.** Barren and fertile ground used to be mottled with
  a second look on a fixed vertex hash; across a whole home base the blotches read as polka dots
  (removed 2026-11-08). The natural generators then split looks by noise regions and distance bands
  instead, which read as stripes and patchwork (removed 2026-11-11). A role takes one look per map;
  a second look needs a named feature drawn as one region and the maintainer's agreement
  ([terrain palette](terrain-palette.md#one-look-per-rule-group-per-map)). Lava is drawn in
  concentric rings (ember rim, lava, molten core) by `paintClimate`.
- **Pick looks that contrast.** In the arid climate, sandstone roads beside sandstone-bench cliffs
  are hard to tell apart in a render.
- **Theme each climate, not just its colours.** The maintainer asked that every volcanic map show
  lava, at least at its centre. Each generator then used its own central water (a plaza pond, a
  plateau pond, a lake, a pit pool or the heart's lake). That changes rules (lava irrigates
  nothing), so check what the water was doing first.

## Walls on vertices

A tile is blocked once two of its four corners are, and a tile whose corners mix two rule profiles is
never buildable. A stone wall was one tile thick. Drawn naively on vertices, the same wall blocks a
row on each side, and every tile beside it becomes an unbuildable seam. Use
`wallVertices(tiles, &keepOpen, &strict)` from [Walls.h](../../../../src/map/generator/shared/Walls.h):

- **Lean away from open ground.** A wall tile beside open ground marks only the corners it shares
  with no open tile, so lanes, doors, courts and homes keep every tile they were drawn with.
  Elsewhere it marks its top-left corner, plus one more wherever two wall tiles meet only diagonally,
  so there is no diagonal gap.
- **Where a wall turns round a lane, leaning can leave a tile one corner.** The fallback gives such a
  tile back corners, choosing each time the one that blocks the fewest open tiles. Pass the gate
  tiles as `strict` so it never closes them. Making the fallback merely refuse door corners left the
  ring's end open beside every door, and the doors then leaked round their own edges.
- **Classify walls by distance, not by touch.** Carousel's court wall is two tiles thick. Making only
  the layer that touched the court Ridges left a layer of cliff behind it, which stopped the siege
  towers' arrows: they fired 5–23 shots a game against 50–300 on the stone walls. The fix was every
  wall tile within the wall's thickness plus one of a lane.
- **Width shows.** Leaning halves a wall's visible thickness. A two-tile wall of Ridges kept no pure
  rock tile, rendered none of its spires, and did not read as a wall. Switchbacks' leg walls became
  four tiles by default, while the walls at the trail's ends stayed at two so towers at home and on
  the plateau still reach the trail. Then thin a crowded map's walls a tile at a time across the
  whole geometry (`geometryWith`), not just where legs are counted: wall thickness also sets the
  plateau's size and the homes' reach.
- **Thicken every wall to three vertices, cliffs too.** Ridges looks (ridge rock, outcrop, hoodoos,
  termite mounds, petrified stumps) draw peaks only on a tile whose four corners are all rock, and an
  Obstacles mass (cliffs, glacier, obsidian) draws its rim and top the same way. A curved or diagonal
  band two vertices wide has almost no such tile and read as bare ground (maintainer, 2026-11-08).
  Call `thickenRidges(terrain, t, canGrow)` once the walls are laid: every rock vertex then sits in a
  full 3x3 block, grown only where `canGrow` allows (keep lanes, doors and farms out); pass `CLIFFS` as
  the last argument for Obstacles walls. Carousel's siege walls and Switchbacks' borders, two
  vertices thick between a lane or a farm, drew as a string of blobs that looked passable
  (2026-11-11). Where a wall has nowhere else to grow, let it take a farm's edge (its grass and cap
  ring, a siege wall even its crossings) and open water, never its water rows; cliffs that took farm
  crossings closed farms' entrances on 6- and 8-colony maps. Measure the effect on a terrain dump:
  count tiles that are wholly one rock (`map study --report terrain`, code 10 + terrain id) before
  and after. Anything that finds walls by their design tiles must then look a step or two further:
  Amphitheatre's towers stand against blocked tiles within two steps of a ring, not the ring tiles
  themselves.
- **Keep paving off a wall's verge.** A thicker wall brings a home's paved square (`paveCore`) within
  reach of the wall's foot; the towers' stone step (`openTowerNiches`) then repaints that paving as
  grass after the terrain is written, and the validator reports the ground "not as designed".
  Pave only beyond the verge (Carousel, Switchbacks).
- **Never cut one wall through another.** Gauntlet's partitions were laid across the ring wall and
  the coast band, and won the shared vertices, so each cut a notch of different rock through a
  continuous cliff that read as a hole. Let the continuous wall win and stop the other at its foot.
- **Seal coasts with cliffs, not stone.** `cliffCoast` turns every wallable vertex within two of the
  sea into cliff, water included, so there is no beach to land on and no shore to swim from. That
  replaces the stone ring with its dead-end beach lane.
  - Make the home's own water wallable too: a Riverside creek mouth that a bay brought near the sea
    let swimmers in.
  - The coast reacts only to the water you call sea, so a lake or a farm's water rows stay as they
    are.

## A wall was also a quarry

A stone wall was every colony's quarry, often its only one, and a home kit commonly planted no stone
"since the home is walled in it". Remove the stone and the towers starve of ammunition:
- **Stone.** City states' tower-building AI mined 30–55% less stone and fired less until each home
  got its stone back.
- **Quarry faces stand at the foot, never in the rock.** Use `plantQuarryFaces` (Planting.h) to put
  stone at the foot of a terrain wall, where it blocks no way through. Seed the faces in the home's
  own frame so every home gets the same. Never carve a quarry into a ridge: Rain shadow's quarries
  were grass notches cut through the rock band, which read as holes punched in the mountain; since
  revision 9 they are the open ground within two steps of the rock (`quarryNear` with the open
  ground as the wall and the rock as the open side).
- **Check the count.** Telemetry counts placed tiles. Carousel's faces were seeded on the homes'
  outer side, which opens onto their own farms with no wall, and all of them placed 0. Search a
  home's actual wall feet instead, and keep the faces away from where its towers stand.
- **Barren ground holds no stone or crops.** A barren base needs a grass verge at the foot of its
  walls for its faces. Make the verge at least four vertices wide: two left a single pure row before
  the grass-barren seam, no 2×2 tower footprint fitted against any wall, and every Carousel and
  Amphitheatre map failed its tower placement.
- **Walls are land.** Helpers that judge a strip narrow by walkable land (`roomyGround`) must count a
  wall as land, or every tile beside a cliff looks like a shore strip and no tower fits.

## Themed bases

The maintainer's main complaint about the first renders was "too much plain grass" in player bases.
The first answer, barren bases, overshot.
- **Homes are not barren.** City states and Amphitheatre covered every home in barren ground beyond
  ten steps of its own water (`barrenBase`). Barren holds no crop and holds back the fields beside
  it, so the wings went dead and the home kinds stopped reading (maintainer, 2026-11-11: "almost all
  of the variants for a player's home base are paved over end to end... I wanted the paved section
  to be in the center, and for the wings to have farmable areas for wheat and wood"). Draw a home as
  grass with a paved centre (`paveCore`), give its wings water of their own (a pond each side) and
  seed fields there, and keep scrub off the town district: a scrub patch beside the swarm leaves an
  unbuildable seam that pushes the swarm off its anchor.
- **Bases whose food grows on farms** (Carousel, Switchbacks, Gauntlet) are still the climate's
  barren ground with a paved square round the swarm: their farms are dedicated, walled areas.
- **A themed fertile home** can read well: Maze's keeps are spring meadow throughout.
- **Removing a feature can remove building room.** Maze's paved temple courts were the one ground
  nothing grew on; dropping them left a colony with three 4x4 sites within 24 steps where the
  property test promises sixteen. The cell outside every keep's door is now a forecourt of plain
  grass with no loam beds or ambient fields. Run the map-generator test tier, not just the sweep,
  after removing anything buildable.
- **Farms keep their grass,** in the climate's farm look (`farmGrass`). Fertile soils would change
  the rules: they irrigate.
- **Barren has a cost.** It holds back crops behind it as sand did. Paths do not, so paving a town
  or a road instead of sanding it lets the fields beside it grow better. City states' paved towns
  raised the AIs' wheat by about 20% in paired games. Call this out as a change of pace.
- **One paving per map,** a bridge excepted: Maze's plazas and keep courts now share its road look.
- **Lava is drawn in rings,** ember rim, lava and a round molten core (`paintClimate`), never
  mottled; Lava shield is the model.

## Validate the terrain you designed

- **Make the terrain a pure function of the design.** Write a `sketchTerrain(const Layout &)` (climate
  included) and compare the finished map with it vertex for vertex. That replaces every "designed
  stone present" check.
- **Check by group where you can't rebuild.** Where the sketch takes in things laid at generation
  time (Gauntlet's farms), check the wall vertices by group with `wallTerrainMismatch`, and
  resolve vertices two masks share (Ridges wins) in the mask itself.
- **Seal checks must include swimmers.** `leakWithShut(units, shut, swim, inside)` floods the terrain
  a unit can walk or swim, counting deposits and buildings as cleared, with the gates shut. Name the
  first tile it reaches outside: that is where the seal leaks.
- **Paint every corner of a ford or bridge.** A ford tile with one land corner swims. Revision 12 of
  City states failed validation on some seeds for exactly this.
- **Compare refusals with the previous build.** Keep a copy of the pre-change binary and diff
  `sweep_shapes.py` against it after every change; a new refusal is a regression even where the
  message looks unrelated. Make refusal messages say where and how much (each colony's walk, the
  broken tile), which turns a refusal into a diagnosis.
- **"Not as designed" can come from a later stage.** When generation's written terrain and the
  validator's rebuild disagree, bisect by checking the map against the sketch after each stage.
- **Check every image of a mirrored placement.** City states placed its islet mirror images without
  checking them against each other, and two overlapped on rectangles.

## Working practice

- **A shared extraction must not move the old maps.** Before extracting shared code from one
  generator for a second, record its rows with `MapGeneratorGoldenTest <profile> --print
  --only=<id>`. That takes seconds for one generator; the full table takes half an hour on a debug
  build. Then check the extraction leaves them byte for byte.
- **Iterate on the perf build.** `python3 tools/dev_build.py perf=1 map-generator-golden-test`
  rebuilds one changed generator in seconds, with byte-identical maps; quote performance only
  from the perf tier.
- **A render helper must clear its old output.** A failed `map generate` leaves yesterday's PNG in
  place; delete the outputs first or check the exit status, or a broken map looks unchanged.
- **Find the change by diffing renders.** Difference two renders of one map, block-average it and
  crop the busiest block: faster than hunting by eye for where a wall or quarry moved.
- **Look at the real game view.** `glob2 map render MAP --output x.png --render-max-pixels 4096`
  draws it at 16 pixels a tile. Minimap previews cannot show whether a wall reads, a climate looks
  right or decor renders.
- **Render every climate.** Find a seed per climate by reading the climate's telemetry choice, and
  show each generator in every climate side by side.
- **Never stash in a shared checkout.** Another session may have uncommitted work there, and
  `git stash` takes it too. Build a baseline in a separate worktree
  (`git worktree add --detach DIR master`).
- **Fix the AI, not the map.** When AIs play badly on new terrain, the maintainer's instruction is to
  fix the AI rather than bend the map to it.
- **Expect noise.** Single paired games swing widely: Cabino's tower shots on the same maps moved
  from 35 to 58 between two game seeds. Compare the same seeds across revisions before reading a
  difference as a regression.
