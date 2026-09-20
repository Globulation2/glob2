# Breachable highlands

Generator `breachable-highlands`, numeric ID 33, revision 4.

## Intended game

Start in a broad valley with renewable wheat and wood and ample dry construction
land. One open pass leads into a network of expansion valleys. Open passes already
connect the whole world: players and AIs can establish contact without clearing.
Extra ridge crossings are filled with dry wood. Clearing one opens a shortcut and
another front. Stone elsewhere remains permanent, even after swimming becomes
available. Fruit is distributed among expansion valleys rather than concentrated
at a central objective.

This is an optional generator, with no simulation, save-format, replay-version or
network-version change. Stone remains an inexhaustible resource as well as a
boundary. Towers can shoot across thin ridges: rock stops walking, not projectiles.

## Rotation tournament

A rotation tournament (six 256×256 maps, four colonies, every cyclic team rotation, four
Nicowars, 45,000 ticks) found the map even and quiet: every start pooled between 70 and 110
units at its peak, no colony was eliminated in 24 games, combat deaths stayed in single digits
per colony, and every game was adjudicated on prestige at the cap. The valley economy caps a
Nicowar colony near a hundred units before armies matter, and the pass network keeps them
apart. Raising the extra open passes from 10 to 30 through the existing control changed
none of this (pooled peaks 90 to 101, still no eliminations), so the defaults stand; whether
the slower, builder's pace is what the map should feel like is a maintainer's call.

## Construction and heuristics

The implementation is in
[BreachableHighlandsGenerator.cpp](../../src/map/generator/generators/BreachableHighlandsGenerator.cpp).
The comments explain each stage's gameplay purpose and numerical budgets.

### 1. Reserve viable valleys

A square tessellation wraps both map axes. Valley size sets its pitch; when the
map dimension does not divide the requested size, the tiling spreads the remainder
across its cells. Shared `warpCorners` perturbs the boundaries while retaining
centre clearance and separation between unrelated edges. The result is irregular
polygonal country, with broad interiors rather than narrow maze corridors.

Both dimensions must be at least 128 tiles and hold two cells. At least two cells
must remain for expansion. `spreadPockets` chooses dispersed home cells while
keeping non-home cells connected. Invalid combinations are explicitly rejected.
The initial home pattern is translated, then randomly dealt to colony indices.

