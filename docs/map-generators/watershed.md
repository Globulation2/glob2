# Watershed

**Watershed** (`watershed`, numeric ID 13) is a river basin draining into a sea along one side of
the map: springs in the uplands feed tributaries that join a trunk, the trunk splits into a delta at
the coast, and the dry divides between the catchments rise beyond any river's reach. Rivers are the
map's walls and its wealth at once: the best farmland lies against channels a colony cannot cross,
and on foot a river is crossed only at its fords.

Revision 4 (Watershed 2.0) redrew the map with the engine's full terrain palette; revision 3 drew it
from grass, sand and water. The rivers, fords, starts and kits are laid exactly as revision 3 laid
them for the same seed.

## How it plays

- **Fords are the routes.** Every channel keeps a core of open water from its spring to the sea, so a
  river is crossed on foot only at a ford, a sand strip laid across a straight stretch (`fords`), or
  round its spring. Colonies start on the floodplain, 5 to 11 steps from water, preferably on
  different banks, so a neighbour is usually across a river and contact runs through the fords.
- **Deep water swims slowly.** The sea's core and a thread down the middle of the trunk, the delta
  and the big tributaries are deep water (`deep-water`): swimmable but slower, and no algae grows
  there. It waters the land as plain water does, every bank keeps a margin of shallows, and no ford
  touches it, so every route on foot is as before. Swimming the trunk and the sea is slower than in
  revision 3.
- **Levees are the richest ground.** A band of fertile ground (spring meadow, moss or loam by
  climate) runs five vertices deep along every channel behind its beach: crop land that is
  buildable and waters the ground round it a little. It makes the river country somewhat richer
  than revision 3 (see the food table); the edge row where levee meets grass grows crops but takes
  no building. Every home's garden (12 tiles round its middle) stays plain grass, so homes start
  as they did.
- **Mudflats at the delta and the confluences.** The river beaches where the delta splits and where
  rivers meet are mudflat: slow, unbuildable, holding nothing, but unlike sand they do not slow the
  crops behind them. The sea's own beaches stay sand, which the algae need.
- **The divides are building room.** Revision 3's dry uplands were sand. Sand stays where it was,
  but beyond 22 steps from water it is pavement (heath, sandstone or laterite by look): walkable and
  buildable, holding nothing and slowing crops near it exactly as sand did. Nothing grew there, so
  the food is unchanged; the far uplands are now room for a second town.
- **Outcrops with quarries.** Knots of rock stand on the crests of the divides, at least 20 steps
  from water and never two within 24 tiles (`outcrops`): impassable, towers shoot over them. Most
  carry a quarry face of stone on their downhill side (how many follows `stone-amount`), with scree
  round the rest. Stone still also comes in every colony's kit and in small outcrops at the upland
  edge, as before.

## How it is built

