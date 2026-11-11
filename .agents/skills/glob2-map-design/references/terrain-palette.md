# Designing with the terrain palette

Read this before drawing anything other than grass, sand and water: rock walls, deep water,
pavement, mud, paths or fertile ground. It records what thirteen natural generators learned when
they were rebuilt on the engine's terrain groups (Bajada, Braided river, Caravanserai, Crater lakes,
Everglades, Portage Lakes, Rain shadow, Ring world, River, Shattered coast, Swamp, Tidal flats,
Watershed). **Engine rules** are marked as such and come from
[TerrainGroup.h](../../../../src/map/TerrainGroup.h) (one property profile per group) and
[TerrainGroupTable.h](../../../../src/map/TerrainGroupTable.h) (which type belongs to which group);
the rest is design advice backed by the named precedent, whose header and doc carry the numbers.
How corners combine into a tile's rules is in
[gameplay and playability](gameplay-and-playability.md#terrain-is-a-rules-surface).

## A group is a mechanic, a type is a look

**Engine rule.** Every type in a group has a byte-identical property profile; types inside a group
differ only in artwork. Corners of two looks of one group combine into that group's rules, so
lush grass beside dry grass is still buildable grass and water beside dark water still swims.
Corners of two *groups* make a transition tile: never buildable, the numeric effects averaged.

| Group (canonical type; looks) | In play (engine rule) | Where it works | Where it hurts | Precedent |
| --- | --- | --- | --- | --- |
| Ridges (`RIDGE_ROCK`; outcrop, hoodoos, penitentes...) | Impassable on the ground; towers shoot over | Ranges, bluffs, rims, tors, knots with quarry faces cut in | Costs the farmland it covers; a knot can wall ground off | Bajada, Rain shadow, Crater lakes, Braided river |
| Obstacles (`THICKET`, `BOULDERS`; cliffs, hedge...) | Impassable; stop projectiles | Hammocks and headlands that should also block fire | Same as rock, plus towers cannot cover across them | Everglades, Swamp, Ring world |
| Deep water (`DEEP_WATER`, `DARK_WATER`) | Swims at 192/256 speed; waters land fully; no algae | Lake, sea and channel cores | A core reaching the bank kills algae and beaches; slower swimming changes every swim shortcut | All the water maps |
| Fertile (`LOAM`; moss, spring meadow) | Buildable crop land that waters the ground round it at 48/256 of water | Levees, lake rings, seeps, bog hollows | Its edge row is unbuildable; it waters wood too | River, Watershed, Ring world, Crater lakes, Everglades |
| Barren (`SHALE`; heath, tundra, laterite, ash...) | Buildable; holds nothing; inhibits growth exactly like sand; no shore support | Dry uplands and towns that must never overgrow | Inhibits crops up to about 30 tiles from water | Bajada, Watershed, River, Crater lakes |
| Rough (`MUDFLAT`, `SCREE`; mud, hardpan, pumice) | Walks at 160/256 speed; unbuildable; holds nothing; no inhibition, no shore support | Flats, talus, point bars, far beaches | Slows footpaths beside homes; algae loses its sand | Tidal flats, Shattered coast, Rain shadow |
| Paths (`TRAIL`; boardwalk, sandstone, brick...) | Walks at 512/256 speed; buildable; nothing grows on or across it; no inhibition | Ring roads, caravan routes, portage trails, fords | A ruler-straight road reads as a diagram | Ring world, Caravanserai, Portage Lakes, Everglades |
| Grass looks (`LUSH_GRASS`, `DRY_GRASS`) | Exactly grass | The map's one grass, chosen by its climate; a second only where it is the map's subject (Bajada's fans greening where the water reaches) | Dry grass renders as bare brown ground in the game view; distance bands round every pond read as stripes | Bajada |
| Sand looks (`DUNE_SAND`, `PALE_SAND`, `WINDY_SAND_DUNE`) | Exactly sand | The map's one sand, chosen by its climate | Two sands on one beach read as two kinds of ground | Caravanserai, Rain shadow |
| Ice (`ICE`; frozen lake, glacier ice, deep and wind-swept snow) | Walks at 128/256 speed; wears ground units down by 8/256 hp a step; unbuildable; nothing grows | Frozen water to cross on foot at a price | A wide field is a slow, damaging shortcut and building room is lost | Spider web (frozen cells), Honeycomb isle (snow caps) |
| Lava, Void | Burns fliers; nothing crosses | A volcanic centrepiece, drawn in rings | | Lava shield, the climates' volcanic hearts |

## One look per rule group per map

**A hard rule (maintainer, 2026-11-11).** A map draws each rule group in one look: one grass, one
sand, one fertile ground, one barren, one rough, one deep water. The climate or variant table picks
which, so maps still differ from one another; a single map stays readable. A player reads terrain
as rules: two looks of one group on one map say "these play differently" when they do not.

- **A second look needs a named feature.** Keep one only where it marks a place a player should
  recognise and name (a salt pan, a playa, scree at a cliff's foot), drawn as one contiguous region
  the design defines, and agreed with the maintainer. Never deal two looks out by noise, by a vertex
  hash or by distance bands: mottles, stripes and "second" looks were removed from every natural
  generator (2026-11-11) for exactly this.
- **Grass varies only where variation is the map.** Lush grass by water and dry grass beyond is
  Bajada's subject (desert fans greening where water reaches), and lush bars against a dry terrace
  is Braided river's, so both keep two clean regions; on River, Swamp and the rest the concentric
  bands round every pond read as stripes and went.
- **Deep water is a rule, not a default.** It swims slower and grows no algae; draw it only where the
  design wants that (a lagoon core, a channel to slow swimmers, an opt-in control), never as every
  vertex some distance from land. City states and the arena maps dropped theirs (2026-11-11).
- **No geology lessons in looks.** Grain size sorting down a slope, wind-rippled crests on the
  highest dunes and similar realism cost readability for a detail no player acts on.
- **Home gardens show no seam.** Where a map's grass look is not plain `GRASS`, paint the gardens
  in that look once every stage that needs exact `GRASS` has run (below), so no disc marks a home.

## Keep mechanics canonical and paint looks over them

**Design two sketches.** A mechanic sketch holds each vertex's canonical type (`GRASS`, `SAND`,
`WATER`, `DEEP_WATER`, `LOAM`, `SHALE`, `MUDFLAT`, `SCREE`, `RIDGE_ROCK`, `TRAIL`); a look sketch holds
the artwork, and `composeLooks` ([Sketch.h](../../../../src/map/generator/shared/Sketch.h)) takes a
look only where it shares its mechanic's group, so a look can never change a rule. Validators,
growth fields and every shared helper read the mechanic sketch.

**Shared helpers key on exact types.** `Map::isGrass`/`isWater`/`isSand`, `pureTiles(..., GRASS)`,
`layBeaches` (exact `WATER`), `sprinkleSand`, `floodReach`'s crop top-up, the height-field groves and
the legacy start fallback in [Terrain.cpp](../../../../src/map/generator/shared/Terrain.cpp) all test
exact `GRASS` or `WATER`. So keep every home garden plain `GRASS` while they run, lay grass looks
(gardens included, in the map's one grass look) after the starts and the starting-crop guarantee, and treat any check that must see deep water as water through the
group (`wetVertices` in [HeightFieldLooks.h](../../../../src/map/generator/shared/HeightFieldLooks.h),
`swimmable` in the rules). A helper that tests `fertilitySource` for "water" (the shared
`homePondMissing`, Ring world's `furnishHomes`) counts loam as a pond: use swimmable water.

**Give every new layer its own named stream.** A layer drawing from another layer's stream moves
that layer's draws when toggled. Shattered coast's mud first drew from the look stream, so turning
mud off repainted looks; its contract now requires that toggling mud changes only mud vertices.
Layers added over an older generator must leave its earlier draws identical
([stream names](constraint-solving.md#a-stream-name-is-part-of-every-draw)).

**Check looks in the game view, not only the preview** (`glob2 map render`, the
[CLI guide](../../../../docs/map-generators/cli.md)). Dry grass from 20 tiles on River's temperate
banks turned the map brown and read as desert; Swamp keeps it to hummock tops and clears it round
homes. A look boundary the design draws (a pan's edge, a named region) can wander with fractal
noise; smoothstep value noise alone draws straight-sided edges (Braided river). Never use noise to
choose between two looks (see [one look per rule group](#one-look-per-rule-group-per-map)).

## Draw distance in straight lines, frayed

Step distance (`stepsFrom`, eight-connected) draws squares and diamonds: deep cores, rims, loam
rings, dry-grass clearings and home rings drawn by steps came out as Chebyshev squares, triangles
and staircases on River, Swamp and Crater lakes. Use straight-line distance (`distanceSquaredTo`
in Morphology.h for any mask, `distanceFromWet` from water, `deepenWater` for cores) for anything
the player sees as an outline, fray it with noise, and keep steps only as a floor where a rule must hold in steps (the growth probe's square reach). A straight-line
band needs about a tile more depth than a step band to keep the same rock (Crater lakes).

## Rock walls and quarry faces

- **A vertex is rock only where all four tiles touching it are rock tiles** (`rockVertices`,
  [Rock.h](../../../../src/map/generator/shared/Rock.h)). The blocked region is then exactly the
  drawn tiles less the wall's convex corner tiles, which keep one rock corner and walk; walls under
  two tiles thick leave no rock at all. A seal flood must shut a pass together with its walkable
  flank tiles (Rain shadow), and a sealed line needs a walkability flood that ignores deposits
  (Braided river at the torus seam).
- **A quarry face is wall tiles left out of the rock and stocked with stone** (`quarryNear`). Stone
  stands only on pure grass, so the face needs four grass corners and a grass approach ring
  (`quarryApproach`); any other terrain beside it spoils it. In pavement, keep the grass to the
  face's corners and that ring, or it shows as olive discs (Watershed).
- **Find home quarries in the stencil frame** and stamp them, so turned homes match (Bajada; Rain
  shadow stamps colony 0's frame). Keep passes, roads and scree clear of home quarries.
- **Decide what is equal**: stone per home, or the walk to the face. Portage Lakes gives equal stone
  at walks of 5 to 21 steps; Ring world measures the walk on the finished map and opens it. Say which
  in the doc and telemetry, and report an unreachable face as -1, never as a spread of zero.
- **Validate every face tile holds stone.** The shared `quarryFault` faults only tiles that *can*
  hold stone and silently passes a face tile whose corners were spoiled; Caravanserai, Crater lakes
  and Shattered coast add their own check that every face tile holds it. Quarry stone never runs
  out, so a validator's walkability must count it as blocking, as the planner did (Watershed).
- **No knot may part the ground.** Check every rock or thicket group on the finished map with a
  walk round it (`partingBarrier` in HeightFieldLooks.h; several generators carry a local
  `partingGroup`). A rock tile the rock cannot block should keep the obstacle it replaced, or a
  forest wall leaks (Portage Lakes keeps wood on slivers).
- **Rock costs farmland and stone supply.** Crater rims within 24 tiles of homes cost 4% of
  near-home growth; Braided river's two-row knots cost twice what a stone line did, and three-column
  knots with faces read as specks (require five). Where rock with faces replaces an old wall of
  infinite stone, the map's stone supply shrinks: a feel change to report.
- **Talus** (`talusApron`, Rough) skirts rock without inhibiting, but it replaces farmland: on Rain
  shadow's windward foot it cost 6% of growth potential, on the lee in front of the sand nothing.

## Deep water

- Keep a shallow margin at least two tiles wide: Everglades' first cores two steps in left no pure
  shallow tile, algae fell from about 1,750 tiles to 7 and the AIs' wood harvest halved. Beaches,
  algae and ford checks need plain water at the bank.
- Prefer `deepenWater` (straight-line reach, bodies of at least `least` vertices, any sketch) to
  `deepCore`, whose eight-connected steps draw square cores. Size the core by share on wide water
  (`deepReachForShare`): River's fixed core took 80% of the water and two thirds of the algae.
- Move displaced algae to the same body's shallows by distance plus noise (Crater lakes, River);
  breadth-first reseating drew wedges.
- Where algae is seeded after deepening, hold its supply with `algaeBandTiles` (Planting.h):
  measure the band on the sketch before and after the cores, then scale both the band's tiles per
  clump and its best-growing share by that ratio. Scaling only the clump size kept the count but
  crowded the clumps onto a smaller best share, and Spider web lost 12–18% of its algae tiles
  until the share was widened too (Coral, Spider web).
- A deep cutoff by radius draws a circle across open sea. Take cores by distance from land and
  keep only those joined to the region you call ocean (Coral), so the edge follows the coasts.
- Drop specks (`dropSmallRegions`) and taper threads in and out; a thread stamped as discs ends in
  round caps (Watershed).
- Deep water swims slower: the sea timer lengthens (Ring world) and a swim shortcut saves less.
  Validate a shortcut's saving in walking time, not steps. Report the feel change.

## Fertile ground

- It waters itself and its neighbours at 48/256 of water, so a band must be wide to pay: its
  boundary with grass is a row of transition tiles, unbuildable and not farm-paintable. River's
  levees two or three deep lost food, seven gained about 5%; Watershed's five deep gained 8% and nine
  52%. Measure the width rather than guessing.
- It waters wood as well as wheat: Portage Lakes' loam bay farms shrank the dry forest until a
  compact map lost its portage, which is why they stay grass.
- **Fix the AI, not the map.** AI placement that treats any fertility source as water (it was found
  in Nicowar's water entity and Maxima's water tier, and the same reading in other AIs' farming)
  stalls colonies beside loam. Put Fertile ground where the design wants it, home surroundings
  included (outside the plain garden), and fix the AI's logic, not its strength. Don't keep an
  AI-only clearance; River and Ring world dropped theirs. Read a births drop next to Fertile ground
  as this defect until shown otherwise.
- Don't set a Fertile layer's default from the growth tool's edge undercount or from an AI's
  misreading: Swamp's moss went opt-in on both and is the case to revisit once they are fixed.

## Barren pavement

**It inhibits growth exactly like sand**, and the mirrored probe carries that up to about 30 tiles
from water. Bajada put it on the old gravel grass and cut near-home food by a third. Lay it only
beyond 22 steps from water (`paveBeyondWater`) or where sand already was (Watershed's uplands),
never on old farmland, and measure growth before and after. It holds no wood, so a forest floor
stays grass (Portage Lakes). As town ground it never overgrows (Bajada's pavement-ringed towns).

## Rough ground

Mud, mudflat and scree slow walkers to 160/256 and inhibit nothing, so crops beside them regrow
better: Tidal flats' mud instead of sand flats nearly doubled near-home food potential. They also
give algae no shore, so keep sand banks by algae water (Tidal flats; Shattered coast leaves open
shores sand). Keep rough ground off home dooryards for the design's sake: a colony's own footpaths
should not be slower than a rival's. Shattered coast keeps mud 28 tiles off homes (from 16 it cost 6%
of births despite more food); Caravanserai keeps rough floors 18 corners off; Swamp keeps mud 12
tiles off.

## Paths

Paths double walking speed, are buildable and never overgrow, so a ring road or caravan route makes
logistics part of the game and keeps a loop open against growth (Ring world lays its road before any
deposit). Being buildable, a road can be built over; sand is the lane nobody can build on. Reserve a route as sand for the shared router and repaint its corners as trail afterwards
(Portage Lakes). A cheapest walk over smooth noise is ruler-straight: add broad noise from its own
stream to the walk cost (Caravanserai: up to 24 on a step of 14, cells 40 tiles across). Boardwalk
reads right for fords and causeways across wet ground (Everglades, Swamp). Keep a road off every
corner of a stamped home, or homes stop matching by rules.

## Post-passes over a legacy generator

River, Swamp, Crater lakes and Shattered coast keep their old draws and add a pass. Repaint only
ground nobody stands on (gardens, kits, deposits and starts untouched), keep every earlier draw
identical, and have `generate` and `validateWorld` plan the pass from the same input: River's
validator first replanned bluffs from the finished map, after the crop top-up had cleared stone,
and checked a different plan; both now rebuild the field with `heightFieldSketch` and a contract
ties the replanned counts to generation's telemetry. Record the swarm corners in telemetry when the
old report has none. Shared pieces are in HeightFieldLooks.h; reuse them before writing another.

## Validators and identity

- Compare stamped homes by full tile rules (`sameTerrainProperties`), not types or a coarse kind;
  looks follow the water and a neighbour's water reaches a home's margin. Leave out the unstamped
  wall behind a home (Bajada).
- Read water as either water group and walkability from the rules (`walkableTiles` on a sketch or
  map). Count terrain by group, not type: a dump that called every boundary between two looks mixed
  made Braided river look a third poorer than it played.

## Measure with a rules-aware tool, and know its blind spots

[`growth_potential.c`](../scripts/growth_potential.c) counts deep water as water, Fertile ground as
48/256 sources and farmland, and Barren ground as an inhibitor. The terrain dump folds look
boundaries inside grass, sand and water, but any other transition, including grass beside loam and
two Fertile looks side by side, is "mixed", and the tool scores mixed tiles as nothing (the engine
blends them). So narrow Fertile bands under-read: measure a Fertile layer with one look, and treat a
small loss at its edge as the tool's before calling it the map's. Pin every variant and measure food
within 24 and 48 steps; the weakest variant sets the bar
([growth potential](tuning-playbook.md#measure-growth-potential-not-water-share)).
