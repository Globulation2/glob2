# Old town

A dense medieval town of houses and paved streets inside the ruins of its wall, with farmland all
round it, open on every side.

Streets are paved but still buildable, and buildings block walking. Every building a player puts up
therefore closes a street: the players build the town's chokepoints themselves, and a tower on a
street fires over the houses into the next.

Revision 10 (Old town 2.0, November 2026) draws the town from the terrain catalogue in one of four
climates (temperate, arid, highland, volcanic). Revisions 1 to 9 drew every block as solid stone,
with grass streets, sand farm caps, sand tendril roads and beaches.

## The town

- **Layout.** The town is a disc of `city-size` percent of the half side (40–90, default 70). It is
  tiled into blocks of `block-size` (10–20, default 14), warped by `warp` (0–100, default 50).
- **Streets.** The band of `street-width` (3–7, default 4) along every cell border is a paved
  street: twice as fast to walk, nothing grows on it, and it is buildable.
- **Blocks.** Every other town block draws one of five kinds:

  | Kind | Share | What it is |
  | --- | --- | --- |
  | Rows of houses | 55% | Ridges masses filling the block to the paving |
  | Courtyard houses | 15% | A ring of building with a court inside and a door on one side |
  | Workshop yards | 10% | An open grass yard with a workshop along its back |
  | Garden plots | 10% | Grass round a well-pond |
  | Rubble lots | 10% | A standing corner of a fallen house, with rubble stone on the rest |

  Houses are Ridges, so they stop walking but not arrows.
- **The town wall** stands again as a ruin: a ring of Ridges just outside the town's edge.
  - It is breached wherever a street or a country road meets it, and wherever it has collapsed:
    about 26–39 breaches on a 256 map.
  - There are no gates. The first play found a walled town unfair to whoever started far from a
    gate.

## Plazas and homes

- **Plazas** are blocks left open, each filled by a fountain pond to a tile short of its streets.
- **The cathedral square** is the cell holding the centre. It keeps an orchard of the three fruits
  round its fountain.
- **Market squares.** `plazas` (0–4, default 2) per colony are spread farthest from the homes. They
  are paved round a grass ring holding a grove of one fruit in turn, and are given up first when the
  town is small.
- **A home is three blocks:**
  - a fountain garden, with the starting kit's wheat and wood round its pond;
  - beside it, towards the middle of town, a green for the swarm;
  - beside that, a mason's yard.
- **Homes are spread** as far apart as the town allows, and the colonies are dealt to them at random.
- **A cramped town** with too few blocks for three per home puts every yard on its home's ground
  instead.

## Stone

Stone is placed, never built in:

| Source | Amount | Scaled by `stone-amount`? |
| --- | --- | --- |
| **Mason's yard** in every home | A stack of 12 stone, the same in every home | No |
| **The robbed wall's foot** | A face of stone every 40 tiles round the wall's inner side | Yes |
| **Rubble lots** | Heaps on the fallen houses' ground | Yes |

- **Mason's yard.** Beside the stack are 8 wheat and 8 wood, so every colony's first harvest is
  within reach whatever the houses round it leave. In a cramped town the stacks hold what the
  smallest yard fits.
- **Supply.** A map carries a few hundred stone tiles instead of a quarry in every block. That is a
  change of pace: stone is now something to fetch and contest.

## The fields

- **Farm rows.** Outside the town, farm rows run along the map's axis, from `layFarm` at
  `bestFarmRows`. They are bridged every 16 tiles under `water-crossings` and `crop-crossings`
  (both on).
- **Caps and crossings.** The caps and plot rings are barren ground (`dressFarmSand`): they hold
  crops back as sand did, and can be built on. The crossings are paved.
- **Building plots.** `farm-plots` (0–6, default 3) 10×4 plots per colony are spread through the
  fields.
- **Crops.** Half the fertile row ground is under wheat and a twentieth under wood. The town's
  gardens, plazas and fountain gardens get a light share.
- **Country roads** (`tendrils`, on): one every 16 tiles round the fields' edge, a wavy paved road
  10–13 tiles long runs in through the wall's breaches.
- **Water.** No beaches are laid.

Food lies outside the town. Against revision 9, the growth potential within 24 steps of a home fell
from about 44 to 17, as the town's grass became houses and paving. Within 48 steps, which reaches
the fields, it is about the same (304 against 285).

## Checked, not assumed

The validator rebuilds the design and compares every terrain vertex. It also checks:
- that the houses and the wall are Ridges;
- that every home keeps the same mason's stone;
- that every fountain is present;
- that every colony can harvest food within 24 steps, wood within 32 and stone within 24, and has
  room for a first base;
- that every colony is walkable from the first through the streets.

## Implementation source

[OldTownGenerator.cpp](../../src/map/generator/generators/OldTownGenerator.cpp) owns this
landscape's construction, controls and validation. See the [catalog](catalog.md) for its stable
command and legacy IDs.

Related: [map generators](README.md).
