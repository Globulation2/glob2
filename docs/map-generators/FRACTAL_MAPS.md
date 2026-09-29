# Fractal maps and recursive geometry

## Play contract

**Sierpiński Gardens** (`sierpinski-gardens`, numeric ID 49, revision 8) surrounds a
central orchard island with a rectangular lake, eight first-level districts, and
smaller central-third lakes in eligible districts. Home reservations stop further
cutting. Opposing causeway pairs provide distinct island approaches; the outside
land remains joined across the torus. Every lake bank offers contained farms and
nearby construction land, and garden beds, square sand paths and sparse copses fill the
meadows the recursion leaves between them.

![Sierpiński Gardens at 256×256 with four colonies](images/sierpinski-gardens.png)

Engine map preview: seed 20001, default controls, 256×256, four colonies.

**Hilbert River** (`hilbert-river`, numeric ID 50, revision 8) follows a continuous
Hilbert curve. Broad pockets between folds hold colonies. Walking around the river
ends and across map seams remains possible; direct bank crossings shorten those
routes, and swimming offers further alternatives. Orchards sit by the mandatory and
regional crossing courts. Farms follow both banks of every segment, garden beds sit in
the pockets the folds enclose, and square sand paths join them.

![Hilbert River at 256×256 with four colonies](images/hilbert-river.png)

Engine map preview: seed 20001, default controls, 256×256, four colonies.

Both maps favor establishing an economy before contesting expansion. They permit
different surroundings rather than promise tile symmetry. Defaults are 256×256 and
four colonies. Supported inputs are 128–512 tiles on each axis and 2–8 colonies,
**subject to geometric fit**. In particular, dense 128-tile maps can lack room for
the requested number of homes. Every failed request is retained in studies; the
CLI does not substitute another seed. The evidence report records the measured
support envelope and any unresolved failures.

### Controls

| Gardens control | Range | Default | Meaning |
| --- | --- | --- | --- |
| `maximum-nesting` | 1–4 | 3 | Maximum lake nesting, stopped by district size or homes |
| `minimum-district-side` | 24–48, step 4 | 24 | Minimum side of children created by subdivision |
| `lake-size` | 75–125%, step 5 | 100% | Size relative to the exact central-third opening |
| `major-crossing-pairs` | 1–3 | 2 | Opposing pairs, giving 2–6 island approaches |

| River control | Range | Default | Meaning |
| --- | --- | --- | --- |
| `maximum-fold-depth` | 1–4 | 3 | Maximum uniform Hilbert order |
| `river-width` | 4–10 | 6 | Full stroke width in undermap-corner units |
| `minimum-land-spacing` | 24–40, step 2 | 28 | Bank separation before beaches |
| `local-crossings` | 0–2 | 1 | Optional shortcuts per eligible recursive parent |
| `major-shortcuts` | 0–4 | 2 | Optional regional shortcuts over the whole map |

Hilbert always retains one direct bank crossing with both optional budgets at zero.
It is mandatory by the map's play contract, although the open-ended river does not
necessarily disconnect land without it. Optional crossings must improve graph
travel; counts are upper budgets and reported shortfalls are possible. At 256×256,
spacing and homes commonly reduce order 3 to order 2. Maximum nesting is not an
instruction to destroy home ground to achieve a number.

Both have 0–300% wheat, wood, stone, algae and fruit controls. Home wheat/wood retain
an opening minimum at zero; ambient bank crops do not. Home stock density ranges
from 50% to 100%, and ambient bank density from 0% to 100%. Opening quarries retain
six tiles per module at zero stone amount. Objective-court fruit, shore wheat spots,
ambient copses, distant quarries and algae scale with their amounts. Plot area and
permanent lanes do not grow with abundance.

### Garden beds and the open land

Until 2026-09-16 the land between the design's features carried nothing: deposits
lived only inside objective courts and contained plots, and both maps measured
around 1,400–1,900 resource tiles against a 7,800 median across the 48 playable
landscapes, third and sixth lowest of all of them. Four things now fill it, and
none of them may touch a home module, a crossing, an objective court or an existing
plot:

