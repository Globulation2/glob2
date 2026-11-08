# Lessons from landscapes and organic shapes

Read this when the concept is organic terrain: a landscape with found starts rather
than a drawn shape with stamped homes. It reviews the landscape generators' headers,
implementation comments, controls, placement stages and validators; with
[shaped generators](shaped-generators.md) it covers every playable entry in the
[registry](../../../../src/map/generator/core/GeneratorRegistry.cpp). Numeric IDs are
compatibility identities, not catalog order. Re-read the linked implementation when
changing a generator: the rows describe the reviewed code, not permanent API guarantees.
Numbers are starting points for experiments, not universal playability thresholds, and
comments headed "WHY IT PLAYS WELL" state design intent without establishing that the
result is fun.

Each generator's options header sits beside its linked `.cpp` in
`src/map/generator/generators/`.

Most of these generators were written when the engine had only grass, sand and water, so
their walls are the stone resource and their roads, fords, plot seals and coasts are sand or
beach. The rows record what each one does; when borrowing an idea for new work or an upgrade,
express it with the [terrain catalogue](terrain-catalogue.md#say-what-you-mean-with-terrain)
instead (walls of Obstacles or Ridges, Paths for roads, Barren for seals, Rough for no-build
ground), and use sand only where the subject is sand. Lava shield, Savannah and Braided Delta
already use catalogue terrain for part of their ground.

## Coverage and where to borrow an idea

| Generator, ID; source | Construction and intended play | Reusable lesson or caution |
| --- | --- | --- |
| [Swamp, 1](../../../../src/map/generator/generators/SwampGenerator.cpp) | Revision 6 (2.0): revision 5's noise height field with scattered ponds (no sand/desert weight; same ponds, deposits and starts for the same seed, the field copied out of the builder), then a deposit-safe bog pass that follows the field's relief: deep pond cores (four tiles from land in a straight line, rounded off algae beds), mud on a share of the beaches (12+ tiles from homes so their own shore paths stay firm, away from algae), thicket hammocks on the hummocks (highest land, 20+ tiles from homes, parting-checked), at most one boardwalk causeway per colony across a neck of water near home when it saves 30+ steps, opt-in moss in the hollows (beyond each home's 12-tile garden), looks via `composeLooks` with a look-only climate (temperate bog, tropical mangrove). | Smoothing changes feature scale and therefore usable room. A visually green wet map can still be a poor base. Moss costs the growth tool a tenth of the map's food (grass/moss edge tiles score as no farmland), so it is off by default; with it on, Nicowar's births fell a quarter in calibration, the known Fertile-as-water AI bug, not a map rule. The map report records no starts: take swarm corners from `swamp.starts.swarm-x/y` telemetry for growth measurements. Dry grass renders as bare brown ground in the game view; keep it to the hummock tops, and clear it round homes with a frayed disc (a step-distance clearing cut the patches into ruler-straight triangles). |
| [River, 2](../../../../src/map/generator/generators/RiverGenerator.cpp) | Revision 6 (2.0): a looping river bed on the torus, with optional meander, on the unchanged height-field pipeline (field, resources and starts as revision 5), then a post-pass from the shared `HeightFieldLooks.h`: a straight-line deep core sized so it holds at most half the water (`deepReachForShare`, `deepenWater`) with its algae reseated in the shallows by distance plus noise, Fertile levees two to seven vertices from water outside the home gardens, mudflat point bars inside the bends (never under a deposit) and Ridges-group bluffs with quarry faces outside them (`wetShare` read against the banks' own percentiles, so a straight river has neither), pavement only beyond 22 steps from water, and a `banks` variant (Temperate: green to 34 tiles, then a mosaic of meadow, dry grass and heath; or a Nile-like Desert, dry beyond a nine-tile ribbon with hardpan pans), the home gardens never dry. | Width controls the bed, water weight the flooded share; check the map, not slider names. The default bed is so wide that a fixed-step deep core swallowed 80% of the water and two thirds of the algae with it: size the core by share. Levees two or three deep lose food (the grass/loam edge row counts for nothing) and seven gain about 5%; bluffs on the bank cost farmland, so keep them few (one per 80 tiles of side). Bends read from a fixed window find nothing on a 90-tile river: scale the window with the bed and cut by percentile. Maxima and Nicowar read every fertility source (loam too) as water when placing buildings, so loam near homes stalls them; that is an AI defect to fix in the AI, not a reason to keep Fertile ground off the homes. Lay grass looks after the starts, so the starting-crop guarantee meets plain grass everywhere and no home reads as a round lawn. Measure far-ground edges as straight lines floored in steps, or they draw squares round ponds. Keep bluffs off the stone band or they eat it. Plan the bluffs from the same input in generate and `validateWorld` (both rebuild the height field with `heightFieldSketch`, before the starting-crop top-up, whose wall clearing removes stone), and contract that the replanned counts equal generation's telemetry: reading the finished map's stone in generate let the validator check a different plan. Dry grass from 20 tiles on the temperate banks turned most of the map brown and read as desert. |
| [Islands, 3](../../../../src/map/generator/generators/IslandsGenerator.cpp) | Height-field hills, one per team plus extras before repeat tiling; water thresholds expose islands. Starts are subsequently searched. | “One hill per colony” does not promise one island per colony. Merging, noise and start selection can put rivals on the same island. Validate isolation if it is part of the concept. |
| [Crater lakes, 4](../../../../src/map/generator/generators/CraterLakesGenerator.cpp) | Revision 6 (2.0): revision 5 unchanged draw for draw (random bowls in high ground; density sets area-scaled bowl count, lake size sets radius, water weight controls filling; height-field bands and balanced starts), then a crater pass over a mechanic sketch: round deep cores 4.5 tiles from land (`deep-water`) with the displaced algae moved to the same lake's shallows, broken Ridges-group rims six to ten tiles (straight-line) from the water of every bigger lake as runs of rock sectors parted by open ones (`crater-rims`; never on a neck between lakes, 24 tiles off the homes, parting-checked) with quarry faces on the outer flank by `stone-amount` nearest each colony first, Rough talus at the rock's foot, loam patches on the outer flank, barren uplands beyond 22 steps from water, looks via `composeLooks` by `crater-type` (maars: dense thick rims; kettles: sparse thin ones). | Small lakes reshape paths on an otherwise open land map. Bowl size and density redistribute the water budget rather than independently adding water. Revision 5 put algae in the lake middles, so deep cores displace them: move them to the shallows rather than leaving a dark ring round each algae patch. Rim rock within 24 tiles of homes cost 4% of near-home growth potential (the worst colony 10%); loam on the outer flanks gained 16% overall. Step-distance (Chebyshev) contours draw squares and diamonds: use straight-line distance for deep cores, rims, loam rings, looks and home rings (a straight-line band needs a tile more depth than a step band to keep its rock). Revision 5 records no start positions in the map report; revision 6 puts the swarm corners in telemetry (`crater-lakes.starts.swarm-x/y`). |
| [Old random / Shattered coast, 7](../../../../src/map/generator/generators/ShatteredCoastGenerator.cpp) | Revision 9 (2.0): revision 8 unchanged draw for draw (random terrain, fitted mix and smoothing; colonies on wide grass patches with optional meadows; directional home deposits; spaced expansion clumps outside a 32-tile home buffer, chosen by rule group), then a coast post-pass that repaints only ground nobody stands on: deep cores four steps from land in the bigger bays (`deep-water`), mudflat beaches on small sheltered waters kept 28 tiles off the homes (`mudflats`, its noise on a stream of its own so the toggle changes no look), sea stacks from the smallest empty islands and rock knots on wide vacant grass 24+ tiles from homes with quarry faces by stone-amount (`outcrops`, parting-checked), lush/dry grass and pale beach/dune looks by look-only `climate`. | Texture alone has weak economic/topological guarantees. Preserve its historical look when maintaining it, but prefer structure first in a new design. Its reinforcing crop slot targets whichever primary crop got less space. Revision 8 records no start positions in the map report; revision 9 puts the swarm corners in telemetry (`shattered-coast.starts.swarm-x/y`). Mudflat removes sand inhibition (growth potential +6%) but also algae shore support, so open shores stay sand; it is slow to walk, and mud within 16 tiles of homes cost 6% of births in calibration games despite the growth gain, so it stays 28 tiles off. |
| [Old islands / Rugged archipelago, 8](../../../../src/map/generator/generators/RuggedArchipelagoGenerator.cpp) | Seeded islands expanded by random accretion, then holes smoothed and beaches widened; compass-oriented kits. | Beach width trades buildable grass for walkable coast. Islands can merge at large settings, despite the introductory “alone on an island” intent. Resource smoothing and backstops materially affect initial room. |
| [Fjord continent, 12](../../../../src/map/generator/generators/FjordContinentGenerator.cpp) | Rough stretched continent, fjords between angular sectors, starts toward tips, core lake/prizes and offshore islands. | Make close neighbours far apart by walking while keeping swimming shortcuts. The lake-connected variant deliberately changes land connectivity; validate each mode against its own promise. |
| [Watershed, 13](../../../../src/map/generator/generators/WatershedGenerator.cpp) | Revision 4 (2.0): springs attach downstream into a river tree and optional delta, selected sand fords; revision 3's stamp (grass, sand, water) written and held a fixed point of `layBeaches`, then 2.0 layers over a mechanic sketch: deep sea core and a smooth deep thread down the trunk, delta and big tributaries (`deepCore`), Fertile levees five vertices deep along every channel outside the home gardens, mudflat river beaches at the delta and confluences, pavement on the old upland sand beyond 22 steps from water, Ridges-group rock knots on the divides' crests with quarry faces by `stone-amount` and scree (planned from the request alone, so the validator replans them), looks via `composeLooks` and a look-only climate. | Strong example of planning and then checking rasterized crossings: the river must remain a barrier between fords, and each ford must cross a real channel and reach both banks (water there is any water corner, deep or plain). Levees are a real water source (48/256): two or three deep lost a quarter of the growth potential to the unbuildable grass/loam edge row, which `growth_potential.c` counts as nothing, while five deep gain 8% and nine 52%; tune their width with the tool. A grass-group vertex beside plain water breaks the beach fixed point; loam, mud and deep water do not. Plan rock that the validator must check from the request alone, not from the chosen homes, and count a quarry's permanent stone as blocking in the validator exactly as the planner did. Grass kept round a quarry in grey pavement shows as dry grass: keep it to the face's corners and its approach ring. A deep thread stamped as discs ends in round caps: taper it in and out, and drop short isolated runs. |
| [Stone highlands, 14](../../../../src/map/generator/generators/StoneHighlandsGenerator.cpp) | Warped nearest-site cells become stone ridges and valleys; a spanning tree of passes plus extra loops connects them. | Permanent walls create rooms; loops create flanking. Choose roomy valleys and reserve ponds/routes before stocking them. The grass margin called a ring road is initially clear, not intrinsically immune to future growth. |
| [Tidal flats, 18](../../../../src/map/generator/generators/TidalFlatsGenerator.cpp) | Revision 9 (2.0): repeated wedge design of grass home islands and wheat-covered oases on walkable mudflat (Rough: slow, unbuildable, does not inhibit growth), threaded by sand tidal channels (a wavy ring through the homes, spokes, an inner ring, creeks) drawn in the wedge frame; shell banks round every pool and lagoon; lagoons with deep beds (`deepCore`); a central rock tor with a quarry face turned to every colony (`rockVertices`, `quarryNear`, `quarryFault`); a tide control scaling the water on the flats; looks over a mechanic sketch with `composeLooks`. | Separate fighting ground from building ground. Mud instead of sand flats keeps the flats unbuildable but stops them inhibiting the islands' growth, which nearly doubled near-home food potential without overgrowing homes in a 20,000-tick game. Algae needs solid sand within its probe's reach, so mud flats need sand banks by the water. A feature the scatter places after the islands may never fit: revision 8's lagoons fitted no wedge at the defaults, which only telemetry showed. Wedge copies are approximate after rasterization, not exact tile symmetry. Fray noise with a cell about a pool's size draws even rings (bull's-eyes): use a cell a few tiles across and stretch its contrast so a bank or seep is wide on one side and thin on another. Scale per-wedge features (creeks, an inner ring) by the wedge's arc, or a small map crowded with colonies is all channel, and keep quarry faces from eating the rock they cut: shrink them when the rim per colony is short and require a minimum of rock. |
| [Everglades, 19](../../../../src/map/generator/generators/EvergladesGenerator.cpp) | Revision 4 (2.0): revision 3's jittered periodic pool lattice, wetness-scaled pools/sloughs, dense crops and asymmetric ring clearings with incomplete levees (same pools, clearings and starts for the same seed), over a mechanic sketch: deep slough beds (`deepCore`, four vertex steps in), mudflat on a share of the swamp's banks (`mudflats`, sand kept near homes), moss over the wet reaches, thicket hammocks drawn as tiles (`rockVertices`) on the broadest dry ground and dropped if they part the ground, `openRoutes` fords repainted boardwalk, looks via `composeLooks` with a look-only season. | A growth-pressure map can intentionally creep into bases; the default levee is only 70% closed. Treat this as a difficulty/style choice and test AI survival, not as an AI-safe template. Deep cores two steps in left no pure shallow tile: algae fell from ~1,750 tiles to 7 and the AIs' wood harvest halved; keep a shallow band two tiles wide. Moss only pays in the growth tool when it covers most of the swamp, since every grass/moss boundary tile scores nothing; two Fertile looks side by side also score nothing, so measure food with a single-look season. Mud banks barely move the tool, which counts only pure sand as an inhibitor. |
| [Coral, 21](../../../../src/map/generator/generators/CoralGenerator.cpp) | One branching fan grown breadth first and rotated into every colony's share; branches refuse collisions with all copies, with optional bridges. Neutral sand-ringed expansion courts widen selected junctions, reserve an accessible building footprint, and keep crop growth outside their clear grass interiors. | Random structure can preserve a recognizable silhouette. Collision checks must include transformed copies; growth depth, taper and angle must scale with usable space. Keep supply trunks open and put prizes on remote tips. |
| [Rain shadow, 27](../../../../src/map/generator/generators/RainShadowGenerator.cpp) | Revision 7 (2.0): periodic slanted Ridges-group rock ridges (`rockVertices`, drawn a tile thicker than asked and thickened until a flood with the passes shut cannot cross), staggered sand-floored passes with scree mouths, windward streams with spring seeps against the rock, lee scree skirt then sand graded into dunes; lattice homes snapped to equal phase with the sway faded across their valleys, one quarry face per home stamped from colony 0's frame, ambient faces by `stone-amount`; look-only climate and lithology via `composeLooks`. | Directional economics gives the map a grain. Pass approaches need continuous sand connections into useful routes, not short decorative strips that end in regrowing grass. Measure a band's thickness in tiles (phase over its local gradient), not phase: a steep sway halves it. A slanted pass's flanking convex-corner tiles walk, so a seal flood must shut a pass with its flanks. Scree on the windward foot cost 6% of growth potential; on the lee, in front of the sand, it is free. A spacing rounded to a whole count per turn of a periodic field has only as many live steps as counts: step the control so each one changes the count at the default size (16, 32, 48 here). |
| [Rice terraces, 52](../../../../src/map/generator/generators/RiceTerracesGenerator.cpp) | Rain shadow's slanted stripe field turned into terraced hillsides: a dry crest of towns, narrow contour strips of rice and channels down both slopes, a valley with a river; the contours sway in a few whole-number waves along the hillside plus a fine grain; stairs straight across the terraces continue as valley roads that ford the river. Towns are spread along the crests' middle line by farthest-point selection, and the terraces bend round each town inside a sand ring. | Built after the concentric-ring version (now Hills) failed to read as rice terraces: start from the landscape's own signatures, not the nearest primitive. A single shared sway for every contour keeps widths and never folds. A lattice slid onto crests packs towns together; pick sites on the crest itself. Count hillsides per 256 tiles of side or a 512 map has valleys twice as wide. |
| [Old growth, 28](../../../../src/map/generator/generators/OldGrowthGenerator.cpp) | Dry wood cover outside independently jittered, protected, well-watered homes; rare distant lakes and hidden groves; optional starting trails. | Clearable barriers and permanent barriers produce different games. Dry forest preserves the history of cutting; hidden prizes should be priced in cutting cost, not straight-line distance. |
| [Drumlin field, 43](../../../../src/map/generator/generators/DrumlinFieldGenerator.cpp) | Teardrop hills all pointing one way under a `Grain`, packed round relaxed dart sites (`packLandforms`) in a water matrix; wandering sand eskers on a near tree plus loops are the only dry routes; each home's head is town, a sand collar parts it from the farm tail, orchards on the farthest drumlins. | Anisotropic packing gives a swarm look with a guaranteed water gap; shut-esker isolation and collar continuity are validated on the finished map. Everything is fertile by design, so farm tails grow shut while the beach rim stays the permanent road. Take the grain heading that keeps crowded homes apart rather than refusing; shrink homes only to a documented floor. A cheap request check plans only the homes. |
| [Polder, 30](../../../../src/map/generator/generators/PolderGenerator.cpp) | Periodic crop/water rows crossed by sand dykes over ditches and crops alike, grass villages/hamlets and clear farm plots. | Abundant food can produce a logistics game rather than automatic prosperity. Open village ground, anti-growth rings and remote inn plots are necessary complements to fertile rows. |
| [Karst towers, 54](../../../../src/map/generator/generators/KarstTowersGenerator.cpp) | Turing-pattern tower thickets; rivers as banded forwards-only cheapest walks between rows of lattice homes; bank terraces of alternating wheat and flooded ribbons with one-corner bunds; gated tower-ring home bowls from one stencil; doline lakes with sealed field rings; sinkholes and chains of pools. | Place a shared feature from the homes for fairness, and measure growth potential near homes rather than water share ([tuning playbook](tuning-playbook.md#measure-growth-potential-not-water-share)). Shrink homes, then rivers, before refusing a crowded map. |
| [Bajada, 55](../../../../src/map/generator/generators/BajadaGenerator.cpp) | Revision 4 (2.0): swaying Ridges-group rock ranges with quarry faces (one per home, found in the stencil's frame) and scree skirts; rows of alluvial fans grown as distributary trees; six home fan stencils stamped over an erased footprint, towns ringed by pavement; pavement only beyond water's reach; hardpan playa with salt crust, lakes and spring-meadow cienegas; mechanic and look sketches joined by `composeLooks`; a look-only lithology. | Barren pavement inhibits growth like sand and reaches mirrored probes far beyond the water's reach: putting it on the old grass gravel cut near-home food by a third, so measure growth with a rules-aware tool before and after. Rock vertices only where all four tiles are rock keep the blocked region exact and leave quarry tiles four grass corners. Compare stamped homes by tile rules, not types, and leave out the unstamped wall behind them. Every home design must grow a real stream tree on every seed: one trunk drawn too near the town grew none. Pin every design and measure it against the previous revision: Canyon mouth was 3 below revision 3 within 48 steps until its spring and arms grew. |
| [Central Quarry, 56](../../../../src/map/generator/generators/CentralQuarryGenerator.cpp) | Natural country (Biomes woodland and farmland without outcrops, zoned by distance from the centre) round one rough lake in the middle of the map. The lake has an island holding the map's only stone, reached by sand bars that all land at the lake's narrowest crossing, with a sealed garden on the far shore. Spline streams with jittered sand fords drain into the lake, and an open common rings it. Sites are spread by walking distance inside a band of equal walk to the isle, with a swimming-distance floor. Dry starts get ponds dug off their routes, trails are cleared to the isle, and kits are protected from every clearing pass. | A natural map can be fair on one objective without plots: search in a walk band, then keep everything placed later off the routes you balanced. A contested prize must be inside the AIs' working range (walks capped at about 70 steps on 256). It becomes holdable through geometry (one landing) and a foothold (the garden), and only a person can test whether it holds, because AIs never garrison a chokepoint. Eight review rounds; see the [tuning playbook](tuning-playbook.md#a-shared-prize-the-ais-must-want-and-a-person-must-hold). |
| [Hidden Oasis, 57](../../../../src/map/generator/generators/HiddenOasisGenerator.cpp) | Dry canyon country (desert as the driest share of a noise field, kept off springs and homes; washes turned to one drainage; mesas) round a keyhole plateau of stone. One winding slot with a pinch per colony on its axis; level-1 towers on alternate sides on ledges carved a fixed wall back from the slot's floor; every box canyon runs forwards in its own lane to the mouth's cliff; a basin with the map's only algae, a sealed garden and validated building room. Sites from Central Quarry's shared walk-band search on the walk to the mouth; a granted school. | A lock made of the colonies' own buildings has to be serviceable and breakable under the lock's own rules (build levels, ammunition, armour). Put the prize inside the haul door to door. Make two fairness walks the same walk. Construct fittings from their constraint instead of searching for them. Three reviewer rounds and 48 games; see the [tuning playbook](tuning-playbook.md#a-shared-prize-the-ais-must-want-and-a-person-must-hold). |
| [Braided river, 42](../../../../src/map/generator/generators/BraidedRiverGenerator.cpp) | Revision 5 (2.0): antiphase sinusoidal channel threads in lanes across a belt along the long axis make lens-shaped bars; crossings are read off the rasterized water (`channelCrossings`) and riffles (shared `SandFord`s: `stampFord`, `fordFault`) laid on a random spanning tree plus loops and one forced crossing per colony; dry terraces of dry grass and pavement beyond the water's reach; Ridges-group rock bluffs sealed round the seam (`traceSealedLap` spine plus a tile either side, `rockVertices`) with a buttress quarry face for every colony and ambient ones, scree aprons; moraine knots of rock two rows deep with stone flanks; deep pools at the widest stretches (`deepCore`, opened); mechanic and look sketches joined by `composeLooks`; a look-only season. | Read the route graph back off the rasterized water rather than trusting the plan: try crossings on the sketch, not by centre-line distance, or inner threads get none. Split request-only checks (`validateRequest`) from seed-dependent design failures the service should roll again. Keep towns above the growth probe's reach so they never overgrow, and stock the promised bar so no start starves across water. Without the bluffs at the torus seam the walk round the back was shorter than the braid; prove the rock seal with a walkability flood that ignores deposits. Rock two rows deep costs twice the bank farmland a line of stone did: shorten the knots and keep them off the homes. A terrain dump once recorded every tile between two looks of one group as mixed, which made the 2.0 map look a third poorer than it played: measure looks by rule group. Smoothstep value noise alone draws straight-sided patches; use fractal noise for pavement and look splits. Give a knot a quarry flank only where it is long enough to stay a knot (five columns): three-column knots with stone flanks read as specks. Report a walk spread as unknown when a colony cannot walk at all. |
| [Portage Lakes, 65](../../../../src/map/generator/generators/PortageLakesGenerator.cpp) | Revision 8 (2.0): Boundary-Waters lake country; long spline-stroked lakes, four scored settlement proposals over up to 24 landscapes, grass bay farms and open shore meadows, dry 75% forest with rock knots, sand-reserved routes repainted as trail, wooden portage plugs between trail approaches, a measured swim shortcut; Ridges-group rock knots (`rockVertices`, slivers keep their wood), one quarter-turned home-knob stencil per colony with an equal quarry face, placed right after the home bays (before the shore fields, neutral bays, trails and portages; placed last only when that layout fails), ambient faces by `stone-amount`, scree aprons on open ground only, mud on far lake beaches, `deepCore` lake centres; mechanic and look sketches joined by `composeLooks`; a look-only shield (granite, greenstone). | Reserve routes as sand and repaint the corners they took as trail afterwards, so the shared router stays untouched and trails stop inhibiting crops. Loam bay farms were dropped because their watering reached the dry forest a portage needs and a compact map lost its portage: a fertile feature waters its whole surroundings, so check every structure that depends on dry ground. The loam maps also bred fewer workers in seat-rotated AI games, which matches the AIs' Fertile-as-water defect: fix the AI, not the map ([terrain palette](terrain-palette.md#fertile-ground)). A rock tile the rock cannot block should keep the obstacle it replaced (here wood), or forest walls leak. Mud on beaches and trail instead of sand raise food without any new water. Place a per-colony feature that must fit beside every home before the optional features that compete for the same ground: placed last, the knobs fell back on 6 of 16 maps; placed early, on none. A shortcut over deep water must be validated in walking time, not steps: deep cores and fast trails left one swim that saved 25 steps saving 8 tiles of walking time. |
| [Braided Delta, 36](../../../../src/map/generator/generators/BraidedDeltaGenerator.cpp) | An inland delta (the Okavango): a gorge through a rock range, a deep panhandle river crossed on foot only at boardwalk bridges, an elliptical fan of forking, meandering arms (`growBranches`, a two-sine `meander`) that thin and die out as dry sandy beds; zones by fan radius reaching out along the channels in fingers (marsh swamp with islands and lagoons, loam floodplain, fringe, Kalahari desert); fords on a spanning set plus a cheapest-crossing repair; homes spread by walk round the swamp's rim before desert features are laid. Revision 6 replaced a ladder of identical islands. | A coastal trunk-to-sea delta read as Watershed: start from what only this landscape has. Mixed water depths remain swimmable. Validate bridge-only crossings for walkers; swimmers may cross the river directly. The validator counts tiles and a thin strip of land corners makes a piece of tens of tiles, so a repair counting corners needs a far lower threshold. The starting-crop guarantee plants only on plain grass: paint home clearings plain, not lush or dry. Lay desert decoration after choosing homes, outside their clearings, or it breaks the room a home needs. |
| [Continents, 44](../../../../src/map/generator/generators/ContinentsGenerator.cpp) | A real continent from the compiled world atlas (`WorldAtlas`, `Raster`, `Landmass`): majority-resampled coast, lakes, deserts as sand, ranges as scree, great rivers with fords; climate classes furnished with biome kits; sites spread by walking distance (`farthestSites` over tiles), scored by fertile grass, walkable catchment and river clearance, recentred with an undo, ponds dug until a fertility floor; routes opened through scree and across rivers. | Fixed geography means fairness is measured, not constructed: spread sites by walk, give each colony a guaranteed square and what the geography left out (water, a kit), and let the lobby's best-of-rolls and the tournament judge. Fertile-grass windows see ground, not the way to it: add a catchment floor. Features whose footprint is fixed in tiles (a river's three-tile scar) must be omitted below the map size that can afford them. Invented islets round a real coast were the first thing a maintainer asked about; they are off by default now. A toy, not a tournament map, and honest about it. |
| [Lava shield, 51](../../../../src/map/generator/generators/LavaShieldGenerator.cpp) | A volcanic island: a crater lava lake with a reserved walking rim of loam (the contested prize, renewable farmland and fruit), branching downhill tongues of live lava with correlated turns (`downhillPath`), one scree ford on every long tongue, cooled side flows of scree and a stone crust along the flows as an inexhaustible quarry, unequal green wedges, towns in one of seven shapes per map with a frayed sand edge every town shares, chosen by scoring completed candidate settlements (`ScoredSettlements`, `spreadRankedSites`) with absolute room and access floors, a bounded starter-field retry that seeds the patch with frontage rather than the single most fertile tile (`seedForPatchCapacity`), swimming-prize islets on the seams (`raiseIslands`). | While the tongues were stone, legal stone could not touch water and the beach pass left walkable sand between them, so a tongue drawn into the sea never sealed a coast: long tongues made narrow beach detours, short ones broad gaps. Lava terrain (revision 5) runs into the sea with no beach and seals its two wedges, so each long tongue now keeps one scree ford: when a barrier moves from a resource to terrain, decide again every gap the old rules left for free, and validate the fords on the finished map. Score whole finished candidates, not bare terrain, when the wedges are unequal by design. |
| [Savannah](../../../../src/map/generator/generators/SavannahGenerator.cpp) | Open plains with jittered lattice homes (`jitterSites`), crop plots sealed in barren red earth (`stampContainedPlot` with a dirt seal; sand before the terrain catalogue) beside small home ponds, ridge-rock kopjes with stone on their flanks, brick-path game trails, two opposed five-tile exits per home, neutral watering holes placed to be locally contested. | Earth seals exactly as sand did and can be built on, so the containment proof survived the change of terrain unchanged. Enlarging plots for growth room is safe only if the plot edge facing town stays where it was: moving the wheat edge toward town made Numbi place no food inn. No generic repair: essential shortfalls fail, optional deposits saturate inside their plots. A plain reads as empty without its scenery: pools where nothing is planted and lone trees on growth-chance-zero ground add the look without touching the containment proof. |
| [Locust](../../../../src/map/generator/generators/LocustGenerator.cpp), Vultures until 2026-09-16 | Old Growth's clearings and lakes with finite dry wheat (zero growth probability, three to five harvests a tile set after every repair) instead of forest, wood only on shores, fixed dry rations and a quarry per home, `roomyLatticeSites` for prime colony counts, followed by bounded independent home jitter that preserves their radius and separation. | A finite-food concept must forbid renewable repairs and give fertility zero weight in the start scorer. Spend a starter budget across disconnected pockets (`growPatchesNear`) rather than failing on the first sliver. A probe that changes winners in both directions across paired seeds is not evidence for a default; a maintainer's reading of the concept (the fields are the obstacle, so fill them in) is, and the cover default went from 35% to 65% on it. |

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
- A stone ridge fixes an impassable ground boundary while allowing tower fire across it, and is
  also the colonies' quarry. A terrain wall chooses its fire rule: Ridges let towers shoot
  over, Obstacles stop arrows; neither yields stone, so plant quarry faces at its foot
  ([a wall was also a quarry](terrain-catalogue.md#a-wall-was-also-a-quarry)).
- A wood barrier costs worker time to cut and may return if watered.
- A sand flat supports travel and denies buildings/resources; an embedded grass patch becomes
  a strategic staging point. Where the subject is not a desert, Rough ground (mudflat, scree)
  denies buildings and crops the same way, slowly; Barren ground denies crops but not buildings.
- A pond supplies potential growth as well as consuming building space and, where the map lays
  beaches, creating them.
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
are distinct effects: a nearby sand tile is not a blanket zero-growth radius. In the terrain
catalogue Barren ground inhibits the probe exactly as sand does, while Paths and Rough ground
inhibit nothing; crops spread onto none of them. A barren ring therefore contains as a sand
ring did, and a paved road keeps crops off itself without slowing the fields beside it.

Four useful patterns emerge:

1. **Protected settlements beside productive country.** Polder's village ring and Old growth's
   clearing ring use two vertex layers of sand to resist bridging through the terrain
   conversion. Polder removes home wheat/wood blocks because the external rows supply them and
   the interior needs building room. Old growth excludes home wood while seeding wheat around
   several pools. Validate both the ring and harvest access across it. A new ring can be Barren
   (`dressFarmSand` turns a farm's caps and plot rings to it), which can also be built on.
2. **Permanent travel corridors through fertile terrain.** Coral traces sand down branches that
   carry through traffic, but keeps pads and dead-end buds free of that road to preserve their
   small grass area. Rain shadow extends sand through passes and connects it to stream beaches.
   A route opened by clearing crops once can grow closed again; sand-based protection persists,
   and so do Paths and Rough ground, on which nothing grows. A path can still be built on, so
   use Rough where a building must not close the way either.
3. **Ground that never grows.** Barren pavement is building room nothing grows on, but it
   inhibits like sand, so lay it beyond 22 steps from water; paths stay open and inhibit nothing;
   rough flats are unbuildable without inhibiting ([terrain palette](terrain-palette.md)). Tidal
   flats' mud and Bajada's pavement are the two ends of that trade.
4. **Dry clearable barriers.** Old growth keeps extra lakes away from homes and checks the
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
takes each tile's rules from its four corner vertices, so beaches consume more usable room than their painted
outline suggests. The same holds for catalogue terrain: a tile whose corners mix two rule
profiles is never buildable, so a wall drawn naively on vertices leaves an unbuildable seam
beside it (`wallVertices` leans walls away from open ground;
[walls on vertices](terrain-catalogue.md#walls-on-vertices)). A thin pond can become almost all beach; a narrow island can keep walkable
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
[model fitted to real games](../../../../docs/map-generators/fairness-model.md) turns those into
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

- **Karst towers:** place the shared feature from the homes, not the homes around the feature.
  Its first rivers were routed across the map first and passed two homes while missing two others
  (walking distance to the nearest paddy 32/48/61/97 on one seed, start fairness 0.67–0.84). Homes
  now stand on a lattice, rows are found across one axis, and a river winds only inside a band in
  the middle of each gap between rows, so every colony has a river at the same distance
  (fairness 0.86–0.98). A banded river may only step forwards along its axis, which also removed
  hairpins that a free cheapest walk made when a waypoint lay far off the line.

- **Central Quarry:** balance the walk to the shared prize, not the neighbours.
  - Candidates are roomy, watered grass within ±(walk ÷ 14) steps of a target walk. The target is a percentile of the roomy ground's walks, capped at 70 steps plus 6 per colony beyond six, and grown on bigger maps.
  - Each candidate's straight-line distance to the isle must be at least 80% of the target.
  - The spread keeps looking until its nearest pair reaches min(60, 0.45 of the ring's circumference per colony), and refuses below 28.
  - Picks prefer a 48-step yield between the 60th and 90th, later 70th and 90th, percentile of a sample of candidates, then roomy ground, then any.
  - Measuring yield as mean fertility over a square instead let one colony start with a 24-step yield of 15 beside neighbours on 36–67.

Tune the weak start and the unmeasured opportunities: expansion room, approaches to shared
prizes, who meets whom first, number of defendable doors, and post-swimming access. Do not tune
weights solely to make a score rise. [The fairness tournament guide](../../../../docs/map-generators/fairness-tournament.md)
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

Prefer the shared components already used here: `TerrainSketch`/`pureTiles`/`layBeaches`
(beaches are optional on catalogue terrain; run `layBeaches` before drawing any),
the catalogue helpers (`wallVertices`, `cliffCoast`, `dressFarmSand`, `barrenBase`, `paveCore`,
`plantQuarryFaces`, and `paintClimate` as the last step),
`composeLooks`, the rock walls, quarry faces and talus of `Rock`, the deep cores, levees, pavement
and gardens of `HeightFieldLooks` ([terrain palette](terrain-palette.md)),
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
