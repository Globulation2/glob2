# Lessons from landscapes and organic shapes

This reference reviews 16 generators, including their headers, implementation comments,
This reference reviews the landscape generators, including their headers, implementation comments,
controls, placement stages and validators. Together with [the structured-generator review](shaped-generators.md),
the review covers all 32 entries in the [registry](../../../../src/map/generator/core/GeneratorRegistry.cpp)
at authoring time (Braided river, 34, was added after the review and its row describes its own design). Numeric IDs are compatibility identities, not catalog order. Re-read the
at authoring time. Numeric IDs are compatibility identities, not catalog order. Re-read the
linked implementation when changing a generator: the examples below describe the reviewed code,
not permanent API guarantees. Numbers are useful starting points for experiments, not universal
playability thresholds. Comments headed “WHY IT PLAYS WELL” state design intent; they do not
establish that the result is fun.

The reviewed option headers are `CoralGenerator.h`, `CraterLakesGenerator.h`,
`EvergladesGenerator.h`, `FjordContinentGenerator.h`, `IslandsGenerator.h`,
`DrumlinFieldGenerator.h`, `OldGrowthGenerator.h`, `PolderGenerator.h`, `RainShadowGenerator.h`, `RiverGenerator.h`,
`RuggedArchipelagoGenerator.h`, `ShatteredCoastGenerator.h`, `StoneHighlandsGenerator.h`,
`SwampGenerator.h`, `TidalFlatsGenerator.h`, `WatershedGenerator.h` and `BraidedRiverGenerator.h`, all beside their
`SwampGenerator.h`, `TidalFlatsGenerator.h`, `WatershedGenerator.h` and `ContinentsGenerator.h`, all beside their
linked implementations in `src/map/generator/generators/`.

## Coverage and where to borrow an idea