- **Bank plots** sit against the water, sharing the shore's own sand as their cap on
  that side, because how fast a crop regrows depends on how much water is near it and
  a plot laid a dozen tiles inland was not worth the walk. They slide along the bank
  and retry two tiles smaller before reporting an omission, instead of refusing on the
  first blocked box, and a site that would leave fewer than forty crop tiles is passed
  over rather than stamped. Gardens proposes all four banks of every smaller lake (was
  two); Hilbert proposes one plot on each bank of every segment, large and square-ish,
  since a plot's sand rim is set by its perimeter and many small plots cost a tenth of
  the map in rim alone. Three banks in four carry food.
- **Garden beds** are the home module at a quarter scale — a square pool, a ring of
  crops it waters, a sand cap that contains them — dropped on a jittered lattice
  through the open land, sized to the pockets between Hilbert's folds. They are the
  water away from the one fractal channel, in the same square-on-a-grid language.
- **Ambient copses** are sparse: timber on one cell in eight of the same 8-lattice the
  objective courts use, scattered rather than gridded, and only on dry ground where the
  growth probe can never find water, so a copse stays the size it was planted — something
  to go and take, not something that takes the map. On fertile ground there is simply no
  copse. Renewable food stays inside the sand-contained plots and the shore wheat spots
  described below. The gaps are also what keep each module's expansion anchors
  available, which the finished-world check requires.
- **Stone** is not scattered at all. A handful of quarries — two plus half the colony
  count — go as far from every home as the map allows, each a clump the size of a
  building court, typically sixty tiles or more from the nearest module. The six-tile
  opening quarry inside every module is what keeps a colony from being stuck before it
  gets there. **Fruit** fills the objective courts instead, at full density: a court
  sits on the central island or beside a crossing, which is where a prize worth
  fighting over belongs. Scattering both across the whole map read as confetti and gave
  neither of them anywhere to be.

Measured over 128/256/512 maps at two to eight colonies, six seeds each, this takes
Hilbert from 1,871 to 3,519 resource tiles and Gardens from 1,422 to 3,137, against
a 7,764 median across the 48 playable landscapes. They stay deliberately short of that
median: most of it is timber, and on a map with water across its open land every scattered
tree is a future forest, so these maps lean on food instead — about two tiles of wheat for
every one of timber (Hilbert 1,966 to 990, Gardens 1,560 to 942). Water rises from 10.2% to
13.8% and from 12.7% to 18.5%, still well
under the field's 29.8%: these are maps of land
cut by one fractal channel, and the median belongs to archipelagos. Widening the
channel was tried and rejected — a seven-tile river leaves no room for a crossing
court on a 128 map, which costs the smallest supported size entirely — so the extra
water comes from the beds instead. `river-width` and `lake-size` remain the controls
for players who want more.

Inside a module, the two irrigation strips are five rows deep and continuous: the sand
aisles that limit crop width stop at the crops, where they used to run on through the
water and leave each strip as four short ponds. The southern strip was once two rows deep,
which read dry and fed its plot slowly, since regrowth depends on nearby water.

### Garden paths, causeways and shorelines

Both maps are formal gardens, and the connections between their parts are drawn that way.
`gardenPaths` joins every recorded feature — home modules, beds, bank plots, lakes and
crossing landings — with a spanning tree built shortest edge first. Each edge is an L or a
Z of straight horizontal and vertical legs, never a staircase or a diagonal, laid only over
open grass, so a path can lead to a feature but never cross one. Paths are sand two corners
wide, the same slim line a module's rim is drawn in, which also makes them permanent: no
crop can grow over sand, so the ways between the gardens survive overgrowth. A crossing's
two landings count as already joined; each landing is the last grass tile on the bridge's
axis, and a path must arrive along that axis, centred on the bridge, so it runs straight on
from the causeway rather than turning onto it.
At 256×256 with four colonies Gardens joins every feature; Hilbert leaves the few pockets
that river folds seal off without a bridge, which a path could only reach through water.

