# Breachable highlands

## On this page

- [Intended game](#intended-game)
- [Construction and heuristics](#construction-and-heuristics)
- [Validation contract](#validation-contract)
- [Controls](#controls)
- [Reproduce](#reproduce)
- [Shared operations](#shared-operations)
- [Verification](#verification)
- [Implementation source](#implementation-source)

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

Each tile's terrain comes from its four corner vertices. All vertices supporting structural
stone or wood are protected from sand road painting, preventing the approach from
eroding the barrier or creating a diagonal bypass. Both stone and saddle wood
are also protected from starting-resource and cramped-start repair routines.

### 4. Put renewal where it cannot consume the roads

Each valley has a central pond, a crop area within 12 tiles, and a sand ring at
radius 14. Sand approach roads reach that ring. Wheat occupies the northern half
and the southeast quarter; wood occupies the southwest quarter, within reach of
the western town. Permanent sand spokes connect the east, west and south banks
to the ring, providing harvesting access and separating the three growing areas.
Sand lanes prevent crops from closing the approach and keep faster-growing wood
from spreading around the shore into wheat. Water takes precedence where spokes cross
the pond.

Each valley's pond is one of the shared centrepiece designs
(`shared/Centrepieces.h`: the map's own rough round pond, square, diamond, cross,
moat round a sand plinth, twin pools, four-pool clover, oblong or round ring);
designs with square corners are drawn one corner smaller, so none reaches further
from the centre than the rough round pond the saddle clearance was measured against.
Every home valley draws the same design, since a design's water sets how fast its
crops regrow. Two to four rough sand blotches of radius 2 to 4 break up each valley's
open ground, clear of the farm ring, three tiles from any ridge, saddle or lane, and
off the home swarm's apron. This adds variety without changing the farm envelope.

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
scons release=1 server=0 -j4 engine-tests map-generator-golden-test map-generator-study build/src/glob2
build/src/glob2 map generate breachable-highlands --seed 1 \
  --width 256 --height 256 --teams 4 \
  --output artifacts/breachable-highlands/play.map \
  --preview artifacts/breachable-highlands/play.png \
  --report-file artifacts/breachable-highlands/play.json
python3 test/run_tests.py --filter 'MapGeneratorDefaults/*'
build/src/MapGeneratorGoldenTest breachable-golden --require-rows
build/src/MapGeneratorGoldenTest breachable-telemetry --telemetry
python3 tools/map_telemetry.py collect --generators breachable-highlands \
  --seed-start 20001 --count 16 --set width=256 --set height=256 \
  --set teams=4 --jobs 3 --out artifacts/breachable-highlands/held-out
```

Use absolute map and output paths with the structured `game run` interface,
which works inside its isolated profile. See [CLI](cli.md) and
[tournaments](../tools/tournaments.md) for game, save and replay commands.

## Shared operations

Crossings use `cellCrossing` in the tessellation's unwrapped edge frame. Candidate
saddles use `closedEdges` and cached `edgeDetours`; the generator decides which
ranked shortcuts to plant. Future crop containment uses `labelComponents`, while
`checkGatePartition` proves the final sealed ridge and plug topology. These are
shared framework operations with independent fixtures, not Highlands-only validators.
See the [extraction and paired-map evidence](https://github.com/Globulation2/glob2/blob/evidence/breachable-highlands/docs/artifacts/breachable-highlands/framework-refactor/README.md).

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[BreachableHighlandsGenerator.cpp](../../src/map/generator/generators/BreachableHighlandsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