| Generator, ID; source | Construction and intended play | Reusable lesson or caution |
| --- | --- | --- |
| [Swamp, 1](../../../../src/map/generator/generators/SwampGenerator.cpp) | Noise height field with scattered ponds; no explicit sand/desert weight. Shore farmland is renewable, while water fragments movement and building space. | Smoothing changes feature scale and therefore usable room. A visually green wet map can still be a poor base. |
| [River, 2](../../../../src/map/generator/generators/RiverGenerator.cpp) | A looping river bed on the torus, with optional meander. Width controls the bed, while water weight controls the flooded share. | Two independent knobs can interact: narrow beds leave surplus water in unrelated hollows; broad gentle beds turn resources into stripes. Check the resulting map, not slider names. |
| [Islands, 3](../../../../src/map/generator/generators/IslandsGenerator.cpp) | Height-field hills, one per team plus extras before repeat tiling; water thresholds expose islands. Starts are subsequently searched. | “One hill per colony” does not promise one island per colony. Merging, noise and start selection can put rivals on the same island. Validate isolation if it is part of the concept. |
| [Crater lakes, 4](../../../../src/map/generator/generators/CraterLakesGenerator.cpp) | Random bowls in high ground; density sets area-scaled bowl count, lake size sets radius, water weight controls filling. | Small lakes reshape paths on an otherwise open land map. Bowl size and density redistribute the water budget rather than independently adding water. |
| [Old random / Shattered coast, 7](../../../../src/map/generator/generators/ShatteredCoastGenerator.cpp) | Random terrain, fitted mix and smoothing; colonies take wide grass patches, optionally stamped meadows; directional resource search. | Texture alone has weak economic/topological guarantees. Preserve its historical look when maintaining it, but prefer structure first in a new design. Its reinforcing crop slot now targets whichever primary crop got less space. |
| [Old islands / Rugged archipelago, 8](../../../../src/map/generator/generators/RuggedArchipelagoGenerator.cpp) | Seeded islands expanded by random accretion, then holes smoothed and beaches widened; compass-oriented kits. | Beach width trades buildable grass for walkable coast. Islands can merge at large settings, despite the introductory “alone on an island” intent. Resource smoothing and backstops materially affect initial room. |
| [Fjord continent, 12](../../../../src/map/generator/generators/FjordContinentGenerator.cpp) | Rough stretched continent, fjords between angular sectors, starts toward tips, core lake/prizes and offshore islands. | Make close neighbours far apart by walking while keeping swimming shortcuts. The lake-connected variant deliberately changes land connectivity; validate each mode against its own promise. |
| [Watershed, 13](../../../../src/map/generator/generators/WatershedGenerator.cpp) | Springs attach downstream into a river tree and optional delta; dry uplands, fertile banks, selected sand fords. | Strong example of planning and then checking rasterized crossings: the river must remain a barrier between fords, and each ford must cross a real channel and reach both banks. |
| [Stone highlands, 14](../../../../src/map/generator/generators/StoneHighlandsGenerator.cpp) | Warped nearest-site cells become stone ridges and valleys; a spanning tree of passes plus extra loops connects them. | Permanent walls create rooms; loops create flanking. Choose roomy valleys and reserve ponds/routes before stocking them. The grass margin called a ring road is initially clear, not intrinsically immune to future growth. |
| [Tidal flats, 18](../../../../src/map/generator/generators/TidalFlatsGenerator.cpp) | Repeated wedge design: grass home islands and neutral oases on walkable sand, pools and lagoons between them. | Separate fighting ground from building ground. Sand lets armies meet early while making forward inns/towers depend on scarce grass patches. Wedge copies are approximate after rasterization, not exact tile symmetry. |
| [Everglades, 19](../../../../src/map/generator/generators/EvergladesGenerator.cpp) | Jittered periodic pool lattice, wetness-scaled pools/sloughs, dense crops, asymmetric ring clearings with incomplete levees. | A growth-pressure map can intentionally creep into bases; the default levee is only 70% closed. Treat this as a difficulty/style choice and test AI survival, not as an AI-safe template. |
| [Coral, 21](../../../../src/map/generator/generators/CoralGenerator.cpp) | One branching fan grown breadth first and rotated into every colony's share; branches refuse collisions with all copies, with optional bridges. | Random structure can preserve a recognizable silhouette. Collision checks must include transformed copies; growth depth, taper and angle must scale with usable space. Keep supply trunks open and put prizes on remote tips. |
| [Rain shadow, 27](../../../../src/map/generator/generators/RainShadowGenerator.cpp) | Periodic slanted stone ridges, staggered passes, wet windward streams and dry lee sand; lattice homes snapped into valleys. | Directional economics gives the map a grain. Pass approaches need continuous sand connections into useful routes, not short decorative strips that end in regrowing grass. |
| [Rice terraces, 52](../../../../src/map/generator/generators/RiceTerracesGenerator.cpp) | Rain shadow's slanted stripe field turned into terraced hillsides: a dry crest of towns, narrow contour strips of rice and channels down both slopes, a valley with a river; the contours sway in a few whole-number waves along the hillside plus a fine grain; stairs straight across the terraces continue as valley roads that ford the river. Towns are spread along the crests' middle line by farthest-point selection, and the terraces bend round each town inside a sand ring. | Built after the concentric-ring version (now Hills) failed to read as rice terraces: start from the landscape's own signatures, not the nearest primitive. A single shared sway for every contour keeps widths and never folds. A lattice slid onto crests packs towns together; pick sites on the crest itself. Count hillsides per 256 tiles of side or a 512 map has valleys twice as wide. |
| [Old growth, 28](../../../../src/map/generator/generators/OldGrowthGenerator.cpp) | Dry wood cover outside protected, well-watered homes; rare distant lakes and hidden groves; optional starting trails. | Clearable barriers and permanent barriers produce different games. Dry forest preserves the history of cutting; hidden prizes should be priced in cutting cost, not straight-line distance. |
| [Drumlin field, 33](../../../../src/map/generator/generators/DrumlinFieldGenerator.cpp) | Aligned teardrop hills packed under a grain metric in a water matrix; sand eskers from a near tree plus loops are the only dry routes; home heads parted from farm tails by a sand collar. | Anisotropic packing gives a swarm look with a guaranteed water gap; shut-esker isolation and collar continuity are validated on the finished map. Everything is fertile by design, so the beach rim is the permanent road; expect farm drumlins to grow shut. |
| [Polder, 30](../../../../src/map/generator/generators/PolderGenerator.cpp) | Periodic crop/water rows crossed by sand dykes over ditches and crops alike, grass villages/hamlets and clear farm plots. | Abundant food can produce a logistics game rather than automatic prosperity. Open village ground, anti-growth rings and remote inn plots are necessary complements to fertile rows. |
| [Braided river, 34](../../../../src/map/generator/generators/BraidedRiverGenerator.cpp) | Antiphase sinusoidal channel threads in lanes across a belt along the long axis; bars are the land components between crossings; riffles (shared `SandFord`s) join them along a random spanning tree plus loops and one forced crossing per home; dry terraces with a sealed bluff line at the seam and an optional moraine. | Read a route graph back off the rasterized water (`channelCrossings`) rather than trusting the plan, and split request-only checks (`validateRequest`) from seed-dependent design failures the service should roll again. Without the sealed line at the torus seam the walk round the back was shorter than the braid. |
| [Continents, 34](../../../../src/map/generator/generators/ContinentsGenerator.cpp) | A real continent from the compiled world atlas (`WorldAtlas`, `Raster`, `Landmass`): majority-resampled coast, lakes, deserts as sand, ranges as scree, great rivers with fords; sites spread by walking distance, ponds dug for dry colonies, each kind of land furnished with a biome kit. | Fixed geography means fairness is measured, not constructed: spread sites by walking distance, give each colony a guaranteed square and what the geography left out (water, a kit), and let the lobby's best-of-rolls and the tournament judge. Features whose footprint is fixed in tiles (a river's three-tile scar) must be omitted below the map size that can afford them. |
| [Braided Delta](../../../../src/map/generator/generators/BraidedDeltaGenerator.cpp) | Longitudinal channels round the torus exchanging water through staggered side channels, closing loops round elongated islands; sand fords fitted on the sketch, a 20x20 sand-rimmed town on every island, sand approaches from every ford end to its own island's rim; farthest-point seats, dealt. | A ford is not an accessible ford: bank crops closed the ground between town and ford and no colony could walk to another. Label islands before laying fords so approaches stay on their bank. Plant kits before ambient layers; validate that no crop seed sits inside a rim. |
| [Braided river](../../../../src/map/generator/generators/BraidedRiverGenerator.cpp) | Antiphase sinusoidal channel threads in lanes across a belt make lens-shaped gravel bars; crossings are read off the rasterized water (`channelCrossings`) and riffles laid on a random spanning tree plus loops and one forced crossing per colony (`stampFord`, `fordFault`); dry terraces either side, a sealed bluff line at the seam, an optional moraine. | Try crossings on the sketch, not by centre-line distance, or inner threads get none. Keep towns above the growth probe's reach so they never overgrow, and stock the promised bar so no start starves across water. Without the bluffs the walk round the torus was shorter than the braid. |
| [Drumlin field](../../../../src/map/generator/generators/DrumlinFieldGenerator.cpp) | Teardrop hills all pointing one way under a `Grain`, packed round relaxed dart sites (`packLandforms`), water in every hollow, wandering sand eskers on a near tree with loops as the only dry routes; each home's head is town, a sand collar parts it from the farm tail, orchards on the farthest drumlins. | Everything fertile means farm tails grow shut while beaches stay roads. Take the grain heading that keeps crowded homes apart rather than refusing; shrink homes only to a documented floor. A cheap request check plans only the homes. |
| [Continents](../../../../src/map/generator/generators/ContinentsGenerator.cpp) | A real continent from a baked atlas (`WorldAtlas`, `Raster`, `Landmass`) with climate classes as terrain and biome kits; sites spread by walking distance (`farthestSites` over tiles), scored by fertile grass, walkable catchment and river clearance, recentred with an undo, dug ponds until a fertility floor; routes opened through scree and across rivers. | A toy, not a tournament map, and honest about it. Fertile-grass windows see ground, not the way to it: add a catchment floor. Better starts made games end sooner by elimination, which is the war game working. Invented islets round a real coast were the first thing a maintainer asked about; they are off by default now. |
| [Savannah](../../../../src/map/generator/generators/SavannahGenerator.cpp) | Open plains with jittered lattice homes (`jitterSites`), sealed sand-capped crop plots (`stampContainedPlot`) beside small home ponds, two opposed five-tile exits per home, neutral watering holes placed to be locally contested. | Enlarging plots for growth room is safe only if the plot edge facing town stays where it was: moving the wheat edge toward town made Numbi place no food inn. No generic repair: essential shortfalls fail, optional deposits saturate inside their plots. A plain reads as empty without its scenery: pools where nothing is planted and lone trees on growth-chance-zero ground add the look without touching the containment proof. |
| [Locust](../../../../src/map/generator/generators/LocustGenerator.cpp), Vultures until 2026-09-16 | Old Growth's clearings and lakes with finite dry wheat (zero growth probability, three to five harvests a tile set after every repair) instead of forest, wood only on shores, fixed dry rations and a quarry per home, `roomyLatticeSites` for prime colony counts. | A finite-food concept must forbid renewable repairs and give fertility zero weight in the start scorer. Spend a starter budget across disconnected pockets (`growPatchesNear`) rather than failing on the first sliver. A probe that changes winners in both directions across paired seeds is not evidence for a default; a maintainer's reading of the concept (the fields are the obstacle, so fill them in) is, and the cover default went from 35% to 65% on it. |