`src/map/generator/generators/WatershedGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made.

| Stage | What happens |
| --- | --- |
| Layout | The coast, springs, flow tree and fords are planned in a canonical frame from named streams, then oriented: the sea can face any side of a square map but a short end of a rectangular one. Springs are sampled by land area (`river-density`) and join the network downstream of them nearest-first; width grows with the square root of the springs upstream (`river-width`); the trunk splits into one to three distributaries on a delta lobe (`river-delta`); every path meanders (`meanders`). |
| Stamp | Revision 3's terrain: grass, channels and sea, the dry uplands as sand beyond the `dryness` reach (never within 11 tiles of water), the beach rule applied and fords laid. It is written, and `Map::layBeaches()` must change nothing. Every ford and channel is checked on it. |
| Starts | Floodplain sites spread across banks, as in revision 3. |
| Outcrops | Knot centres on crest tiles of the distance-from-water field (no neighbour further from water), 20 steps or more from water, 24 tiles apart, one per 3,500 tiles at most; each a wavering lens 9 to 15 tiles long and 4 to 6 thick along the crest, every tile 17 steps from water. Quarry faces (the downhill rim within two tiles of the middle) on 60% of the knots at 100% stone; the rock left two tiles thick drawn with `rockVertices`; a flood drops any knot whose rock and stone part the walkable ground round it. Planned from the request alone. |
| Mechanic layers | On a copy of the stamp: pavement (`SHALE`) on upland sand 22 to 25 steps (frayed) from water, grass on the corners of every quarry tile and the ring before it and nowhere else (a disc 2.5 tiles round each face drew olive dry-grass patches across the pavement), frayed scree, rock vertices; the deep sea core (four steps from land, opened) and a deep thread three vertices narrower than each big channel's radius (`deepCore`), tapering in and out over 8 tiles of the channel instead of ending in round caps, with any thread the sea's core does not join under 30 vertices dropped (short runs read as dark pills); mudflat river beaches within 7 tiles of the delta's channels and 9 of a junction, clear of fords, gardens and the sea; levees (`LOAM`) on grass two to five (six where a noise peaks) vertices from water, outside the gardens. |
| Looks | `composeLooks` over the mechanics by climate: levees, lush grass within 7 to 11 steps of water, dry grass beyond 10 to 18, beach and dune sand, two pavement looks, deep water, mud and rock. The finished terrain must again be a fixed point of `layBeaches()`. |
| Resources | As in revision 3, on crop land that now includes the levees: every colony's kit, farmland ribbons along the rivers at 2:1 wheat to wood, stone outcrops at the upland edge, fruit at confluences, algae at the mouths and in the sea's shallows; quarry stone on the faces, protected from every clearing and route. |

`validateWorld` plans the layout and the outcrops again from the request and requires: every ford
interrupting a real channel, open across its width with walkable land at both ends, and every channel
keeping its core outside its fords, on tiles of water corners, deep or plain; every quarry face
holding its stone (`quarryFault`); no knot of rock parting the walkable ground round it, its quarry
stone counted as blocking, since a quarry never runs out (as the planner counts it); every colony
walking to colony 0 and standing beside wheat and wood.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| River density | 1–10 (5) | Springs per area of land; each that reaches the network is a tributary. |
| River width | 1–5 (2) | The narrowest channel, at a spring; rivers widen downstream. |
| Dryness | 0–10 (5) | How near the rivers the uplands dry to sand (never within 11 tiles); beyond 22 steps the dry ground is pavement. 0 leaves the uplands grass, with no pavement. |
| Fords | 1–5 (3) | One ford per about 80 tiles of river at 1 to 26 at 5. |
| River delta | on/off (on) | Off, the trunk reaches the sea through one mouth, with no delta lobe. |
| Meandering rivers | on/off (on) | Off, rivers run without their meanders. |
| Deep channels | on/off (on) | Deep water in the sea and down the big channels: slower swimming, no algae. Routes on foot are the same either way. |
| Divide outcrops | on/off (on) | Rock knots on the divides' crests with quarry faces and scree. |
| Climate | Random, Temperate, Steppe, Tropical | Looks only: Temperate (spring meadow levees, heath and shale, pale beaches, blue deep water, rock outcrops), Steppe (loam, sandstone and dirt, hardpan mud, hoodoos, grass dry from 10 steps), Tropical (moss, laterite and clay, mud, dark water, grass green to 18 steps). All three play identically. |
| Wheat, wood amount | 0–300% (100) | The farmland ribbons; every kit stays. |
| Stone amount | 0–300% (100) | Upland-edge outcrops and the share of knots with a quarry face (none at 0, all from about 170%). Every kit's stone stays. |
| Algae, fruit amount | 0–300% (100) | Linear. |

## Food

Growth potential (`growth_potential.c`: the engine's crop growth chance summed over farmland, levees
counted as farmland that waters at 48/256, pavement as an inhibitor, deep water as water), 256x256,
4 colonies, seeds 1-3, revision 4 against revision 3 on the same seeds and settings. Mean per colony,
worst colony in brackets.

| Variant | Total | Within 24 of swarm | Within 48 |
| --- | --- | --- | --- |
| Revision 3 | 2,004 | 87.2 (41) | 228.8 (87) |
| Revision 4, defaults | 2,161 | 91.9 (43) | 248.1 (88) |
| Each climate pinned | 2,162 | 91.9 (43) | 248.0 (89) |
| Deep channels off | 2,162 | 91.8 (43) | 247.9 (89) |
| Divide outcrops off | 2,156 | 91.7 (43) | 247.3 (89) |
| Revision 3, dryness 0 | 2,114 | 82.9 (39) | 238.2 (137) |
| Revision 4, dryness 0 | 2,281 | 87.2 (38) | 259.0 (147) |
| Revision 3, dryness 10 | 1,650 | 72.0 (31) | 191.2 (56) |
| Revision 4, dryness 10 | 1,784 | 74.2 (33) | 205.6 (63) |
| Revision 3, river density 2 | 1,459 | 64.9 (35) | 180.2 (97) |
| Revision 4, river density 2 | 1,588 | 68.1 (38) | 196.9 (97) |
| Revision 3, river density 9 | 2,009 | 80.1 (37) | 208.2 (74) |
| Revision 4, river density 9 | 2,141 | 84.0 (37) | 222.3 (75) |

The levees are the difference: they water the ground round them, and five vertices deep they gain
about 8% overall and 5% near the homes, whose gardens keep plain grass. Narrower levees cost food:
the row where levee meets grass grows crops but the tool counts it as nothing (and it is not
buildable), so two or three deep lost a quarter of the growth potential and four deep 7%; nine deep
gained 52%, too large a change in feel. Climates are looks only and deep water waters the land as
plain water does, so neither moves the numbers; pavement lies only on old sand beyond the water's
reach, and the outcrops stand further out still. Mudflat patches up the bank as well as on the beach
cost 3% and were left out. The one colony below revision 3 is the weakest at dryness 0 within 24
steps (38 against 39). The defaults row was measured again after the quarry grass and the deep
threads were tightened (2026-11-07); the other revision 4 rows predate that by a figure or so.

A four-AI calibration game (`map study watershed --seed 101`, 256x256, Nicowar, Cortex, Cabino and
Maxima, game seed 1, 20,000 ticks, both maps played by the same development game binary) harvested
1,788 wheat and 316 wood with 122 worker births and 52 buildings on revision 4, against 1,122 wheat,
235 wood, 74 births and 42 buildings on revision 3; the weakest colony (team 3, Maxima) harvested 206
wheat against 112. One game varies widely: this shows no opening stall and a somewhat richer river
country, not a measured improvement.

## Limits

- Quarry faces are where the divides are, not where the homes are: at 256 with four colonies the walk
  from a swarm to its nearest face ranged from 25 to 193 steps on seeds 1-3 (spread 107 to 137,
  telemetry `watershed.quarry.nearest-walk`). Every colony's own stone is still the kit's clump behind
  its swarm, the same for all, with revision 3's outcrops at the upland edge; the faces are a bonus
  for whoever holds the divides.
- On small maps and wet settings the uplands rarely reach 20 steps from water, so few or no knots
  stand (a 128 map may have none); `dryness 0` has no pavement.
- Deep water makes swimming the trunk and the sea slower than revision 3; no AI game has measured
  swimmers.
- The richer levees change the economy a little (about 8% more growth potential); towns on the
  floodplain may see crops creep closer along the banks.
- The edge row between a levee and grass is not buildable, so a bank loses one row of building room
  at the levee's inner edge.
- Generation takes roughly 10–17% longer than revision 3, an estimate rather than a release
  measurement: a 512x512 map with eight colonies took 3.2 s of wall time (best of seeds 1–3, the
  whole command) on the revision 3 release build and 51 s on a revision 4 development build, which
  scales to 3.5–3.7 s by the development-to-release ratio of two generators unchanged since revision
  3 shipped (Hedgerow country 13.7, Stone highlands 14.6), on a machine shared with other builds.
  (An earlier figure was scaled by Braided river, which this branch rebuilt.)
- No rotation tournament or human playtest has been recorded for revision 4.

## Verification

The source definition and its request/world validators define accepted settings and finished-map
guarantees. Run the focused contracts (`GLOB2_CONTRACTS=watershed`, `MapGeneratorContracts.h`) and
the sim-diff map fingerprints, inspect small and large rectangles and resource extremes, then check
populated games with complete seat rotations. See [generator verification](verification.md) for
commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[WatershedGenerator.cpp](../../src/map/generator/generators/WatershedGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
