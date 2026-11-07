# Old random (Shattered Coast)

**Old random** (`shattered-coast`, numeric ID 7) is the game's original random map: grass, sand and
water painted at random in requested shares and smoothed into blobs, so a coastline is shattered at
every scale into coves, ponds, necks of land and specks of island. It has no large-scale structure
at all; that patchwork is its character. Colonies settle on the widest grass, each with a cleared
meadow, and find their first deposits by looking along the eight compass directions.

Revision 9 (Shattered Coast 2.0) draws the same patchwork with the engine's full terrain palette:
dark water in the bigger bays, mud in the sheltered coves, pale beaches and dunes, lush meadows and
parched necks of grass, sea stacks and quarried knots of rock. Revision 8 drew everything from
grass, sand and water.

## How it plays

- **The same map underneath.** For a given seed, revision 9 paints, smooths, seats colonies, lays
  home deposits, repairs openings and stocks expansion clumps exactly as revision 8 did. The 2.0
  pass then repaints only ground nobody stands on: no tile holding a deposit, building or unit
  changes its rules, so every deposit and start is where it was.
- **Bays have deep middles.** Water at least four steps from any shore in the bigger bays and lakes
  is deep: swimmable but slower, holds no algae, and waters the land like water. Every shore keeps
  two tiles of plain shallows, so algae, beaches and swimming access work as before. Crossing a big
  bay takes longer than in revision 8: a change of feel.
- **Coves are muddy.** Part of the beach round small, sheltered waters (ponds, coves, lagoons of at
  most 500 vertices) is mudflat: slow, unbuildable, holds nothing, and unlike sand it does not hold
  crops back, so the fields beside those ponds grow a little better. Mudflat gives algae no shore
  support, so the open shores, where algae live, stay sand, and so does every beach within 16 tiles
  of a home, where the sand is the containment between the home and its overgrowth.
- **Rock.** The smallest islands (at most ten land vertices, nothing on them) become sea stacks,
  and knots of rock stand in the wider grass at least 24 tiles from every home. Rock is impassable
  and permanent; towers shoot over it. Some knots carry a **quarry face** on the side facing the
  nearest home: permanent stone on grass (how many follows `stone-amount`). No rock ever cuts
  walkable ground off; a knot or stack that would is not drawn.
- **Looks only.** Lush grass in broad meadows, dry grass on thin necks, pale beach sand and dune
  sand change nothing in play; the climate changes only artwork.

## How it is built

`src/map/generator/generators/ShatteredCoastGenerator.cpp`; the header comment and the constants'
comments record why. Revision 8's stages run unchanged, then the coast pass:

| Stage | What happens |
| --- | --- |
| Mix and paint | `fitTerrainMix` simulates the paint-and-smooth process on a scratch map to find painting shares that land on the requested ones; every vertex is then painted at random. |
| Smooth | `smoothing` passes; a terrain over its share spreads only rarely, which keeps water broken into many small pools. |
| Beaches and starts | `layBeaches`, colonies on the widest grass patches (relaxing the spacing on crowded maps), meadows, beaches again; swarms and workers. |
| Home deposits | Wheat, wood, stone and a reinforcing crop along compass directions, algae on the water, `smoothResources`; the starting-resource guarantee and buried-start relief. |
| Expansion clumps | Spaced wheat, wood, stone and algae clumps outside a 32-tile home buffer, chosen by rule group. |
| Rock | Knots: lenses at least two tiles thick on vacant grass with two tiles of vacant grass round them, at most one per 8,192 tiles, 26 apart; quarry faces on 75% of them at the default stone amount; sea stacks on 60% of the qualifying islands; a parting check drops any knot or stack that cuts ground off. |
| Deep cores | `deepCore` at four steps, opened, specks under 12 vertices dropped, never at a corner of a tile holding algae. |
| Mudflats | Beach vertices of bodies of at most 500 water vertices where a noise field passes, outside 16 tiles of every swarm. |
| Looks | A mechanic sketch (canonical types) with looks painted by `composeLooks`, by climate; quarry stone stocked and checked with `quarryFault`. |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Water, sand, grass weight | 0–100 (40, 4, 60) | The painted shares, before beaches. |
| Smoothing | 1–8 (3) | Blob scale, from speckle to broad coasts. |
| Colony meadows | on/off (on) | A cleared grass square round each colony. |
| Deep water | on/off (on) | Deep cores in the bigger bays and lakes. Routes on foot are the same either way. |
| Mudflats | on/off (on) | Mud on the beaches of small, sheltered waters. |
| Rock outcrops | on/off (on) | Sea stacks and rock knots with quarry faces. |
| Climate | Random, Temperate, Steppe, Tropical | Looks only: beach, dune, mud, deep water and rock artwork and how much grass is lush or dry. Plays identically. |
| Wheat, wood, algae amount | 0–300% (100) | Home deposit area and expansion clumps. |
| Stone amount | 0–300% (100) | Home deposit area, expansion quarries and how many knots carry a quarry face. |