The four height-field generators share [Terrain](../../../../src/map/generator/shared/Terrain.h)
and [HeightMap](../../../../src/map/generator/shared/HeightMap.cpp). Changes to their shared
classification, shoreline, resource bands, repeat tiling or start selection affect all four.

## Turn landscape imagery into player decisions

Translate a concept into a route/economy story before choosing its drawing algorithm. For
example, “river country” can mean early fights at fords (Watershed), delayed cross-bank contact
(River), or long walks around peninsulas with short swimming shortcuts (Fjord continent).
They look related but need different invariants. “Forest” can mean renewable encroachment
(Everglades) or a one-time worker investment to open the world (Old growth).

For every geographic feature, write its player consequence:

- A channel delays untrained ground units, but trained swimmers change the route network.
- A stone ridge fixes an impassable ground boundary while allowing tower fire across it.
- A wood barrier costs worker time to cut and may return if watered.
- A sand flat supports travel and denies buildings/resources; an embedded grass patch becomes
  a strategic staging point.
- A pond supplies potential growth as well as consuming building space and creating beaches.
- An orchard is strategically valuable through inns and conversion, not merely decoration.

Then place a reward where the intended decision happens. Coral's fruit buds reward the longest
walks into fragile territory. Watershed puts fruit at confluences and stone toward dry uplands.
Fjord continent puts multiple fruit types and stone in the shared core. Polder's remote farm
plots reward pushing feeding infrastructure out to the fields. Leave room for the buildings
that make those rewards usable; a full resource clump is not an outpost site.