Gardens' causeways run horizontal and vertical only. A pair is a causeway and its mirror
along its own axis, so the third pair is two parallels rather than a diagonal. On a 128 map a
home's court can block a whole axis; only then does the original angled search return, for
the missing axis alone, recorded as `sierpinski.crossings.angled-fallback`. On both maps a
crossing's protection mask covers its whole stroke, but its sand is painted only over water:
nothing is drawn onto the land beyond the lake, or across Gardens' orchard island.

Grass never touches water. Both designs end with a full `layBeaches` pass once the last
terrain is cut, because beds, plots and paths are all laid after the pass the home modules
use. A bed lays four rows of grass round its pool so the beach can take the innermost and
leave three rows of crops.

Wheat also grows along the shore of each map's centrepiece — Hilbert's river, Gardens'
central lake — in 3×3 spots, at least twenty tiles apart and never on anything the design
owns. They sit right against the beach and grow and spread like any
farmland; the beaches and paths round them are sand, so a spot can spread along the bank
without shutting a route. Gardens' four bank plots
round every smaller lake are three wheat and one timber, the seed choosing which bank.

Clean, neat and tidy is these maps' aesthetic, and the paths are held to it. Every path
meets the sand or water of the feature it joins, not the edge of that feature's recorded box,
which is larger than its visible rim, and it meets it flush: both of its tiles stop against
the rim, so nothing juts past a join and no join is one tile wide. A path keeps a tile of
grass between itself and anything it passes, so two paths or a path and a rim never merge
into a wider smear; bends are full two-by-two corners. A route that cannot be drawn that way
is not drawn. A crossing's two landings are joined to the tree separately, so every bridge is
met by a path on both banks.

No growth is frozen anywhere on either map: generated maps may not use the engine's saved
no-growth flag, which is for hand-made scenarios such as the tutorial, and the shared
structural check refuses any generated world that has one. Everything that must not spread
is contained by terrain. Plots and beds are sand-capped. Copses sit only on dry ground and on
one lattice cell in eight. Quarries need nothing: stone never extends. Before any of this,
one 50,000-tick game per map (map seed 3001, game seed 19, four AIs) took wood from 2,858 to
17,682 tiles on Gardens and 2,882 to 20,737 on Hilbert — 27% and 32% of the map, one forest of
9,132 tiles.

The home module's rim is straight-edged, two corners of sand on every side, including
beyond each water strip, so it is the same width as the paths that meet it. A
ragged, frayed rim was tried so colonies would not all open inside an identical stamped box,
and removed: it is exactly what a formal garden should not have, and a path cannot meet it
flush.

### Home gardens

Since revision 8 a map draws its homes in one of four formal garden designs
(`HomeDesign`, from the `fractal-home-design` stream, recorded as `fractal.homes.design`).
Every home on the map gets the same design; the next map may draw another. All four are
tidy and square, mostly wheat with a little timber, and all keep the same 24×24
construction court, the service apron with wheat along its north edge, the starting point
and the 56-tile footprint. Reservation, settlement and the finished-world checks are
therefore the same for every design.

| Design | Drawing |
| --- | --- |
| Parterre | Long canals north and south of the court, crops on their inner banks, timber bays at the ends of the north bed (the only design before revision 8) |
| Horseshoe | One canal bent round the north, east and west, crops inside the bend, timber at the tips of its arms, a causeway north across it, and open lawn to the south |
| Cloister | A canal all the way round, crops on its inner bank, timber in the southern corners, a causeway across it on each side, and the quarry as a well in a corner of the court |
| Four beds | A short north canal feeding the wheat along the apron, and a square pool bed in each corner of the module, one of them timber |

![The four home garden designs on Sierpiński Gardens (top) and Hilbert River (bottom)](images/fractal-home-designs.png)

The horseshoe and cloister canals are narrower than a parterre's and keep two corners of
lawn outside their sand, with a causeway beside the starting point. A home in a tight
pocket between Hilbert's folds builds its first expansion on the ground round the module's
edge. A canal out to the rim, with no northern way out, left such a home no expansion room
within the 48-step check, where the open parterre had passed. Each design's crops are
checked on the design itself: a flood over pure grass from its crops must stay inside the
home's reservation, or the map fails. The opening quarry's tiles are held like an objective
court, so a garden path running in to meet the court across open lawn cannot pave them
first. Before revision 8 a path could, and some parterre maps failed their quarry check.

