# Everglades

**Everglades** (`everglades`, numeric ID 19) is a river of grass: a dense wetland, the same
everywhere and across the wrap, of pools and long sloughs with dark beds lying in a plain of moss
and sawgrass, tree islands of thicket standing in the broad dry reaches, and banks of sand and mud
winding between the water. Every colony starts in a clearing with a pond and a kit, ringed by a sand
levee with gaps the swamp creeps through; what lies beyond is held only while it is kept cut.

Revision 4 (Everglades 2.0) redrew the swamp with the engine's full terrain palette; revision 3 drew
it with grass, sand and water alone. The pools, clearings, levees, starts and kits are the same as
revision 3's for the same seed; the deposits differ a little where hammocks now stand.

## How it plays

- **The swamp grows shut.** Every tile is a few steps from water, so wood and wheat grow back at the
  engine's top rate, and resources block movement: the swamp closes unless workers keep cutting it.
  Nothing is symmetric on purpose; fairness is statistical, and the lobby keeps the best-scoring of
  several seeds.
- **The banks are the lanes.** Nothing grows on a pool's bank, so the banks between pools stay open,
  winding and narrow. Half the swamp's banks are now mud (mudflat: walkable at 160/256, unbuildable,
  holds nothing): the lanes stay open, but mud does not inhibit growth as sand does, so the grass
  beside a mud bank regrows harder and travel along it is slower. Banks within six tiles of a
  clearing's levee stay sand, so a home keeps its room.
- **Moss farms better.** The swamp's wet reaches are moss (Fertile: crop land like grass that also
  waters its neighbours at 48/256), most of its grass at the defaults; the dry reaches are sawgrass.
  The clearings and a tile round them stay plain grass.
- **Sloughs run dark.** The big pools and sloughs have a bed of deep water (swimmable but slower, no
  algae, still waters the land); every pool keeps a shallow band about two tiles wide where algae
  grows and the banks and fords work.
- **Hammocks are landmarks you walk round.** Tree islands of thicket (Obstacles: impassable, block
  arrows) stand in the broadest dry ground, one tile of grass off every bank and six tiles beyond
  every clearing, so they never sit on a lane; a hammock that would cut ground off is not laid.
- **Fords are boardwalks.** When the sloughs box a colony in, `openRoutes` opens the cheapest way
  through, clearing deposits and fording water. Revision 3 forded with sand, which looked like an odd
  spit across a pool; the ford is now boardwalk (Paths: fast, buildable, holds nothing). Fords are
  rare at the defaults (none in the default sweep); dense, big sloughs bring them.
- **Stone is where it was.** The swamp's scattered stone outcrops and every home's quarry are placed
  as in revision 3; hammocks replace none of them. Each colony walks 10–18 steps to its nearest stone
  on seeds 1–3, as on revision 3 (14–18).

## How it is built