## Growth containment is a spatial design problem

Use the engine-derived [Growth](../../../../src/map/generator/shared/Growth.h) and fertility
helpers, rather than assuming “near a pond” means uniformly fertile. The probe reaches up to
15 tiles on each axis, requires pure water on one side, and rejects pure sand at the opposite
sample. A strip of sand also prevents crops physically spreading onto the strip itself. These
are distinct effects: a nearby sand tile is not a blanket zero-growth radius.

Three useful patterns emerge:

1. **Protected settlements beside productive country.** Polder's village ring and Old growth's
   clearing ring use two undermap-corner layers of sand to resist bridging through the terrain
   conversion. Polder removes home wheat/wood blocks because the external rows supply them and
   the interior needs building room. Old growth excludes home wood while seeding wheat around
   several pools. Validate both the ring and harvest access across it.
2. **Permanent travel corridors through fertile terrain.** Coral traces sand down branches that
   carry through traffic, but keeps pads and dead-end buds free of that road to preserve their
   small grass area. Rain shadow extends sand through passes and connects it to stream beaches.
   A route opened by clearing crops once can grow closed again; sand-based protection persists.
3. **Dry clearable barriers.** Old growth keeps extra lakes away from homes and checks the
   finished fertility field beyond water reach. Cutting then changes the world permanently.
   A dry forest still demands enough accessible food and worker support to make clearing fun.