### Home and start policy

`generators/FractalMapSupport` contains the economic policy shared by these two
maps; it is deliberately outside the neutral geometry toolkit. A home module
occupies roughly 56×56 corners. It contains 24×24 pure-grass construction tiles, a
garden of wheat with a little timber in one of the designs above, harvesting aisles, a
quarry, and access to outside building ground. Sand caps contain most crop edges.
A grass service apron directly beside wheat fits feeding buildings. Wheat may grow onto the
apron as farmland does, but a single row of sand corners along its foot keeps it out of the
court below. The near timber bays shorten inn construction and upgrade deliveries. No
emergency resource clearer may carve another crossing.

A bounded maximin search tries up to 64 initial anchors, with toroidal separation.
Gardens prefers complete first-level districts where they are large enough; narrow
maps use wrapped lattice anchors. Hilbert reduces the entire order when necessary
for homes, preserving entry/exit continuity. One spare module may be furnished
where it fits without reducing order. It becomes an expansion farm/court.

After terrain and resources are finished, start selection checks actual renewable
harvesting frontage, quarry access, expansion anchors, and walking/swimming contact
using engine movement predicates. It exhausts the small set of ways to omit a spare,
favoring the weakest economy and more even nearest-rival walking distances. Selected
sites are then randomly dealt to team indices. The completed world must connect
all colonies and fit ten separate 4×4 buildings per home on a six-tile grid, leaving
two-tile circulation lanes. A separate alternative layout must fit at least six full
6×6 upgrade envelopes with circulation; smaller initial buildings can occupy those
reserved cells without blocking their future growth. Counts of overlapping anchors
alone are not this proof.

## Shared primitive interfaces

### Reusable operations on existing toolkit modules

The generators compose these operations; home sizes, crop budgets, resource guarantees,
scoring weights, and rejection messages remain economic policy in the small shared
`generators/FractalMapSupport` layer.

| Module | New operation | Reuse example |
| --- | --- | --- |
| `Geometry` / `Drawing` | Half-open `RegionBounds`; exact wrapped `fillRectangle` | District reserves, crop beds, canals, masks |
| `BalancedStarts` | `selectSeparatedSites`: bounded deterministic maximin selection over legal tile candidates | Space city plazas or reservoirs before assigning owners |
| `Grid` | `groundUnitTiles(map, canSwim)` | Compare walking and swimming routes with the same engine predicate |
| `Resources` | `resourceFrontages(map, flood, range, fertility)` | Score quarry access or renewable harvesting faces using a previously computed catchment |
| `Growth` | `cropSpreadEnvelope(map)` | Prove sand-contained farmland cannot overrun a building court |
| `Room` | `potentialBuildingTiles`, `arrangeBuildingGrid` | Prove a city district fits separate buildings and access after all proposals are built |

`selectSeparatedSites` returns candidate indices, search attempts, and a failure reason;
it consumes no RNG and does not retry seeds. Shuffle the input explicitly when wanted.
Its limits are 4096 candidates, 32 requested sites, and 64 first-anchor attempts.

`BuildingGrid` specifies unwrapped bounds, rectangular footprint size, gap, and inset.
The arrangement considers every complete footprint in fixed row-major order, blocks
all of them in the walking mask, and requires a reachable cardinal face for each from
the supplied entrances. It reports failure if circulation is lost. This is a feasible
layout check, not a maximum-packing optimizer. The caller supplies the minimum count.
`potentialBuildingTiles` ignores mobile units but retains resources and existing buildings;
it describes future room rather than permission to place a building immediately.

The growth envelope floods pure grass from wheat/wood across seams, ignoring buildings
and current fertility. It deliberately overestimates spread, so containment survives
building demolition and fertility changes as long as the terrain remains. Generated maps
contain crops with terrain only; there is no helper for the engine's saved no-growth flag,
which is forbidden on generated maps. An engine-growth fixture checks that a sand ring
really holds irrigated wood back, including across the map's seams.
Frontage counts describe edges, not unique resource deposits or concurrent worker slots.

