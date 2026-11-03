# Braided Delta

**Braided Delta** (`braided-delta`, numeric ID 36) is an inland delta, after the Okavango: a river
that never reaches the sea. It leaves a rock range through a gorge, runs down a marsh-lined
panhandle, and at the apex spreads into one great swamp in the middle of a desert basin. Out from
the apex: the permanent swamp, papyrus marsh threaded by channels that split and rejoin, deep
lagoons, and islands of grass round bare salt-pan hearts, one of them a great island with an
orchard; the seasonal floodplain, loam levees and lush grass along shrinking channels; the fringe,
where the channels die out in dry sandy beds among sand ridges; and the desert, dry grass, linear
dunes, clay pans and rocky kopjes, round the wrap to the range.

Revision 6 replaced the earlier ladder of parallel channels and identical islands. The coastal
delta first drawn for the rebuild (trunk, fan, sea) was set aside as too close to
[Watershed](watershed.md); the inland delta also stays clear of [Bajada](bajada.md)'s row of
per-colony fans by making one shared wetland the middle of the map.

## How it plays

- **Water and food in the middle, room at the edge.** Colonies start round the swamp's rim on dry
  ground, each in a clearing of plain grass with a starter wheat patch and a copse. Floodplain
  farms, island woods and the great island's orchard lie inward; building room and kopje stone lie
  outward in the desert.
- **The panhandle's river requires bridges or swimming.** Its deep core and shallow banks
  block walking between the boardwalk bridges (River crossings). Trained units can swim
  through either depth and across their transitions. Shallow shoals line the banks; inside
  the range the river is deep against the rock.
- **The swamp is slow, not closed.** Marsh is walkable at 5/8 speed and takes no buildings or
  crops. The swamp's channels are shallow: forded where the fords are, swum by trained units.
  Lagoons are deep pools off the channels.
- **Growth stays where it is put.** Marsh, sand and gravel grow nothing, so the swamp's islands and
  the floodplain are the renewable ground.

## How it is built

`src/map/generator/generators/BraidedDeltaGenerator.cpp` draws everything in a frame along the
flow (u downstream, v across) and transposes it onto the map, so the river runs the long way and
picks its axis on a square map.

| Stage | What happens |
| --- | --- |
| Range | Ridge rock along the wrap, thickened round the gorge, with a scree and boulder skirt. |
| Panhandle | The river rises inside the rock and meanders to the apex; papyrus marsh along it. The apex sits at least 24 tiles below the range. |
| Fan | An ellipse round the apex whose edge wanders with its bearing. Primary arms leave the apex across it, fork (`growBranches`), meander and thin outwards; past the wet edge they carry on as dry sandy beds. Side channels braid neighbouring arms in the swamp. |
| Zones | A tile's zone comes from its fan radius, reaching out along the channels in fingers: swamp, floodplain, fringe, desert. |
| Water | Lagoons, seasonal pools on the floodplain, and rare desert pans that hold water. Deep cores in the panhandle's river and the lagoons. |
| Swamp | Marsh, except where the island noise rises; one great island along the flow. Channel banks are marsh, not sand. |
| Crossings | Boardwalk bridges over the panhandle; sand fords across the channels on a spanning set of the land plus loops; then a repair that fords any piece still cut off by the cheapest crossing of shallow water (or a lagoon), never the panhandle's river. |
| Homes | Spread by walking distance round the swamp's rim, on squares of pure grass with room; desert features are laid only afterwards, outside every home's clearing. |
| Resources | Starter kits first, then the great island's orchard, a quarry at each kopje, wheat on levee loam, woods on lush ground and islands, and algae. |

The design is built once per request and reused by the request check, generation and validation
(`cachedDesign`).

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Flood | Dry, Normal, Wet (Normal) | The fan's size and how much of it is permanent swamp: roughly half, three fifths or three quarters of the map. |
| Distributaries | Few, Normal, Many (Normal) | Primary arms leaving the apex. |
| Islands | 0–100 (50) | How much of the swamp is island rather than marsh. |
| River crossings | 1–3 (2) | Boardwalk bridges over the panhandle's river. |
| Dunes | 0–100 (50) | Desert dune cover. |
| Wheat / Wood amount | 0–300% (100) | Levee wheat and lush-ground woods; starter kits stay fixed. |
| Stone amount | 0–300% (100) | Each kopje's quarry. |
| Fruit amount | 0–300% (100) | The great island's groves; 0 removes the orchard. |
| Algae amount | 0–300% (100) | Algae in the shallow water. |

Maps must be at least 128 tiles each way; every size from 128 to 512 and every colony count the
lobby offers generate. Tags: natural terrain, river, swamp, desert, islands, contested centre.

## Checked, not assumed

`validateWorld` rebuilds the design and checks the finished world:

- the terrain is the design's, corner for corner;
- every colony can walk to colony 0;
- in each stretch of the panhandle between its gorge, bridges and apex, units without swimming
  cannot reach the other bank (the bank is the side of the river's middle line);
- no piece of 48 or more walkable tiles is cut off from colony 0 by terrain;
- every colony can harvest food within 24 steps and wood within 32, with 16 building sites.

`braidedDeltaChecks` in `MapGeneratorContracts.h` generates six fixtures (128 to 512, Dry to Wet,
4 to 12 colonies) and damages each, with a sand causeway and with stone over every tile, to show
the validator refuses them. Fairness is by measurement: starts are spread, not stamped, and the
map carries no fairness tag.

## Implementation source

[BraidedDeltaGenerator.cpp](../../src/map/generator/generators/BraidedDeltaGenerator.cpp) owns this
landscape's construction, controls and validation. See the [catalog](catalog.md) for its stable
command and legacy IDs.

Related: [map generators](README.md).