Do not infer long-run AI compatibility from an initial flood fill. Everglades deliberately has
levee gaps; Stone highlands' unplanted grass road can acquire crops later; a sparse initial
field may become dense. Observe several AI economies through expansion and upgrades, tracking
whether workers can reach food, building sites, front-line feeding and rivals. Retain maps and
replays showing the failure before adjusting widths, water placement or containment.

The source's recorded human feedback is especially instructive: Polder's initial villages were
too small and overgrown; Old growth needed larger homes, more pools and substantially more
seeded wheat; Rain shadow needed deeper water and longer, connected sand approaches. These are
examples of tuning after play, not proof that their current defaults solve every parameter set.

## Building and movement room after rasterization

Measure pure grass footprints on the final map, not disc radii or nominal land area. Terrain
uses four undermap corners per tile, so beaches consume more usable room than their painted
outline suggests. A thin pond can become almost all beach; a narrow island can keep walkable
sand but lose its building strip. Tidal flats sizes oasis ponds down with small oases; Coral
keeps a minimum branch half-width of four so beaches plus a road leave some grass.

Reserve the home footprint, worker exits, nearby economy space, resource approaches and future
construction sites as different needs. A count of legal 4×4 anchors can include overlapping
footprints; it is a useful room score, not that many independent buildings. Favor continuous
buildable patches over many tiny scraps. Stone highlands reserves a five-wide route from homes
toward its ridge margin before growing ponds. Watershed keeps swarm candidates clear of fords
and distributes starter crop clumps around different sides of the swarm.

Validate traffic in stages: workers leaving home; workers reaching harvest-adjacent tiles;
workers supporting forward inns/buildings; warriors reaching fronts; and the changed network
after swimming. A topological path one unit wide can still be a terrible supply route. For
barriers, an eight-neighbour movement model matters: Watershed checks a four-connected water
core to prevent diagonal stepping across a supposed continuous river.

## Fairness on asymmetric maps

Symmetry is only one option. Use the existing [start scorer](../../../../src/map/generator/shared/StartQuality.h)
and [balanced start search](../../../../src/map/generator/shared/BalancedStarts.h) as measurable
components, not as certificates of competitive fairness. The scorer measures wheat/wood access,
fertility, resource depth, building room and isolation, and a
[model fitted to real games](../../../../docs/map-generators/FAIRNESS_MODEL.md) turns those into
each colony's chance of winning; maps are ranked by how evenly that chance is shared. Fairness
alone will happily reward a map on which every colony is equally bad, so keep an absolute
viability check in `validateWorld`.

Useful construction heuristics from these generators:

- **Watershed:** reject candidates outside the main land component, outside the floodplain,
  lacking an 8×8 grass square around a 4×4 swarm, or too near a ford. Prefer the more fertile
  half of candidates; run multiple greedy spreads with random first sites; discount candidates
  sharing a bank and only broaden the fertility pool if needed. These are heuristics, not
  equal access to all downstream opportunities.
- **Stone highlands:** favor the largest suitable valleys, discard those below a room floor,
  spread homes across valleys, and only share valleys when necessary. Pond placement reserves
  homes and escape paths. Initial valley depth is a much better base criterion than a random
  point in a green area.
- **Fjord continent:** choose sector roles first, then place each colony toward its own tip;
  maintain matching guaranteed resource roles on both flanking banks. Equal resource counts
  still need route, growth and room comparisons on the finished coast.