### `RecursiveGeometry.h`

`RegionBounds` are integer half-open `[x0,x1) × [y0,y1)` bounds in unwrapped tile
coordinates. `partitionRegions(bounds, divisions, maximumDepth, minimumSide, stop,
maximumNodes)` supports divisions 2 and 3. It computes shared boundaries once per
parent, distributes integer remainders without gaps, and assigns breadth-first IDs.
Each flat-tree node records its parent, children, depth, bounds, and terminal stop
reason. IDs belong to geometry, never to a team number.

The callback may return `Home`, `Spacing`, or `Caller`; the helper adds `Depth`,
`Size`, and `Limit`. A stopped region still covers its entire rectangle. The result
reports actual depth and an explicit failure. Defaults cap output at 16,384 nodes;
public arguments have separate safety limits. A limit failure is not a valid partial
map. Existing generators have not been migrated to these helpers.

`hilbertPath(bounds, maximumOrder, minimumSpacing, orientation)` returns ordered
points, segments, and its binary region tree. Segment IDs are path order; segment
hierarchy is the endpoints' lowest common ancestor. Eight global rotations/reflections
are supported. The decoder preserves recursive entry/exit continuity. Uniform order
is reduced until cell spacing on the shorter axis fits. Coordinates are transformed
before rectangular scaling; callers pass their own tile-unit widths to `strokePath`.
The helper never wraps a segment onto a shorter unintended route.

### `HierarchicalCrossings.h`

`selectCrossings(torus, regionCount, existingEdges, requiredRegions, candidates,
localPerParent, majorBudget, separation, seed, compatible)` accepts at most 256 graph nodes and
2,048 candidates. Each candidate carries a stable ID, two region IDs, endpoint
positions, level, parent, and travel length. Generators propose legal geometry.
Level zero uses the global major budget; higher levels use per-parent local budgets.

The selector joins required components first, then greedily maximizes the reduction
in all-pairs graph distance. It recomputes marginal benefit after each selection.
Mandatory crossings do not consume optional budgets. Stable integer seed/ID mixing
breaks ties without drawing generator RNG. Midpoint separation uses toroidal distance.
The result includes crossings in selection order, their benefit estimates, counts,
shortfalls, and connectivity failure. This is a bounded greedy heuristic, not an
optimal constrained-network solver; separation choices can prevent later connections.

The generators' coarse graph uses raster land floods that include seam and end-around
routes. The graph precedes resources and settlements, so final-world validation still
matters: a correct graph cannot prove a beach preserved a crossing, a building did not
block access, or farms left useful gathering frontage. Benefit estimates are not game
travel times. Gardens ranks an approach to the required island destination and stamps its opposing
counterpart as the same causeway pair. The counterpart is not credited in that coarse
benefit estimate. Pair paths may share the island court; distinct approach positions
and the shared compatibility predicate provide separation. Measuring only travel between outer banks would omit the island
objective and incorrectly reject valid maps with short seam routes.

## Worked reuse examples

```cpp
// Recursive city districts: reserve the administrative/home quarter before zoning.
auto city = partitionRegions({0, 0, 256, 512}, 2, 4, 24,
    [&](const RecursiveRegion &r) {
        return reservedDistrict(r.bounds) ? RegionStop::Home : RegionStop::None;
    });
// Each terminal's complete bounds become a district. Use its parent for regional roads.

// Nested reservoirs: thirds share exact boundaries even on a 128×512 torus.
auto reservoirs = partitionRegions({0, 0, 128, 512}, 3, 3, 24);
// Paint the central opening only in eligible nonterminal regions; preserve a land rim.

// Folded roads: geometry moves with the rectangle, road width stays three tile units.
auto road = hilbertPath({-32, 0, 224, 512}, 4, 30, 5);
std::vector<StrokePoint> stroke;
for (auto p : road.points) stroke.push_back({p.x, p.y, 1.5});
strokePath(roadMask, torus, stroke);

// Candidate bridges across canals: sampling does not assign owners or edit terrain.
auto proposed = transverseCrossings(road.segments, {0.5, 0.25, 0.75}, 8, 12);
// Filter candidates against protected masks with strokeIntersectsMask, then build
// crossingEndpointGraph(torus, passableTiles, legalCandidates). Its edges include
// ordinary streets, river ends and seam routes.
auto bridges = selectCrossings(torus, districts, streetEdges, requiredHomes,
                              legalBridgeCandidates, 1, 2, 12, seed);
if (!bridges.failure.empty()) return bridges.failure;
// Stamp bridges, finish terrain/resources, then check engine walking predicates again.
```