The controls are not independently combinable. For example, a 256×256 map with
valley size 72 or 80 supports up to three colonies in the tested layout heuristic;
the default four colonies require valley size 64 at that map size. A 128×128 map
requires valley size 64 and one or two colonies. See the
[bulk generation study](https://github.com/Globulation2/glob2/blob/evidence/breachable-highlands/docs/artifacts/breachable-highlands/bulk-generation/README.md)
for the complete measured support table, including rectangular maps. Unsupported
requests return a diagnostic explaining the layout limit.

The home placement is a heuristic. Equal starter resources and geometric spacing
are not equal expansion opportunities, travel distances, or competitive strength.
Candidate selection uses the existing start-quality scorer; human games and seat
rotations are still needed to assess fairness.

### 2. Construct two route networks

A seeded spanning tree connects every expansion cell. Each home gets one open
exit onto that tree. A fraction of remaining expansion edges become additional
open passes; a fraction of the rest become wooded saddles. At least one edge is
reserved for a saddle. Wooded saddles preferentially cross ridges whose two valleys
have the longest existing detour in the open cell graph. Seeded ties preserve
variety. This favours useful additional fronts but does not predict actual travel
time or the best military move. Percentages round to whole edges, so small maps have fewer
distinct settings. Telemetry reports candidates, extra passes and actual saddles.

Saddles never directly enter home cells. A clearing campaign exposes expansion
territory rather than opening a surprise rear entrance into a starting town.
This also means homes remain defensible behind one exit; stalemates at those exits
are a playtest concern, not something connectivity checks rule out.

On a two-column or two-row torus, two valleys can share different edges. The
implementation retains edge identities rather than collapsing them into one pair;
a saddle across the wrap can be a distinct route between the same two valleys.

### 3. Paint permanent and clearable boundaries

Stone strokes cover every cell boundary, including shared junctions. A pass is a
short cut through that ridge at the edge midpoint. An open pass gets a sand access
lane; a saddle's entire ridge section gets wood instead of stone.

Ridge depth sets the approximate number of wood rows that must be cleared. It is
not a timer: clearing cost also depends on deposit amounts, worker abilities,
access and staffing. Pass width sets frontage after clearing. Cleared saddles are
grass and can subsequently be built on; open passes retain their sand lanes.

Terrain uses four undermap corners per tile. All corners supporting structural
stone or wood are protected from sand road painting, preventing the approach from
eroding the barrier or creating a diagonal bypass. Both stone and saddle wood
are also protected from starting-resource and cramped-start repair routines.

### 4. Put renewal where it cannot consume the roads

Each valley has a central pond, a crop area within 12 tiles, and a sand ring at
radius 14. Sand approach roads reach that ring. Wheat occupies the northern half
and the southeast quarter; wood occupies the southwest quarter, within reach of
the western town. Permanent sand spokes connect the east, west and south banks
to the ring, providing harvesting access and separating the three growing areas.
Grass lanes in revision 1 could fill with crops and let faster-growing wood
spread around the shore into wheat. Water takes precedence where spokes cross
the pond.

Each valley's pond is one of the shared centrepiece designs
(`shared/Centrepieces.h`: the map's own rough round pond, square, diamond, cross,
moat round a sand plinth, twin pools, four-pool clover, oblong or round ring);
designs with square corners are drawn one corner smaller, so none reaches further
from the centre than the rough round pond the saddle clearance was measured against.
Every home valley draws the same design, since a design's water sets how fast its
crops regrow. Two to four rough sand blotches of radius 2 to 4 break up each valley's
open ground, clear of the farm ring, three tiles from any ridge, saddle or lane, and
off the home swarm's apron (revision 5, 2026-09-16: a maintainer asked for "a few
different designs and patterns for each square" and "some terrain variety to break
up the monotony").

The nominal 3:1 allocation is a production heuristic: wheat has an additional one-in-three
growth gate in the engine, while wood does not. Equal growing areas and shorter
food trips alone still produced heavy starvation in the early AI batches. A second
wheat plot adds renewable growing area and harvesting frontage, not just a deeper
pile of opening resources. Two thirds of wheat is independently seeded in the
north, one third in the southeast; the shared growth search cannot cross sand.
Actual grass areas vary with shoreline shape and the rasterized sand lanes.
The rest of the valley remains predominantly dry, buildable grass.

The minimum centre-to-boundary clearance is 28 tiles. This reserves room for the
pond, farms, ring, construction apron and dry ridge buffer. Pond roughness is 15%.
The arithmetic is a design budget, not a substitute for testing the actual world:
`cropGrowthField` and the final engine fertility field must both report zero growth
at every saddle tile. No special no-growth flag or runtime terrain event is used.

At 100% abundance, each valley requests 84 wheat and 32 wood tiles. Homes receive
an additional 54 wheat and 24 wood tiles, including at 0%. Ambient amounts scale
the plots, algae and neutral fruit. The smaller wood quarter can saturate even at
the default home target; the starter reserve still fits, while excess ambient stock
is omitted. At high amounts all contained plots can saturate rather than consume
construction ground; requested and placed counts plus saturation events make this
visible in telemetry.

Each neutral valley gets one fruit kind, with an initial six-tile target at 100%,
on its dry apron. Types cycle across cells, so the graph has several destinations.
Nearby open grass permits inns and other outpost buildings. Home plots provide
ordinary wood so routine opening harvest need not clear a strategic saddle.

### 5. Place and protect colonies

Swarms prefer the northwestern dry apron: their centres are approximately 17
tiles west and 10 north of the pond. This favours repeated wheat deliveries while
keeping the town outside the farm ring. The wood quarter is on the southwest
shore so opening forestry stays on the town side of the pond too. The settlement
helper still relocates the complete footprint and workers when needed. This is
a placement heuristic, not a fixed travel-time guarantee.
Final crop guarantees use walking access (24 steps for wheat, 32 for wood), and
non-default abundance can trigger the existing cramped-start repair. Structural
stone and wood are protected throughout.

The final validator requires at least 32 remaining 4×4 building anchors in each
home valley. These anchors overlap: this does not promise room for 32 separate
buildings. It is a conservative rejection floor, complemented by final-map room
measurements and real AI expansion checks.

## Validation contract

The layout can be reconstructed from the request and named seed streams. Final
validation checks:

- Every designed stone tile remains stone and every saddle tile remains dry wood.
- All colonies can walk to each other through initial open passes.
- Every expansion valley has a reachable apron and retains water within its pond square.
- The north farm half and both south quarters occupy separate eight-neighbour
  components,
  even when current resources and buildings are ignored. Wood cannot grow around
  the pond into the wheat plot through a diagonal gap.
- Sealing every designed gate separates the remaining walkable regions by valley,
  including eight-neighbour diagonal movement and wraparound seams.
- Each gate's plug is connected and touches the two intended valley interiors;
  removing the wood therefore creates a real crossing.
- Every home retains the construction-anchor floor.

The defaults harness additionally removes saddle wood and confirms a shorter
colony-to-colony walking route on a retained seed. It verifies that the validator
rejects missing saddle wood and missing ridge stone, and that abundance extremes
preserve structural deposits. Golden-map and telemetry checks cover serialization,
repeatability, RNG isolation and collection-on/off equivalence.

These checks do not prove AI clearing strategy, traffic throughput, balanced
matchups or human enjoyment. Ordinary construction can block a cleared grass
saddle; a tower can suppress a pass; long supply routes can make expansion slow.
Those are intended decisions or playtest risks to inspect, not validation errors
to repair away by cutting another ridge.

## Controls

| Control | Range; default | Meaning |
| --- | --- | --- |
| Valley size | 64–96, step 8; 64 | Requested cell pitch |
| Ridge depth | 3–11, step 2; 5 | Stone thickness and wood clearing depth |
| Pass width | 6–12, step 2; 8 | Width of open and wooded crossings |
| Extra open passes | 0–30%, step 10; 10% | Share of spare expansion edges opened immediately |
| Wooded saddles | 25–100%, step 25; 50% | Share of remaining spare expansion edges filled with wood |
| Warp | 0–100%, step 10; 80% | Share of the safe vertex displacement budget |
| Pond size | 3–6; 6 | Pond radius before roughness and beaches |
| Resource amounts | Registered percentage domains; 100% | Ambient wheat, wood, algae and fruit |

There is no ambient stone control: all stone belongs to the structural ridges.
Wood amount excludes the structural saddle plugs; ridge depth controls that
investment. Unsupported size/count combinations fail with a diagnostic rather
than silently shrinking settlements or removing all saddles.

## Reproduce

```sh
scons release=1 server=0 -j4 map-generator-defaults-test map-generator-golden-test map-generator-study build/src/glob2
build/src/glob2 --generate-map breachable-highlands --seed 1 \
  --width 256 --height 256 --teams 4 \
  --output artifacts/breachable-highlands/play.map \
  --preview artifacts/breachable-highlands/play.png \
  --json artifacts/breachable-highlands/play.json
build/src/MapGeneratorDefaultsTest breachable-contracts
build/src/MapGeneratorGoldenTest breachable-golden --require-rows
build/src/MapGeneratorGoldenTest breachable-telemetry --telemetry
python3 tools/map_telemetry.py collect --generators breachable-highlands \
  --seed-start 20001 --count 16 --set width=256 --set height=256 \
  --set teams=4 --jobs 3 --out artifacts/breachable-highlands/held-out
```

Use absolute map and output paths with the structured `--run-game` interface,
which works inside its isolated profile. See [CLI](CLI.md) and
[tournaments](../tools/tournaments.md) for game, save and replay commands.

## Initial validation and tuning

See the [retained evidence](https://github.com/Globulation2/glob2/blob/evidence/breachable-highlands/docs/artifacts/breachable-highlands/README.md), including
a playable map, preview, report, regression logs and a Nicowar replay.

The first food comparison held terrain seed 1, four Maxima opponents, engine seed
19 and 32,000 ticks constant. Pond radius 4 produced 200 surviving units, 25
buildings and 64 units recorded as critically hungry across the four teams at the
cap. Radius 6 produced 211 units, 29 buildings and 44 critically hungry units.
The larger pond became the default. This is one paired tuning example, not a
statistical balance result, and food pressure remained significant.

A second run with four Nicowar opponents, engine seed 23 and the larger ponds also
reached 32,000 ticks with all colonies alive. Final populations ranged from 12 to
73, showing that viable initial geometry does not imply similar game outcomes.
Neither game reached a winner. These first runs did not include human play or
other platforms; the subsequent Linux playtest study is documented below.

The regression seed's cleared saddles shorten a colony-to-colony walk by up to
seven tiles. The larger tactical benefit is an additional crossing around a held
pass, not necessarily a dramatic peacetime shortcut between swarms. The saddle
heuristic measures detours between adjacent expansion valleys, which can differ
from shortest paths between colonies. Future tuning should inspect which passes
players actually hold and whether workers are used to open the alternative front.

## Subsequent AI playtesting

The [initial playtest report](https://github.com/Globulation2/glob2/blob/evidence/breachable-highlands/docs/artifacts/breachable-highlands/playtest/README.md)
records 36 full-length games on `therig.local`, including rejected farm-lane and
start-position variants, followed by the final three-sector farm. On four matched
seeds per AI, surviving population rose from 107.5 to 185.25 for Maxima and from
210.25 to 279.5 for Nicowar. Melee contact occurred earlier in every matched game.
All final-batch colonies survived to the 65,536-tick cap.

Raw starvation deaths increased with the larger populations; this is stronger
colony development and interaction, not a solved food economy. All games remained
unresolved at the cap. The report retains the exact settings, per-team measurements,
map/replay evidence, platform checks and remaining human-play questions.

## Shared operations

Crossings use `cellCrossing` in the tessellation's unwrapped edge frame. Candidate
saddles use `closedEdges` and cached `edgeDetours`; the generator decides which
ranked shortcuts to plant. Future crop containment uses `labelComponents`, while
`checkGatePartition` proves the final sealed ridge and plug topology. These are
shared framework operations with independent fixtures, not Highlands-only validators.
See the [extraction and paired-map evidence](https://github.com/Globulation2/glob2/blob/evidence/breachable-highlands/docs/artifacts/breachable-highlands/framework-refactor/README.md).