- **Old growth:** locate hidden groves using per-colony cutting-cost fields and `equalCostSites`
  from [Contact](../../../../src/map/generator/shared/Contact.h). Euclidean closeness is the wrong
  budget when the cost is clearing a forest.
- **Height-field generators:** choose starts after resource bands exist, with terrain-only
  fallback if the balanced set cannot be found. Repetition duplicates a landscape patch, but
  does not by itself assign one equivalent completed settlement to each copy.

Tune the weak start and the unmeasured opportunities: expansion room, approaches to shared
prizes, who meets whom first, number of defendable doors, and post-swimming access. Do not tune
weights solely to make a score rise. [The fairness tournament guide](../../../../docs/map-generators/FAIRNESS_TOURNAMENT.md)
documents why rotating team indices over the same starts is necessary and why apparently good
start scores can miss dominant positions. Use saved maps with all team-index rotations and
multiple engine seeds, and report inconclusive or capped games separately from decisive wins.

For exact symmetry use [Orbits](../../../../src/map/generator/shared/Orbits.h): point symmetries
have supported map/count combinations; exact translations on power-of-two maps require an
appropriate group order. A `latticeSites` layout for other counts is approximate. Coral and
Tidal flats reuse continuous wedge geometry but rasterize rotated copies separately, so do not
claim their tiles, deposits, starts or travel times are identical for arbitrary team counts.

## Variety without destroying the concept

Separate structural draws from resource and cosmetic draws using named context streams. A
resource slider should not redraw the coast merely because it consumed a different number of
random values. Coral rolls branch decisions once before copying; its random lengths, bends,
fork angles and collision refusals change the tree itself. Breadth-first growth prevents the
first subtree taking all the room. Watershed changes spring locations, network attachments,
coast phase and fords; rectangles orient its river network along the long axis to retain coverage.

Polder samples a near-continuous row angle then rounds to integer wrap counts, using
[Patterns](../../../../src/map/generator/shared/Patterns.h); this keeps torus seams exact while
making many more variants than a few fixed orientations. Rain shadow combines slant, staggered
passes, warped ridge phase, inland lakes and sand patches. Everglades hides its periodic pool
lattice through bounded jitter and a coarse wetness field. Use random topology, meaningful
spacing and regional texture together; rotating a fixed layout alone offers limited replay value.

Two drawing traps turn nature into diagrams. A route found by `cheapestWalk` along a boundary
follows the boundary: noise in the step cost is not enough to make a stream meander, and The
Glacis' first streams came out ruler-straight. Walk the boundary, then displace the walk sideways
by two sine waves of wavelength and phase drawn per stream, tapered to nothing at both ends so
streams still meet at junctions, and stroke the displaced line. And a body grown with `growWater`
round a key of distance plus a little noise, or a `RadialShape` of low roughness, comes out a
circle: ponds, oases and homes on the first rebuilds read as compass work until the noise weight
dominated the key, the roughness rose to 0.2 to 0.3, and oases were stretched up to 1.8 along a
drawn heading. Check a mosaic of seeds for circles and straight lines where the concept names
something natural.

Keep geometry-aware bounds. Fjord continent checks an offshore island against the actual coast
in its direction, instead of a pessimistic single maximum coast radius that would reject almost
every island. Tidal flats uses bounded placement attempts and skips optional blobs that cannot
fit. A skipped essential food source, gate or opponent connection is a failure, not decoration.

## Parameter reliability and shared primitives

The examples offer several bounded adaptation strategies: reduce optional feature counts,
reduce jitter, shrink homes only down to a documented viable floor, widen a placement ring,
reduce ridge count, or simplify branching before reporting that a request cannot fit. Everglades
estimates overlapping pool coverage and scales radii down above a 60% expected coverage cap.
Coral scales branch levels with map size and reduces levels if the trunk cannot clear its pad.
These are design-preserving adaptations to test, not permission to silently erase a slider's
effect. Report what an extreme setting actually produced.