## Food

Growth potential (the engine's crop growth chance summed over farmland, by rule group: sand inhibits,
mudflat does not, deep water waters like water), 256×256, 4 colonies, seeds 1–6, revision 9
against revision 8 on the same seeds (the starts are identical):

| Variant | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| Revision 8 | 1470 | 41.5 (29) | 186.3 (144) |
| Revision 9, defaults | 1576 | 42.8 (29) | 199.3 (151) |
| No deep water | 1576 | 42.8 (29) | 199.3 (151) |
| No mudflats | 1466 | 41.5 (29) | 185.7 (143) |
| No outcrops | 1580 | 42.8 (29) | 200.0 (152) |
| Each climate | 1576 | 42.8 (29) | 199.3 (151) |

Mudflats carry the gain: total food +7%, +3% within 24 steps and +7% within 48. Without them
revision 9 matches revision 8 to within 0.5% (the rock knots and stacks take a few tiles of
grass). Deep water and the climate change no food at all.

## Limits

- The pass costs about half again revision 8's generation time in the same build (512×512: 8.2 s
  against 5.5 s with every layer off, unoptimised development build); revision 8 itself takes
  0.4 s in a release build.
- Still texture first: nothing guarantees a colony water, room or a fair share beyond what revision
  8 guaranteed (the starting floor and the lobby's best-of-several seeds).
- Deep cores need water at least seven vertices across, so at the default smoothing only the
  bigger bays get one (a few hundred vertices on a 256 map); at smoothing 6 and above they are
  common.
- Mudflat gives algae no shore support, so algae regrow less round the muddy coves; the open
  shores keep their sand.
- Knots need wide vacant grass away from the homes; crowded or resource-rich maps place fewer than
  wanted (telemetry `shattered-coast.knots.shortfall`). Quarry faces are ambient: no colony is
  promised one, and the walk to the nearest varies by tens of tiles between colonies.
- One calibration map (seed 101, 4 AIs, three game seeds) showed Nicowar, Cortex and Maxima within
  single-game noise and Cabino's seat a little poorer in all three games; no rotation tournament or
  human playtest has been recorded for revision 9.
- The optional baseline check in the Shattered Coast expansion test
  (`GLOB2_SHATTERED_BASELINE_MAP`) compares exact terrain with a revision-7 map and so no longer
  holds; deposits and starts still match.

## Verification

Run the focused contracts (`GLOB2_CONTRACTS=shattered-coast`, `MapGeneratorContracts.h`) and the
generator's default case, sweep every shape and colony count against revision 8, and compare growth
potential. See [generator verification](verification.md) for commands, revisions, save/replay
checks and evidence requirements.

## Implementation source

[ShatteredCoastGenerator.cpp](../../src/map/generator/generators/ShatteredCoastGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
