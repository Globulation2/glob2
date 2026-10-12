# Hedgerow Country

## Play contract

The opening already has a connected network of lanes and gateways. Every field,
including vacant expansion fields, can be entered without clearing wood or
learning to swim. Players can follow that network or cut a shorter approach
through a hedge. The resulting opening works in both directions: a more convenient
farm or attack route can also expose the player's territory.

Villages have broad grass for buildings, a contained renewable wheat plot and
woodlot, and a small quarry. Vacant fields offer the same irrigated ground plus
scaled crops and scattered fruit kinds. These are expansion opportunities rather
than mandatory prerequisites for first contact. Unlike Old growth, the colony
does not have to clear its way out of a forest.

## Construction and heuristics

The implementation is extensively commented in
[`HedgerowCountryGenerator.cpp`](../../src/map/generator/generators/HedgerowCountryGenerator.cpp).
It uses shared primitives without changing their behavior for other generators.

1. **Warped fields.** `squareTessellation` divides the torus into cells. Shared
   corners move with `warpCorners`, preserving valid polygons and at least 28
   tiles from each centre to its boundary lines (34 for field sizes 80/96, 22 for 48).
   The shared `rasterizeBoundaries` and `relaxWarpOutside` operations check all
   potential hedges at the requested thickness.
   If any wall enters the 41×41 square enclosing a pond’s possible growth reach,
   corner offsets are halved toward the regular lattice until the envelope is
   clear. This bounded, deterministic safeguard preserves safe warps and the RNG
   streams; it never removes hedge pixels to hide a fertile boundary. The
   `hedgerow.warp-contractions` metric reports its frequency.
   The larger budget protects the square growth probe against steeper diagonal
   boundaries. Larger fields still allow stronger
   warping; the smallest fields deliberately limit distortion to preserve farms
   and village room. Wrapping edges are ordinary edges, including distinct edges
   between the same pair of cells on a two-cell-wide torus.
2. **Connected routes first.** `carveSpanningTree` opens a winding connected graph
   through all fields. The gateways control then opens that percentage of the
   boundaries the tree left closed. Selected boundaries carry
   wood even when they have a gateway; unselected boundaries stay broadly open.
   Therefore lowering wooded share can shorten journeys beyond the marked lanes.
3. **Solid hedges, deliberate entrances.** `traceSealedPath` closes diagonal leaks;
   dilation makes cutting several tiles deep. Sand lanes cut through their planned
   gateways. Terrain is represented by corners, so after the beach pass the hedge
   mask excludes mixed-terrain road shoulders where wood cannot be placed.
4. **Permanent breaches.** Farm ponds sit inside the fields. The clearance budget
   accounts for their centred five-corner radius, the engine's 15-tile
   growth probe, hedge depth and rasterization. This geometric budget is only a
   heuristic: final validation checks the actual engine fertility field at every
   surviving hedge tile and requires exactly zero fertility.
5. **Separate production and construction.** Each field has a 29×29-corner farm
   enclosure, capped by two sand corners. Its central pond, up to 121 water corners, waters separate
   wheat and wood halves; a sand divider prevents wood spreading into the wheat.
   The surrounding 41×41 grass square interrupts the sand lanes and gives room
   for buildings. Crops inside the enclosure cannot spread across its cap.
6. **Forgiving starts.** A swarm starts north of its farm. The settlement helper
   searches a 17×13 candidate rectangle and places the full footprint and workers.
   Occupied fields retain 48 initial wheat tiles and 24 wood tiles before scaled
   crops, and one quarry tile. The normal reachable-crop backstop protects hedges
   from clearing; it cannot create an unrequested shortcut.
7. **Walking access, then team assignment.** Farthest-point selection proposes
   one arrangement per possible first field. Reject candidates that reduce the
   original minimum geometric separation. Score the remainder using shared tile
   floods around actual designed hedges, water and capped growing plots. Sample
   one walkable tile per 4×4 block, splitting ties equally. Maximize the product
   of weakest/strongest territory and nearest-rival distance ratios; break ties
   by territory ratio, contact ratio, then longer nearest contact. This avoids
   rewarding either private land or isolation alone. Strict ties retain the seeded
   arrangement. Finally randomly deal the selected sites to team indices.
8. **Equal crop stocks.** Corresponding farm tiles receive alternating two/three
   resource units. This preserves the prior mean stock while removing accidental
   per-village stock differences from deposit randomization. Crops remain ordinary
   renewable engine resources. Structural hedge stocks keep their original policy.

The placement score is a bounded approximation: at most 64 floods and a 64 MiB
local distance cache at 512×512. It excludes growing plots even where they start
partly empty, and does not model buildings or exact initial worker positions.
Final reports independently measure actual worker routes and exclusive territory.
It improves selected layouts without promising symmetry or a global fairness floor.

The farm pond provides renewable water exposure and harvest frontage, rather
than relying only on finite opening stock. The swarm moves
farther north to retain a complete dry footprint outside the larger enclosure.
The generator uses the shared simulation crop-growth rules.

Every field has one farm pond in one of the shared centrepiece designs
(`shared/Centrepieces.h`: square, round, diamond, cross, moat round a sand plinth,
twin pools, four-pool clover, oblong and round ring), all inside the pond's square
so the growth envelope above holds for each. Every home field draws the same design,
since a design's water sets how fast its crops regrow; every other field draws its
own. Two to four rough sand blotches of radius 2 to 4 break up each field's open
ground, clear of the plot, three tiles from any hedge or lane and off the swarm's
apron. The variation stays outside protected paths and construction ground.
Algae is seeded in the ponds.

A 48 field gets a smaller village so the pond's growth reach still clears its
hedges: a pond of radius 4 in a plot of 11, the swarm 16 north of the centre. Its
ponds hold less water, so its crops regrow more slowly; its warp is nearly regular.

## Controls and supported settings

| Control | Values; default | Effect |
| --- | --- | --- |
| Field size | 48, 64, 80, 96; **64** | Target pitch. Actual pitch is map dimension divided by the whole number of fields that fit. |
| Hedge thickness | 2–4; **3** | Dilation radius is value minus one: approximately 3/5/7 wood tiles deep, with rasterization variation. |
| Existing gateways | 0, 25, 50, 75, 100; **25** | Percentage of the boundaries the spanning tree left closed that also get a gateway; 100 opens every boundary. Zero remains connected. |
| Wooded boundary share | 50–100%, step 10; **90%** | Seeded chance that a whole boundary carries a hedge. |
| Wheat / wood amount | 0–300%; **100%** | Farm crops; occupied fields retain their starter guarantees. Wood amount does not thin structural hedges. |
| Stone / fruit amount | 0–300%; **100%** | Small deposits beside farms; occupied fields retain one stone tile. |
| Algae amount | 0–300%; **100%** | Algae in the ponds. |

The map must fit at least two fields along **each** axis and at most one colony
per field. Thus 128×128 supports four colonies at field size 64; larger field
settings require larger dimensions. 64-tile sides are rejected explicitly.
The normal shared limits remain 512 tiles per axis, 12 colonies and 1–8 workers.
Rectangles and odd colony counts are supported when they satisfy the field budget.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[HedgerowCountryGenerator.cpp](../../src/map/generator/generators/HedgerowCountryGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