Test interactions, not only isolated minima/maxima: crowded small rectangles with wide channels;
high roughness plus large homes; few fords plus dense resources; maximum water plus minimum
grass; repeat tiling plus high team count; and resource zero/default/high with the same terrain
seed. Verify routes after deposits, guarantees and repair passes, since a top-up can close a
route and a route cut can remove a starter crop. Watershed explicitly reconnects, tops up and
reconnects again. Use protected masks when repair must not clear structural stone.

Prefer the shared components already used here: `TerrainSketch`/`pureTiles`/`layBeaches`,
`Torus`, morphology and distance fields, `RadialShape`, `WedgeFrame`, periodic noise,
`growWater`, `growBranches`, settlement/kit helpers, `furnishGround`, fertility-aware algae,
farm plots, cost-based roads and start guarantees. If a generator reveals a general defect in
one of these, fix and test the primitive with its other callers. Do not paste a local corrected
copy. Watershed's bespoke river planner and Stone highlands' local pass graph are useful
algorithms to consider extracting when a second generator needs the same contract; extraction
must preserve or explicitly revise existing outputs.

## Existing exceptions to avoid inheriting accidentally

The user-facing resource promise needs a deliberate policy, because these implementations are
not uniform. Many keep an unscaled minimum starter kit; that is distinguishable from ambient
resources. Current examples also include broader exceptions:

- Old growth's structural wood uses `forest-density`, with no `wood-amount` control.
- Rain shadow keeps structural ridge stone fixed while scaling scattered stone. Stone highlands
  instead lets the stone amount scale additional ridge thickness without deleting the wall.
- Tidal flats fills neutral oases with unscaled wheat. Several small fruit/stone prizes are
  gated by a positive scaled count rather than varying continuously above the threshold.
- Fjord continent keeps starter/bank guarantees and offshore themed prizes unscaled; its
  optional bank scatter is scaled separately.
- Height-field fruit is an old grove-count control and also appears in the non-swamp terrain
  weight denominator, a documented historical coupling. It is not a pattern for new controls.

For a new generator, make resource-bearing regions respond to shared resource controls and
document any required survival floor or structural barrier minimum. Prefer scaling surplus
coverage, thickness or density in a way that preserves the design. Do not silently ignore a
wood/stone/wheat slider because “the resource is the map”; expose the relationship clearly and
verify the zero/default/high outputs. Correcting existing exceptions is a separate behavioral
change requiring appropriate versioning, evidence and review, not part of writing this skill.

Older code also preserves historical slips in Shattered coast's algae search and Rugged
archipelago's quarry offset. Their comments explain output compatibility; they are not coding
recommendations. Likewise, a validator is code to review, not an oracle: check sentinel meaning
explicitly (`steps < 0` means unreachable), test deliberate broken designs, and confirm that a
connectivity check runs on final resources rather than only bare terrain.
| [Lava shield](../../../../src/map/generator/generators/LavaShieldGenerator.cpp) ([options](../../../../src/map/generator/generators/LavaShieldGenerator.h)) | A volcanic island: crater lake in a stone summit with a reserved walking rim (the contested prize, renewable farmland and fruit), branching downhill lava tongues with correlated turns (`downhillPath`), unequal green wedges, towns in one of seven shapes per map (square, three rectangle ratios, rounded, octagon, oval, turned either way) with a frayed sand edge every town shares, chosen by scoring completed candidate settlements (`ScoredSettlements`, `spreadRankedSites`) with absolute room and access floors, a bounded starter-field retry that seeds the patch with frontage rather than the single most fertile tile (`seedForPatchCapacity`), swimming-prize islets on the seams (`raiseIslands`). | Legal stone cannot touch water, and the beach pass leaves walkable sand between them, so a lava tongue drawn into the sea never seals a coast: long tongues make narrow beach detours, short ones broad gaps. Design the map round that instead of writing illegal stone or filling the coast. Score whole finished candidates, not bare terrain, when the wedges are unequal by design. |
