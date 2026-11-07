# Braided river

**Braided river** (`braided-river`, numeric ID 42) is a glacial outwash plain: a wide pale bed of
gravel runs the long way round the map, its channels splitting and rejoining round lens-shaped bars
of lush grass, darker where they scour pools at the confluences. Dry, stony terraces lie along either
bank, and a line of bedrock bluffs closes the far seam of the torus where the two terraces meet. The
glacier's moraine is hummocked along each terrace edge. Homes stand on the terraces; the bars are the
fertile ground; on foot the only way from bank to bank is over the riffles (sand fords) that join the
bars this map, and which bars join which is redrawn every seed.

Revision 5 (Braided River 2.0) redrew the map with the engine's full terrain palette; revision 4 drew
everything from grass, sand, water and stone. The braid, the riffles, the homes and their kits are
laid exactly as revision 4 laid them for the same seed.

## How it plays

- **The bars are the farms.** Wheat and wood regrow only within fifteen tiles of water, so a bar with
  channels on every side is the richest ground on the map, a terrace's bank strip is middling, and the
  town behind a home never grows over. The bar nearest each home is joined to its bank by a forced
  riffle and stocked with wheat and wood; every further bar is a decision, richer and more exposed the
  deeper into the braid it lies.
- **Riffles are the routes.** Raiders and farmers cross the same riffles. They lie on a random spanning
  tree of the bars plus `extra-riffles` percent of the spare crossings as loops, so scouting the braid
  is worth the trip.
- **Deep pools swim slowly.** Where a channel is widest, mostly at confluences, its core is deep water
  (`deep-pools`): swimmable, but slower, and no algae grows there. It waters the bars as plain water
  does and changes no route on foot. Swimming across the braid is slower than in revision 4.
- **The moraine is cover with doors.** Knots of rock two tiles deep stand four steps up each bank,
  with a gap at every riffle landing and none against a home. Towers shoot over them; units walk round.
  Stone stands on a knot's flank where a run of hummocks starts or ends.
- **The bluffs are a wall with quarries.** The seam is a sealed line of rock: nothing walks round the
  back of the torus, so the braid stays the only way across. Buttresses stand out of it with a quarry
  face of stone: one on every colony's own terrace, as near its column as fits and the same ten stone
  each, and more along both faces with `stone-amount`. Scree skirts the rock (slow, unbuildable,
  grows nothing).
- **The dry terrace is building room.** Beyond the water's reach the terrace is pavement (heath or
  tundra: buildable, holds nothing, slows growth near it as sand does) and dry grass with finite
  woodlots and stone outcrops. Revision 4 broke the terrace with sand patches from eight steps from the
  water, which slowed the bank strip's crops; pavement lies only where nothing grows anyway.

## How it is built

