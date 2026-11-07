# Tidal flats

**Tidal flats** (`tidal-flats`, numeric ID 18) is an estuary at low water: grass islands stand on a
wide expanse of grey-brown mud, threaded by pale sand channels and shell banks, with tide pools and
long lagoons over the mud and the odd lush sandbar. Every colony has a home island with a pond; small
oasis islands round the flats carry a prize each under a crop of wheat, and the island at the centre
carries the orchard of all three fruits round a rocky tor.

Revision 9 (Tidal Flats 2.0) redrew the flats with the engine's full terrain palette; revision 8 drew
them as sand. The islands are placed exactly as revision 8 placed them for the same seed (homes,
oases); the lagoons, sandbars and pools are placed again because the lagoons now fit.

## How it plays

- **The flats are open ground nobody can build on.** Mudflat walks at 160/256 of normal speed, holds
  no building and no deposit, so armies meet in the open from the first minute and there are no
  forward bases: an army on the flats fights far from any inn or tower while a defender holds a rim of
  towers on its island's edge, and expansion means taking another island whole. No barren or path
  ground is laid on the flats, and `validateWorld` refuses a buildable tile there.
- **The channels are the routes.** Sand walks at full speed, so the mud is slow ground with fast ways
  through it: a wavy ring channel through every home island (the way to the neighbours), a spoke
  through each from the centre to the outer flats (the way to the middle), an inner ring between the
  spokes when there is room, and short creeks off the ring. Every wedge has the same channels.
- **Islands regrow faster.** Unlike sand, mud does not inhibit growth, so a home island's fields round
  its pond regrow much faster than on revision 8 (see the food table). Wood did not overrun any home in
  the calibration game; no sand rim is kept round the islands.
- **Lagoons are deep in the middle.** A lagoon is a long tidal channel of water whose bed is deep
  water (`deepCore`): swimmable but slower, no algae; its shallow margin keeps algae and its beach.
  Shell banks (sand, pale look) ring every pool and lagoon on the flats, which is also what lets algae
  regrow there: algae needs solid sand within reach of its growth probe.
- **The tor is the central quarry.** The central island's pond is now a knot of rock (Ridges, outcrop
  look: impassable, towers shoot over it) with a quarry face turned to every colony, stone on plain
  grass tiles left out of the rock. Every colony walks within a step or two of the same distance to its
  nearest face (`tidal-flats.tor.quarry-walk`); with many colonies on a small tor neighbouring faces
  share tiles. The orchard's three groves stand four tiles past the tor, off every face's approach.
- **The tide changes the water.** A neap tide shrinks every pool and lagoon on the flats to 0.8 of its
  radius and bares wider shell banks and more dried hardpan; a spring tide swells them to 1.25, kept
  four tiles off every island, with wet mud further from the water. The islands, channels, homes and
  kits are the same at every tide for one seed; the water near the islands, and so their food, moves a
  little.

## How it is built

`src/map/generator/generators/TidalFlatsGenerator.cpp`; its header comment is the design brief. The
design keeps a mechanic sketch (the canonical type of each rule group: GRASS, SAND, WATER, DEEP_WATER,
MUDFLAT, RIDGE_ROCK) that every shared helper and check reads, and `generate` paints looks over it with
`composeLooks` (`shared/Sketch.h`), so looks never change a rule.

| Stage | What happens |
| --- | --- |
| Layout | Home islands on a ring at 58% of the half side, one per colony from a random phase, radius `home-island-size` percent of the half side or what the ring, the wrap and the central island leave. In one wedge's frame: `extra-islands` oases, `lagoons` (3.5 to 5.5 tiles, stretched 1.2 to 2 times, 3 tiles from other features and 6 from a home), `sandbars`, and `tide-pools` per 128x128 of flats, stamped into every wedge. Revision 8 drew lagoons 8 to 14 tiles, which fitted no wedge at the defaults, and placed them after the sandbars. |
| Channels | From their own stream: the ring (a long wave of one or two per wedge plus a ripple about every 30 tiles of arc, 3.4 tiles across), the spoke (two waves, 3 across), the inner ring (when the gap between the central island and the homes is 12 tiles or more) and three creeks per wedge tapering to their tips, every one kept inside its wedge. |
| Tide | `tide` scales pools and lagoons; spring water beyond the mean's only where it keeps 4 tiles from every island. |
| Sketch | Mud, then channels as sand, islands as grass, water; shell banks within 2 (neap 3) tiles of the water on the flats, their width swaying by up to 1.2 tiles on noise sampled in the wedge frame; beaches (`layBeaches`); the tor's rock (`rockVertices`); deep cores of the lagoons two vertex steps from any other ground, specks under four vertices left shallow. |
| Tor | A rough disc 35% of the central island's radius (2.5 to 7 tiles); each colony's quarry face is the rock within a tile of the rim point facing its home (`quarryNear`), stocked with stone before the colonies settle; `quarryApproach` keeps the orchard off it. |
| Looks | Wet mud (MUD) in blotches beyond the shell banks, hardpan on 5 to 34% of the flats far from water by tide, shell banks and patches of the channels pale sand, sandbars and oases lush grass, neap lagoon beds dark water, the tor outcrop. Home islands and the central island stay plain grass, which the starting-crop guarantee reads. |
| Resources | Every home's kit (40 wheat and 30 wood beside the pond, a quarry towards the centre), unscaled, then 4% wheat and 2% wood of the island on its most fertile ground and an outcrop, scaled; every oasis under wheat with a fruit or stone prize; the orchard; algae; the starting-crop guarantee. |