1. **Layout (revision 3's, unchanged).** One pool per cell of a `pool-spacing` lattice that divides
   the map exactly, jittered within its cell, its radius `pool-size` scaled by a smooth wetness field
   (0.6 at the driest ground to 1.5 at the wettest) and a random factor; `sloughs` percent are grown
   1.4 times and stretched 2.2 times. Clearings of `clearing-size` radius sit near a ring at 58% of
   the half side, nudged off it, each with a pond, a two-tile levee of which `levee` percent stands,
   and no pool water two tiles beyond. A clearing that does not fit shrinks (to radius 6), then the
   margin goes, then the ring widens to 76% of the half side; pools whose expected coverage passes 60%
   are scaled down.
2. **Mechanic sketch.** One canonical type per vertex for every rule group (GRASS, SAND, WATER,
   DEEP_WATER, MUDFLAT, MOSS, THICKET), which every shared helper and check reads: revision 3's
   grass, water and levees with beaches laid; hammocks planned on it; mud on `mudflats` percent of
   the swamp's beach vertices, ranked by smooth noise so the mud lies in long stretches; moss on swamp
   grass where the wetness field passes 0.30, beyond a tile past each clearing's levee clearance;
   thicket where all four tiles round a vertex are hammock (`rockVertices`); deep water on pool water
   four or more vertex steps from any bank (`deepCore`, cores under six vertices dropped).
3. **Hammocks.** Candidate centres are pure grass tiles at least four steps from anything else and
   six tiles beyond a clearing's levee clearance, shuffled and then taken broadest first; each knot is
   a rough disc 2.8–5 tiles across its radius, capped by its centre's room, every tile at least one
   step from any bank, trimmed to tiles in a two-by-two block (so every tile has a thicket vertex),
   kept at ten tiles or more and 16 tiles from the next. A hammock whose blocked tiles would leave the
   ground round it in two pieces is dropped and the check runs again. `hammocks` per 128x128 tiles
   are wanted; telemetry reports how many fitted.
4. **Looks.** `composeLooks` paints looks inside each rule group, so looks never change a rule: deep
   beds dark water; sawgrass lush where it is wet or near the water and tawny where it is dry; mud
   banks wet mud and mudflat, or mudflat and hardpan; beach sand pale in patches in the dry season;
   moss, with patches of loam (peat) away from the water in the dry season.
5. **Resources (revision 3's).** Each home's unscaled kit (40 wheat, 30 wood, a quarry) beside its
   pond; off the clearings wood on 55% of the ground and wheat on 15%, in patches, stone outcrops and
   fruit groves, scaled by the amounts; algae on the shallows; the starting-crop guarantee.
6. **Routes.** `openRoutes` (`openColonyRoutes`) opens a way from colony 0 to any colony it cannot
   walk to; every vertex it forded is then painted boardwalk.
7. **Checked, not assumed.** `validateWorld` rebuilds the design and requires every clearing's pond
   (any swimmable corner), no deposit on a levee, deep water only inside shallows (or beside a
   boardwalk ford), every hammock tile still blocked and no hammock cutting ground off, and every
   colony reachable on foot from colony 0 with water, buildings and every resource blocking.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Pool spacing | 8–20 (15) | Lattice cell size; closer pools link more banks into lanes. |
| Pool size | 2–7 (4) | Pool radius before wetness and random scaling. |
| Sloughs | 0–50% (15) | Share of pools grown and stretched into sloughs. |
| Clearing size | 12–18 (13) | Clearing radius, shrunk to fit a crowded ring. |
| Levee | 0–100% (70) | Share of each clearing's ring that is sand. |
| Mudflats | 0–100% (50) | Share of the swamp's pool banks that is mud rather than sand (banks near a clearing stay sand). |
| Hammocks | 0–8 (3) | Thicket tree islands wanted per 128x128 tiles, as many as fit. |
| Season | Random, Wet, Dry | Looks only: lush sawgrass and wet mud, or tawny sawgrass, dried mud, pale sand and peat. Random picks from the seed. |
| Wheat, wood, stone, algae, fruit amount | 0–300% (100) | The swamp's fields, outcrops, groves and algae; every home's kit is unscaled. |

## Food

Growth potential (`growth_potential.c`: the engine's crop growth chance summed over farmland, moss
counted as farmland that waters at 48/256, deep water as water), 256x256, 4 colonies, seeds 1–3,
revision 4 against revision 3 on the same seeds and settings. Mean per colony, worst colony in
brackets. The season changes no rule on any tile (contract), so food is measured with it pinned wet;
the dry season's moss/loam tiles play as moss but the terrain dump records them as mixed, which the
tool scores as nothing (its row below shows that undercount, not less food).

| Variant | Total | Within 24 of swarm | Within 48 |
| --- | --- | --- | --- |
| Revision 3 | 2,188 | 66.6 (40) | 229.0 (115) |
| Revision 4, defaults (season wet) | 2,342 | 68.0 (41) | 244.3 (116) |
| Mudflats 0 | 2,338 | 68.0 (41) | 243.8 (116) |
| Mudflats 100 | 2,346 | 68.1 (41) | 244.4 (116) |
| Hammocks 0 | 2,387 | 68.3 (41) | 249.6 (119) |
| Hammocks 8 | 2,285 | 68.4 (41) | 242.6 (118) |
| Season dry, as the tool scores it | 2,183 | 66.4 (41) | 226.9 (112) |
| Random season, as the tool scores it | 2,239 | 66.7 (41) | 234.7 (112) |

Moss is the difference. The tool scores a tile where grass meets moss as nothing (it grows crops in
the game but is not buildable), so a little moss measured worse than none: the wettest reaches alone
(wetness 0.62) cost 5%, and 0.45 still 4%; at 0.30 the moss covers most of the swamp and gains 7%
overall. The mud banks barely move the tool's number, because it counts only pure sand tiles as
inhibitors and a bank is one vertex wide; in the game every sand corner inhibits a share of its tile,
so the mud's gain is real but unmeasured here. Hammocks take swamp ground, about 2% at the most.

A four-AI calibration game (`map study everglades --seed 101`, 256x256, Nicowar, Cortex, Cabino and
Maxima, 20,000 ticks; revision 3 played by the master release binary, revision 4 by the development
build, which plays revision 3's map identically) is summarised below over game seeds 1–4.

| Game seed | Births (3 / 4) | Wheat (3 / 4) | Wood (3 / 4) | Buildings (3 / 4) |
| --- | --- | --- | --- | --- |
| 1 | 90 / 97 | 1,180 / 1,180 | 286 / 249 | 44 / 39 |
| 2 | 90 / 94 | 1,124 / 1,149 | 321 / 215 | 39 / 39 |
| 3 | 93 / 87 | 1,214 / 1,075 | 279 / 197 | 42 / 37 |
| 4 | 94 / 87 | 1,245 / 1,085 | 228 / 221 | 41 / 40 |
| Sum | 367 / 365 | 4,763 / 4,489 | 1,114 / 882 | 166 / 155 |

Births are level, wheat 6% lower, buildings 7% lower and wood harvested 21% lower. Revision 4 with
the deep beds two steps in harvested about half revision 3's wood; moving them to four steps (algae
back to about 70%) recovered most of that, and with mud, moss or hammocks turned off one at a time
the wood came back by about a tenth each over two games, within the noise of single games. The swamp
regrows harder and the mud lanes are slower; the AIs build a little less. No home was choked by wood
at the end of game 1 (`calibration_final_old_new.png` in the evidence): every clearing held, the
swamp beyond was wood on both revisions.

## Limits

- The swamp grows shut where nobody cuts it: after 4,096 ticks of growth with every resource at its
  ceiling and nobody harvesting, a colony can be walled in (revision 3 promised no more, and the
  contract accepts only that fault).
- Mud banks are slower to walk than sand, so lanes are slower; algae needs a sandy shore within
  reach, so a pool whose banks are all mud regrows little algae.
- Deep beds two vertex steps from the bank (the least `deepCore` allows) left almost no pure shallow
  water: algae fell from about 1,750 tiles to 7 and the AIs' wood harvest halved. The beds are four
  steps in, so only the big pools and sloughs have one.
- Hammocks are fewer than wanted on crowded or very wet maps (telemetry `everglades.hammocks.short`).
- Boardwalk fords are one ford corner wide, as revision 3's sand fords were.
- Generation takes about a quarter longer than revision 3 (a 512x512 map with four colonies in 7.6 s
  against 6.0 s on the same development build, so about 0.7 s of release-build time against 0.56 s):
  the hammock plan, mud ranking, moss and deep cores are whole-map passes, and the validator rebuilds
  the design.
- No rotation tournament or human playtest has been recorded for revision 4.

## Verification

Run the focused contracts (`GLOB2_CONTRACTS=everglades`, `MapGeneratorContracts.h`) and the platform
golden rows, inspect small and large rectangles and resource extremes, then check populated games with
complete seat rotations. See [generator verification](verification.md) for commands, revisions,
save/replay checks and evidence requirements.

## Implementation source

[EvergladesGenerator.cpp](../../src/map/generator/generators/EvergladesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