Keep hierarchy through rasterization: it distinguishes a shortcut inside one parent
from a regional connection, allows home reservations to stop only their descendants,
and makes achieved-depth/omission telemetry interpretable. Flattening everything to
an unlabeled bitmap throws away those decisions.

## Reproducible validation and pilot

`python3 -m tools.fractal_maps.plan --bundle BUNDLE --output PLANS` reads the immutable
bundle catalog and prepares training/held-out geometry, control boundaries, all-AI
mirror games, and four-colony Nicowar/Maxima blocks. Execution uses the existing
`tools.tournaments` coordinator and disposable local worker profiles. It creates no
alternative runner. Geometry studies disable candidate selection.

Evidence lives under `artifacts/fractal-maps/`; see its `INDEX.md` and the checked-in
[validation report](FRACTAL_VALIDATION.md) for exact build identities, commands, results,
tuning changes, support limits, and missing coverage. Saves/replays use existing
formats. No simulation, AI-policy, save-version, replay-version, or network-version
change is part of these generators. Cross-platform execution equivalence requires
actual matching checksum runs; local results do not establish it. Human fun remains
a maintainer playtest judgment.

### Operational validation notes

Pilot bundles precede the final registration audit: consult each bundle catalog when
reading numeric generator IDs. ID 33 remains retired; the delivered registrations
are 49 (Gardens) and 50 (Hilbert). Prototype IDs are not a save-format change.

The analyzer normalizes omitted control defaults and lists distinct successful and
rejected seeds per configuration in `support.csv`. Duplicate baseline/grid jobs do
not become independent evidence. Mixed seed outcomes require investigation.

The optimized custom-setup harness generates both maps at seed 20001, runs a real
match, and plays its replay with normal engine checksum assertions. The toolkit
harness independently checks zero-ambient-resource map serialization. These are
local platform checks; they do not establish cross-platform execution equivalence.

`crossingEndpointGraph` builds a coarse graph directly from a passable raster and
candidate endpoints, assigning endpoint-region IDs while retaining feature hierarchy.
Its wrapped floods include river ends and seams; it refuses blocked endpoints.
`strokeIntersectsMask` checks proposed corridors with the exact drawing raster against
reserved masks. `selectCrossings` also accepts a pure symmetric compatibility predicate
for constraints beyond midpoint separation (for example, keeping both shores of an
opposing causeway pair distinct). These operations support roads, canals, and bridges
without embedding either fractal map's layout.

Gardens first proposes its two square centre lines, then parallels on both axes that
still land on the orchard island, rejecting complete strokes that touch protected
irrigation water, crop corners, the construction court or its service apron. Only when
home reservations leave an axis with no square candidate does it add a bounded
five-degree angular grid for that shortfall. Empty external home lanes may meet a causeway. Pair selection keeps both bank
approaches at least ten tiles apart, including across seams.
It never clears a home farm or changes the nested lakes to force a crossing.

`transverseCrossings` proposes perpendicular crossings along caller-supplied recursive
segments at stable fractional sample slots. Span and end clearance are tile units;
segment hierarchy survives in each candidate. It keeps coordinates unwrapped and
omits samples too near bends, with explicit size/input limits. Raster legality stays
with the caller. Hilbert initially samples midpoints, then tries fixed interior
fractions only when every midpoint court conflicts with homes. This also supports
bridges across canals or junctions along folded roads.