`validateWorld` rebuilds the design and requires: every home's pond; no buildable tile on the flats;
every deep vertex surrounded by water; every quarry face stocked (`quarryFault`); every colony walking
to colony 0, and the shore of every island, the central one included, reached on foot, with water,
buildings and every resource blocking.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Home island size | 14–26 (20) | Home islands' radius as a share of the half side, shrunk to fit. |
| Extra islands | 0–10 (6) | Oases per colony. |
| Sandbars | 0–16 (8) | Lush green patches per colony, room for an inn and a tower. |
| Tide pools | 0–12 (8) | Pools per 128x128 of flats. |
| Lagoons | 0–4 (3) | Deep-bedded lagoons per colony, as many as fit. |
| Coast roughness | 0–100 (50) | How much every coast wobbles. |
| Central island | on/off (on) | The central island with its orchard and tor; off, the middle is flats. |
| Tide | Random, Neap, Mean, Spring | Water on the flats 0.8, 1 or 1.25 of its radius, shell banks and looks; Random picks from the seed. |
| Wheat, wood, stone, algae, fruit amount | 0–300% (100) | Ambient fields, outcrops, prizes and algae. Each home's kit and the tor's faces are guaranteed. |

## Food

Growth potential (`growth_potential.c`: the engine's crop growth chance summed over farmland, counting
sand and barren ground as inhibitors), 256x256, 4 colonies, seeds 1–3, revision 9 against revision 8.

| Variant | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| Revision 8 | 109 | 22.1 (21) | 24.4 (23) |
| Revision 9, defaults (random tide) | 204 | 30.7 (30) | 44.3 (41) |
| Neap tide | 194 | 30.6 (30) | 42.7 (40) |
| Mean tide | 199 | 30.6 (30) | 43.5 (41) |
| Spring tide | 207 | 30.7 (30) | 44.6 (42) |

Nothing round an island inhibits growth any longer: revision 8's sand flats refused a share of every
growth roll whose mirrored probe landed off the island. The lagoons, which now exist, add water within
reach of some islands. The tide moves the total by about 6%; the weakest variant (neap) is still 38%
above revision 8 within 24 steps.

A four-AI calibration game (`map study tidal-flats --seed 101`, 256x256, Nicowar, Cortex, Cabino and
Maxima, game seed 1, 20,000 ticks, both maps played by the same master release binary) harvested
1,627 wheat and 323 wood with 118 worker births and 52 buildings on revision 9, against 1,386 wheat,
270 wood, 99 births and 47 buildings on revision 8; the weakest colony (Cortex) harvested 291 wheat
against 288. No home island was overgrown with wood at the end. One game varies by up to 40%.

## Limits

- The tor's faces are fair by construction only up to rasterization and a rough outline: the walk to
  the nearest face differed by one step between colonies in the default map. With many colonies on a
  small map, faces share tiles and are not owned.
- Algae regrowth relies on the shell banks; no algae haul has been measured in a game.
- Lagoons are placed before the sandbars, so a crowded wedge drops sandbars before lagoons; set
  `lagoons` to 0 for revision 8's sandbar count.
- Swimming through the deep lagoon beds is slower than revision 8's shallow water; no AI game has
  measured swimmers.
- No rotation tournament or human playtest has been recorded for revision 9.

## Verification

Run the focused contracts (`GLOB2_CONTRACTS=tidal-flats`, `MapGeneratorContracts.h`) and the platform
golden rows, inspect small and large rectangles and resource extremes, then check populated games with
complete seat rotations. See [generator verification](verification.md) for commands, revisions,
save/replay checks and evidence requirements.

## Implementation source

[TidalFlatsGenerator.cpp](../../src/map/generator/generators/TidalFlatsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