`src/map/generator/generators/BraidedRiverGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made. The design keeps a mechanic sketch (the canonical type of
each rule group: GRASS, SAND, WATER, DEEP_WATER, SHALE, SCREE, RIDGE_ROCK) that every shared helper and
check reads, and `generate` paints looks over it with `composeLooks` (`shared/Sketch.h`), so looks never
change a rule.

| Stage | What happens |
| --- | --- |
| Threads | `channels` sinusoidal threads (3 to 9; 6 by default) wrap the map a whole number of times, each in its own lane across the belt (`braid-width`), neighbours in antiphase so every adjacent pair crosses twice a period and the land between is a lens-shaped bar `bar-size` long. Lanes are held between 11 and 26 tiles by dropping or adding threads. Every channel keeps a 4-connected core of open water outside its riffles (`channelCoreFault`). |
| Bluffs' core | A sealed line (`traceSealedLap`) waving round the seam, and every tile touching it: rock vertices stand where all four tiles are rock (`rockVertices`, `shared/Rock.h`), so the whole spine is rock. |
| Bars and riffles | The bars are the land components the rasterized water leaves. `channelCrossings` finds every stretch two bars face each other across; riffles (`SandFord`) are tried on the sketch (`stampFord`, `fordFault`): one forced per colony to the nearest bar with room for wheat and wood, a spanning tree, then loops. |
| Homes | 8 to 13 steps up the bank, one slot per colony along the lap alternating banks, dealt at random. |
| Moraine | Hummocks on the contour four steps up, in runs of 3 to 6 with gaps of 4 to 7, doors at riffle landings and 4 tiles clear of every home; each knot takes the next contour too (two rows, so it holds rock vertices); stone on a flank per run per 100% of `stone-amount`. |
| Buttresses and relief | Every colony's buttress (a half ellipse four tiles out, its outer two rows over five columns a quarry face) as near its column as fits on its own terrace; ambient buttresses one per 64 tiles of each face at 100% stone; a relief of up to three rows along each face; scree aprons (`talusApron`). Rock keeps 3 tiles clear of every home's settling box and 12 steps from water. |
| Pavement | `dry-patches` percent of the terrace beyond 22 steps from water, where a fractal noise peaks, 20 tiles (frayed) from every home's middle and off every quarry. |
| Deep pools | Water two steps from any land (`deepCore`), opened so only cores three vertices across survive, in pools of ten or more, clear of every riffle. |
| Looks | Season (Meltwater: every bar tile lush, blue deep water, tundra pavement; Late summer: bars lush only by the water, dark water, heath and dirt). Terrace green within 14 or 9 steps of water (frayed) and dry grass beyond, every home's garden plain grass; every sand vertex pale gravel; knots outcrop. |
| Resources | Every home's kit (wheat and wood beside the swarm, stone behind) and its promised bar's wheat and wood, unscaled; bar farmland a third of their grass, bank strips a quarter of that; dry woodlots and outcrops counted over dry grass and pavement both; fruit on the deep bars; algae; quarry stone. |

`validateWorld` rebuilds the design and requires: the band round the seam cannot be crossed over
walkable terrain, every deposit counted as open ground (so the rock alone seals); every quarry face holds
its stone and every colony's own face the same ten (`quarryFault`); every channel's core and every
riffle open on tiles of water corners only, deep or plain; every riffle walkable with land at both ends;
every colony walking to colony 0 and onto its promised bar beside wheat and wood.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Braid width | 30–60 (40) | The belt as a share of the map's breadth; the rest is terrace. |
| Channels | 3–9 (6) | Threads across the belt, clamped so lanes stay 11 to 26 tiles. |
| Bar size | 24–64 (40) | A bar's length along the river. |
| Extra riffles | 0–100 (25) | Share of the spare bar-to-bar crossings opened as loops. |
| Dry patches | 0–60 (35) | Share of the terrace beyond the water's reach laid as pavement. Revision 4 meant sand patches nearer the water (0–30, 12). |
| Moraine hummocks | on/off (on) | Rock knots with stone flanks along each terrace edge. |
| Deep pools | on/off (on) | Deep water in the widest stretches of channel: slower swimming, no algae. |
| Season | Random, Meltwater, Late summer | Looks only; both play identically. |
| Wheat, wood amount | 0–300% (100) | Bar and bank farmland, dry woodlots. The kits and promised bars are guaranteed. |
| Stone amount | 0–300% (100) | Ambient buttress quarries, moraine flanks, dry outcrops. Every colony's own quarry face is guaranteed. |
| Algae, fruit amount | 0–300% (100) | Linear. |

## Food

Growth potential (`growth_potential.c`: the engine's crop growth chance summed over farmland, counting
pavement as an inhibitor and deep water as water), 256×256, 4 colonies, seeds 1–3, revision 5 against
revision 4. Terrain dumps record a tile between two looks of one rule group (grass beside lush grass,
water beside deep water) by its group since revision 5; before that such tiles were "mixed" and the tool
counted none of them, which made every 2.0 map look a third poorer than it plays.

| Variant | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| Revision 4 | 1,725 | 59.7 (41) | 174.8 (95) |
| Revision 5, defaults | 1,740 | 60.2 (43) | 177.6 (97) |
| Meltwater, Late summer (each pinned) | 1,740 | 60.2 (43) | 177.6 (97) |
| Deep pools off | 1,740 | 60.2 (43) | 177.6 (97) |
| Dry patches 0 | 1,744 | 60.2 (43) | 178.6 (99) |
| Dry patches 60 | 1,738 | 60.1 (43) | 177.4 (97) |
| Revision 4, moraine off | 1,785 | 65.8 (46) | 186.8 (107) |
| Revision 5, moraine off | 1,814 | 66.4 (46) | 191.2 (114) |

Seasons are looks only and deep pools water the land as plain water does, so neither moves the
numbers. Pavement lies beyond the water's reach and costs almost nothing even at 60%. The moraine is
the one cost: knots two tiles of rock deep at revision 4's runs took 9.6 of the 66 within 24 steps
(revision 4's line of stone took 6.1), so the runs are shorter (3 to 6 with gaps of 4 to 7) and keep
4 tiles off every home's 8x8. Removing revision 4's sand patches, which inhibited the bank strip,
gains a little everywhere.

A four-AI calibration game (`map study braided-river --seed 101`, 256×256, Nicowar, Cortex, Cabino
and Maxima, game seed 1, 20,000 ticks, both maps played by the same game binary) harvested 1,346 wheat
and 347 wood with 84 worker births and 54 buildings on revision 5, against 1,118 wheat, 301 wood, 77
births and 45 buildings on revision 4; the weakest colony (Cabino) harvested 288 wheat against 194.
One game varies by up to 40%: this shows no opening stall on the dry terraces, not an improvement.

## Limits

- Every colony's own quarry face is a walk across its terrace (about 65 steps at 256 by default, the
  same within a few steps for every colony); the nearest stone is the kit's clump behind the swarm and the
  moraine's flanks a few steps away.
- The season and the pavement's look are looks only: a map plays the same in both seasons.
- Deep pools make swimming the braid slower than revision 4; no AI game has measured swimmers.
- Some seeds at 128 tiles with one colony are refused for a riffle without a walkable bank, as in
  revision 4; the lobby rolls again.
- Generation takes about 10% longer than revision 4 (a 512×512 map with eight colonies in about 0.6 s
  of release-build time, scaled from a development build against unchanged generators).
- No rotation tournament or human playtest has been recorded for revision 5.

## Playtest focus

Revision 4's dry terraces could stall AI openings even when resources, town room and geometry checks
passed. The calibration game above shows none, but compare multiple controllers and complete seat
rotations, and inspect hauling and construction rather than treating a low unit peak as proof of a
disconnected start. Watch swimmers in the deep pools, and whether the colonies' own quarry faces, a
walk across the terrace, are used before the moraine's flanks.

## Verification

The source definition and its request/world validators define accepted settings and finished-map
guarantees. Run the focused contracts (`GLOB2_CONTRACTS=braided-river`, `MapGeneratorContracts.h`)
and the platform golden rows, inspect small and large rectangles and resource extremes, then check
populated games with complete seat rotations. See [generator verification](verification.md) for
commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[BraidedRiverGenerator.cpp](../../src/map/generator/generators/BraidedRiverGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
