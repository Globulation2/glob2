# Tuning a generator until its games work

Read this after the design brief and before the first AI game. A generator that validates, scores well and looks right still has to be played, and the defects that games expose recur from one map concept to the next. This reference names those defects, compares the remedies the shipped generators chose, and describes the measurement loop that finds them. The generator sources cited are the record of what was measured; their numbers are tuning results, not balance promises.

## The first failure is the opening economy

Nearly every generator that reaches AI play fails first in the same way, and it is never geometry: one or more colonies stop growing before any fight. In telemetry the colony's unit count peaks under about a dozen by tick 15,000, `hungry` and `critical` run near its population, and starvation deaths accumulate while the map still validates, every route is open and the start scorer is content. Check this before measuring fairness; a map whose starts starve has no fairness to measure.

The causes, roughly in order of frequency:

1. **Dry starter crops.** A kit on ground the growth probe cannot water is one harvest, and the AI eats it before its own farm exists. Forts' first prototype had one percent water; the first Glacis' compound, Allotments' home fields and Caravanserai's capital (revision 1) were dry by design until play showed the consequence; Locust makes finite food the concept and must therefore forbid renewable repairs.
2. **Renewable crops too far from the swarm.** the first Glacis' nearest bank was 45 tiles beyond the gate, and Allotments' nearest ditch two blocks away (revision 1); Breachable Highlands' due-west start made the first food route circle the farm ring. Measure distance to food from the workers' tiles by walking, after crops and beaches exist, not from the swarm centre.
3. **Too little frontage or too small a pond.** Hedgerow Country's 16-water-tile pond fed a mirror game for a while and then starved it; a 100-tile pond with wider capped plots fixed it. Merely adding starter wheat made Maxima expand faster and starve harder.
4. **The first inn's food arriving after the first hunger.** Hills' (then Rice Terraces') summit cap put the first stocked inn across a sand ring from the crops; a small completed, stocked inn halfway to the first stair supplies the first meal. Preplaced towers with an empty stone store recruited three of the four workers for stone before that inn had food.
5. **A single starter patch on the wrong side.** One patch made Hills' opening haul depend on which side of town the AI chose; seeding wheat beside every entrance fixed it. Wood needs one patch only, because its faster spread must not compete with wheat at every entrance.
6. **Ambient furnishing consuming the kit's ground.** At wheat 300% and wood 0%, Braided Delta let ambient wheat, stone and fruit occupy the wood kit's bank, and the emergency top-up then planted wood inside the sand-rimmed town. Plant guaranteed kits before any ambient layer.
7. **Room bought with distance.** Bajada's fourth revision moved each town onto dry gravel beside its fan, which doubled town room and evened food between colonies, and put the kit and the water 12–16 tiles from the swarm: every AI on seed 202 food-capped at 15–36 units, where the previous revision had grown 55–81. Wheat within 12 tiles of the swarm went from 16–56 to 0. The fix was a garden: the swarm on the fan's side of the town, the kit's wheat in a band between the ring and the streams (kept seven tiles off the town), and a small pond at the garden's far end. A pond first drawn on the kit's own ground displaced the kit and slowed the opening again; move water beside the kit, never onto it. Count wheat within 12 tiles of the swarm on every revision; the stencil being identical says nothing about where its food is.

8. **A later pass that clears or trims the kit.** Passes that run after the kits can take them away without any check noticing. Central Quarry (2026-09-17) found two:
   - A trail to the objective cut straight through one colony's kit. Its route clearing removed every deposit within a tile, and that start failed with every AI on the seed.
   - A cosmetic trim of straight field edges removed the ambient fields round several homes, and a seed whose colonies had grown to 172–236 units food-capped at 28–115.

   Protect kit tiles from every clearing pass (routes, trails, sliver clean-up) by recording what the kit planted and passing it as a keep mask. Keep look passes a fixed distance (20 tiles) off every site. Validate a wheat floor near every swarm so the next pass that eats a kit is refused rather than shipped.

The remedies, compared:

| Remedy | Used by | What it costs | When it is the wrong tool |
| --- | --- | --- | --- |
| A small pond inside or beside the home with wheat and wood on its shore ("a well") | The Glacis (two cisterns in the kitchen gardens behind the courtyard's middle), Allotments (the village block's ditch), Caravanserai (the home oasis's lake with its field ring), Continents (dig up to three 32-corner ponds until mean fertility reaches its floor), Hedgerow Country (radius-5 pond) | The beach spoils a few tiles behind the pond for building; the pond's sand slows regrowth, which inside a compound is the point | Finite-food concepts (Locust); maps whose water is the front (Braided river keeps towns dry on purpose and puts renewal on the bank strip and the bars) |
| Sand-capped crop plots so growth fills the plot, not the town | Savannah (sealed grass islands in sand), Hedgerow Country (ringed pond plots with a divider), Breachable Highlands (farm ring plus spokes), Hills (concentric caps), Rice terraces (caps above and below every slope and a ring round every town), Plantations (one-vertex plot ring), Braided Delta (town rim) | Sand beside a crop reduces its regrowth; every cap is unbuildable | Maps that want overgrowth as pressure (Everglades, Drumlin field's farm tails) |
| More guaranteed tiles, kept unscaled at 0% | Emoji (48 wheat and 48 wood on the shore plus a close supply of 24 and 16), Forts (32 per plot plus scaled surplus), Caravanserai (at least 60 wheat and 30 palms on the home's ring), The Glacis (at least 40 wheat and 20 wood in the gardens) | Larger farms cost building room; Hedgerow Country showed more starter wheat alone can make hunger worse | When the shortfall is regrowth rather than stock |
| Separate wheat plots from wood plots | Breachable Highlands (sand spokes; three quarters of the annulus to wheat), Hedgerow Country (divider), Drumlin field (12% wood versus 30% wheat cover) | A plot per crop needs two seeds and two frontages | Not needed where sand or water already separates them |
| Plant the kit where the AI measures it: a solid block of wheat nearest the swarm, water behind it rather than inside it, the swarm on the most open ground of its home | The Glacis (wheat band from the garden line back, cisterns at the gardens' back), Caravanserai (wheat arc of the field ring nearest the town, swarm on the widest town tile) | Nothing but planting order and a few tiles of layout | Only for AIs that estimate food from one contiguous patch (Numbi); does nothing for regrowth |
| A stocked completed inn or granted buildings | Hills (one small inn), Plantations (pool and inn on every granted island), premade bases (a whole base; The Glacis, Allotments and Caravanserai until revision 2) | Changes the feel: the opening is skipped or shortened | Ordinary landscapes, where the opening is the game |

Wheat carries the engine's extra one-in-three growth gate and wood does not. Equal wheat and wood areas therefore make a food-poor farm: the generators that feed an AI give wheat two to three times wood's area or cover.

## The AI has to work the plot, not just own it

Contained plots protect the town from its own crops, and the remedy table above recommends them. Rotation tournaments show their cost: a Nicowar colony's harvest, not the plot's regrowth, is what limits it, and the same colony harvests very differently on different plot shapes. Mean wheat harvested per colony in a 45,000-tick four-Nicowar game at 256x256, with the births and peak population it bought:

| Plot design | Wheat harvested | Births | Peak units |
| --- | ---: | ---: | ---: |
| Lake shore field ring and palm grove in a large oasis town, caravanserais between towns (Caravanserai, first rebuild) | 1301 | 135 | 120 |
| Star fort with kitchen gardens behind the courtyard, woods and streams outside (The Glacis, first rebuild) | 1085 | 105 | 107 |
| Village block of plots by a ditch, garden sites of strip plots between villages (Allotments, first rebuild) | 995 | 97 | 100 |
| A whole volcanic island's slopes, coast and crater rim, homes scored on the finished world (Lava shield) | 424 | 292 | 216 |
| Open river banks and bars (Braided Delta) | 320 | 160 | 148 |
| Farm ring with sand spokes and a pond (Breachable Highlands) | 305 | 98 | 96 |
| Real geography, kit on fertile grass, dug ponds (Continents) | 244 | 162 | 139 |
| Dry terrace with a bank strip (Braided river) | 239 | 109 | 94 |
| Walled fort with two irrigated, sand-rimmed plots (Forts) | 211 | 85 | 84 |
| Sand-ringed plot inside a sea-watered crop band (Plantations) | 201 | 189 | 144 |
| Fertile drumlin tail behind a sand collar (Drumlin field) | 159 | 180 | 139 |
| Finite dry fields, 35% cover (Locust) | 151 | 148 | 128 |
| Finite dry fields, 65% cover (Locust after review) | 127 | 194 | 166 |
| Concentric capped bands with stairs (Hills, then Rice Terraces) | 146 | 189 | 138 |
| Fixed home module: sealed wheat and wood plots, a service court, near timber bays, in a pocket of a folded river (Hilbert River) | 180 | 47 | 51 |
| The same module in a district between nested lakes (Sierpiński Gardens) | 123 | 37 | 37 |
| Sand-ringed pond plot with a divider (Hedgerow Country) | 109 | 55 | 55 |
| Small sealed grass islands in sand (Savannah) | 66 | 24 | 25 |
| The same plots with a third more watering holes, pools and bigger ponds (Savannah after review) | 88 | 30 | 30 |

The first three rows are revision 2 candidates of the rebuilt maps, measured later (2026-09-16) with `harvested_1` from the final `GLOB2_MEASURE` row by `scripts/tournament_starts.py`; their wheat counts run far above the older rows' for similar births, so compare births and peaks across the table, and harvests only within a run.

Under about 150 wheat in that time a colony never grows, whatever else the map offers; a home module engineered for an AI's opening (service court, timber bays, sealed plots) still sits in that band, because the sealing is what caps the harvest. The two lowest rows are the fully sealed circular plots, and neither responded to its obvious lever: giving Hedgerow Country's wheat two thirds of the plot changed nothing (110 harvested either way), and doubling Savannah's home pond changed nothing (66 to 85); a third more watering holes, pools on the plain and bigger ponds together lifted it only to 88, a fifth more units and no other change. Regrowth was never the bottleneck; the AI simply does not work a small ringed plot. The Locust rows show the opposite lever: filling the finite fields in from 35% to 65% cover cut the harvest from 151 to 127 yet raised the peak from 128 to 166 units and warriors from 50 to 90 while starvation and eliminations fell to none. Fields the AI holds rather than clears are food it keeps and walls it keeps behind; a harvest count reads as work done, not wealth held. Rings with spokes and several entrances (Breachable Highlands, Rice Terraces) are worked; open shores are worked best. So before building a whole map on a sealed-plot economy, put one plot beside an AI colony and read its harvest; and when a tournament shows a flat population with few deaths and few births, look at the harvest before the fertility.

Premade bases were their own case, and why The Glacis, Allotments and Caravanserai dropped them in revision 2: in rotation tournaments at 45,000 ticks (2026-09-16) a finished base of fifty-odd units bred 14 to 15 times a colony with Nicowar and 4 to 9 with Numbi, and Caravanserai's capitals starved out in 67 and 70 of 96 colony-games; only Allotments' base of construction sites bred (82 births with Nicowar), because the AI had to build it. A premade population is not a growing economy.

## Ground a colony can reach beats ground it can see

The second recurring defect is measuring room or fertility as area rather than as walkable catchment:

- Continents' fertile-grass window scored a colony inside a river loop at a thousand while its 24-step catchment held under four hundred tiles. The fix was a walkable-catchment floor and a clearance from rivers on top of the fertility window. Counting grass alone had moved sites inland, away from the water, and traded one failure for the other.
- Hedgerow Country's equal straight-line home spacing left some starts closest to four times as much land as others. It now scores each candidate arrangement by sampled walking territory (one tile per 4x4 block, an eight-neighbour flood over the designed obstacles) times a nearest-rival contact ratio, and keeps the best product.
- Braided Delta's fords were intact while every colony pair was unreachable: bank crops had closed the ground between town and ford. A ford is not an accessible ford. Every crossing end now gets a sand approach to the town rim on its own island, with islands labelled before the fords join them so the route search cannot service the wrong bank.
- Hills' summit was 16 tiles wide by design, yet a bulk seed found crops on it: the generic crop rescue had planted on the dry town. Give repairs a mask of where emergency crops may go.
- Emoji's inverse outlines put every colony on a rim ribbon a dozen tiles wide, and the start beside a facial-feature junction lost every rotation game: attacked along the ribbon with no room to build behind its crops. Room on a ribbon is the stroke's width, and the width had been chosen for the drawing, not the town.

Use the start scorer, a fertile-grass window, catchment floods from the actual worker tiles and the report's per-colony metrics together; none alone predicts these.

## Measure growth potential, not water share

Water on the map is not food. A crop regrows when a random probe up to fifteen tiles away lands on pure water and the mirrored probe misses pure sand, so what feeds a colony is the **density of pure water right beside its crops**, and a map can hold a river, lakes and ponds and still starve. Karst towers' first review (2026-09-16) found 5% water, a quarter of Forts' growth potential near every home, and Maxima colonies breeding on the starter stock and then starving out, although every metric the generator already reported looked fine.

Measure it with the engine's own kernel: [`scripts/growth_potential.c`](../scripts/growth_potential.c) reads a terrain dump, computes every tile's growth chance exactly as `Map::growResources` weights it, and sums the chance over farmland (pure grass or wheat) walkable from each colony within 24, 48 and 96 steps. Call that sum "yield", in full-fertility tiles; one yield tile holding wheat regrows about 5.4 times per thousand ticks, which matched the harvest rates in games. Compare with a reference generator on the same seeds:

| Map, 256×256, 4 colonies | Water | Total yield | Yield ≤24 steps | ≤48 steps |
| --- | --- | --- | --- | --- |
| Hedgerow Country | 1.5–1.7% | 341–392 | 14–24 | 20–49 |
| Forts | 13% | 1,530–1,650 | 28–47 | 61–156 |
| Braided river | 10% | ~1,720 | 44–91 | 95–207 |
| Everglades | 30–32% | ~2,200 | 41–91 | 115–301 |
| Karst towers, first review | 5–6% | 513–603 | 11–14 | 11–54 |
| Karst towers, shipped | 11–15% | 1,662–1,938 | 28–56 | 52–237 |

What the measurement found, and what fixed it:

- **The home water is the lever.** A one-row channel and a pond of radius three gave the home paddy a 3.7% growth chance. The yield within 24 steps is almost all home ground, so no amount of water further out moved it; a wider channel and a bigger pond on the clearing's rim did.
- **Water narrower than its beach is sand.** Flooding only the corners two steps inside a paddy's bunds left most paddies with no pure water at all, and a pool of one or two water corners becomes a ring of beach: the "30% flooded paddies" never appeared on a default map. Flood every corner inside the bund, so the bund is the paddy's only sand.
- **Sand beats the probe.** Bunds whose label noise wobbled per corner turned half of the paddy belt into sand-grass mix, and pure sand beside a crop blocks its regrowth. Jitter a cut line as a whole, cut less often, and let neighbouring fields share one bund.
- **Water where nobody farms is scenery.** Sinkholes 40–70 tiles from any home added 4% of the map's yield. Put water beside the fields the colonies will actually work: the home, the gates, the ground between neighbours.
- **Rows of crops between rows of water.** Alternating ribbons of wheat and flooded paddy along a bank (9 and 6 tiles deep) read as terraces and tripled the belt's yield; see `bestFarmRows` in `Farmland.h` for the row widths that yield most.
- **Every new water source needs its fields sealed.** Unbunded wheat round Karst towers' doline lakes filled a halo 6–10 tiles wide in 50,000 ticks, on the ground between neighbouring homes; the lakes accounted for half of all the tiles that changed in the game. Water makes the ground fertile, so anything planted near it must be contained (see the overgrowth section of [gameplay and playability](gameplay-and-playability.md)).
- **Water takes building room.** Two pools beside the swarm halved the bowl's 4×4 sites. Measure building room again after every water change, and move water to the rim before shrinking the clearing.

## Fair by construction, fair by search, or fair by measurement

Three fairness models are in use. Choose one deliberately and validate the thing it promises:

| Model | Generators | What it guarantees | What it does not |
| --- | --- | --- | --- |
| Construction: identical modules on a lattice or orbit (`latticeSites`, `Orbits`, `dealStarts`) | The Glacis, Allotments, Caravanserai, Forts, Hills, Savannah, Emoji's crossing ring | Same home, same kit, same walk to the first objective; stencils stamped by quarter turns (`turnStencilTile`) copy a whole home exactly (The Glacis' forts, Allotments' villages, Caravanserai's home oases), and Caravanserai proves equal caravanserai costs with `unevenCosts` | Neighbours: which rival is nearest and what lies between; lattice rows on rectangles give some homes two neighbours in a line |
| Search: candidate arrangements scored on the finished layout | Hedgerow Country (territory times contact ratio), Continents (fertile window, catchment floor, recentring with an undo when rivals come too close), Emoji (site refinement by the best fertile tiles plus room), Plantations and Braided Delta (farthest-point over islands, then dealt) | Bounded inequality in the measured quantity | Anything unmeasured; Hedgerow Country ships maps with a 7:1 expansion ratio and says so |
| Search in a walk band round a shared objective: sites spread by walking distance (`farthestSites`) only among candidates within ±(walk ÷ 14) steps of a target walk to the prize, with floors on the swimming distance and on the nearest rival | Central Quarry | Equal walk to the prize within twice the band; no start a short swim from it; no rival closer than the floor; all checked on the finished map | Neighbours and the ring's shape. When every route to the prize lands at one place (Central Quarry's single landing), the ring of equal walk is offset toward that side, so colonies bunch on it; penalising lopsided spreads fought the geometry and refused six maps in 90. It also misses anything placed after the search: ponds dug for dry starts crossed routes and spread one 512 map's walks from 93 to 126 steps until they were kept off every site's shortest routes |
| Measurement: the lobby keeps the best of five rolls by the start scorer | Everyone; the whole model for Continents, Drumlin field and Braided river | Rejects the worst rolls | Position bias in games: the scorer's rank correlation with win share ranges from 0.6 on good maps to negative on others |

Whatever the model, deal sites to team indices with `dealStarts` before any per-team array is indexed, and sequence named-stream draws in separate statements: two `bounded()` calls inside one expression are evaluated in compiler-dependent order, and both Forts and Rice Terraces shipped lattices that differed per platform until they were split.

## The measurement loop

Tune with games, not only the scorer. The loop, with the distributed tools:

1. **Build a Linux bundle** of the branch and register it with `tools.tournaments bundle`. Build on the host with the oldest glibc among the workers so one bundle runs everywhere.
2. **Play a rotation tournament** with `tools.tournaments.fairness`: four to six map seeds at 256x256 with four colonies, every cyclic rotation of team indices over the starts, one AI in every slot, 40,000 to 45,000 ticks, and `outputs.telemetry` set to `team-timeline` so the `GLOB2_MEASURE` rows are retained; add `saves: ["final"]` when the end state will be looked at. Set `settings.prefetch` to 0 and list the biggest host first, or the small hosts queue three waves of games while the big one idles. Do not put `map: true` in the game outputs: it is a generation output, and a game that cannot produce it is recorded as an artifact failure and retried. A 45,000-tick four-Nicowar game at 256x256 takes about four minutes of one core.
3. **Read per-start economy before win counts.** For every start slot, pooled over rotations: final and peak units, buildings, prestige, eliminations, combat and starvation deaths (`deaths_<unit>_<cause>` in the final `GLOB2_MEASURE` row, cause 0 combat and 1 starvation) and `hungry`. A start that never passes twenty units is the defect, whoever wins. A start that peaks at fifty and is eliminated in every rotation is being overrun, not starved; look at its neighbours and its routes.
4. **Then read position bias.** `reanalyze` gives wins by start pooled over rotations, an exact test per map and the RMS position bias per generator. With one game seed and four rotations a map has four games; only gross dominance is visible, which is what tuning needs to catch.
5. **Look at the maps that failed.** Render the played map with `--preview-map` and crop around the weak start; render the final save the same way. Most defects are visible: a start on a cape, a kit across water, a town inside a crop belt, a colony on a ribbon between two rivals.
6. **Change one thing, re-run the same seeds, then fresh seeds.** Pin the affected variant through `generator_params` so the paired runs compare the same kind of map. Every generator tuned this way found that fresh seeds kept some favoured position; report that honestly.

## Which AIs to play

Play-test and calibrate with the newer AIs only: **Nicowar, Cortex, Cabino and Maxima**. Read each one's results separately; each exposes a different habit, and a map tuned for one can starve another.

The older AIs, **Numbi, Castor and Warrush**, are not tuning targets (maintainer decision, 2026-09-16). Their stalls come from narrow local heuristics, like Numbi's straight-line wheat scan below, not from a map players would find unplayable, and bending geometry around them has cost several generators their look. When one of them fails on a map:

- Don't change the map for it, and don't spend a tournament or calibration run on it.
- Record the failure as a known limit in the generator's notes or PR if it's worth knowing.
- If the failure is a real bug that should be fixed, fix it in the AI. That changes existing games and replays, so it follows the repository's compatibility and review rules, as a separate change.

Rubble city's first calibration games (2026-09-16) show the split. On the same map, Nicowar grew to 48–145 units per colony and Maxima to 45–90, while Numbi stalled at 5–10 and Castor at 8–12, both harvesting food they never turned into units.

The notes below on each AI's habits stay as a diagnostic reference, including the older AIs, whose entries explain failures seen in earlier generators:


- **Nicowar** overbuilt swarms on premade bases and starved; it otherwise works most plot shapes and is the most forgiving measure of an economy.
- **Castor** cannot run a base of construction sites.
- **Maxima** clears routes other AIs cannot, hiding blocked approaches. A map that only Maxima survives is not tuned.
- **Numbi** places no food inn if the wheat plot's edge moves toward town, and has two gates a map can shut without noticing (`src/ai/AINumbiPlacement.cpp`, `AINumbiEconomy.cpp`):
  - *It builds only when its swarm stands in open ground.* `findNewEmplacement` scores the ground round the main building before searching at all; the score starts at 352 and must stay above 299, and the swarm's own footprint already costs most of that margin, so a sand path, a plot rim, a wall or a crop within about three tiles of the swarm means the colony never places a building. Caravanserai's first rebuild set its swarm a fixed distance forward, against the field ring's path: in a 45,000-tick tournament 72 of 96 Numbi colonies built nothing and died. Put the swarm on the most open ground of its home (the tile farthest from any other kind), not at an offset.
  - *It breeds only on a compact wheat block by its swarm.* `swarmsForWorkers` turns production off unless `estimateFood` of the swarm is at least three tiles per colonist (five to switch it back on). `estimateFood` walks from the wheat tile nearest a corner of the swarm along a row and a column, tolerating only a few non-wheat tiles in all, and returns width times height of that one rectangle. Narrow strip plots (Allotments' first village, four tiles wide: villages stalled at 13), a thin ring of wheat along a shore (Caravanserai), or a cistern in the middle of the patch (The Glacis) all measure small however much wheat there is. Because the scan runs along the map's axes, a home stamped by quarter turns measures differently in different facings: The Glacis' Numbi colonies reached 60 facing two ways and 15 facing the others; moving its cisterns to the back of the gardens and planting the wheat as a solid band nearest the swarm lifted the weak facings to 20 to 30 but left one facing at 60, and only one facing per map (every home a translation of the others) removes the bias. Plant the opening wheat as a block with no water or sand inside it, beside the swarm, and read `AINumbi.estimateFood.result` per colony in the AI telemetry of a short game.


**Compare revisions with the variant pinned.** The lobby's best-of-five roll picks a different home design or layout for the same map seed as a generator changes, so two revisions' games on "seed 202" can be different kinds of map; one Bajada A/B compared a Broad fan home against a Twin springs home without noticing. Pin the variant through `--param` for paired games, and remember that one game per condition is noise: two runs of the same map and AI differed by up to 40% in final units. Use growth potential and wheat near the swarm, which are deterministic, to decide between revisions, and games to catch collapses.
## Calibrate locally before a tournament

A tournament costs an hour of a cluster; a food-capped or unbuildable home shows in a few minutes on one machine. Before the first tournament, and after every economy change:

1. Generate the map the lobby would pick (`--generate-map --generator ID --map-seed S --candidates 5 --write-map true`) and the same seed of a reference generator whose AI games are known to grow (Forts, Hedgerow Country).
2. Play both with four copies of each newer AI (Nicowar, Cortex, Cabino, Maxima; see [which AIs to play](#which-ais-to-play)), 20,000 to 30,000 ticks, `--telemetry team-timeline` (commands in [`scripts/game_economy.py`](../scripts/game_economy.py)). Run them in the background side by side.
3. Compare per colony: wheat and wood harvested, worker births, starvation deaths, units over time and buildings. Absolute numbers depend on the AI and the tick count; the reference game is the yardstick. The Glacis' first rebuild harvested 155 to 291 wheat against Forts' 427 to 882 in the same 30,000 ticks, which found a cramped courtyard and gardens too small an hour before a tournament would have.
4. When one of those AIs stalls, read its own telemetry (`GLOB2_AI_FINAL`) for the gate that stopped it, such as a building count that never passes one. Then look at the home in a terrain close-up (`scripts/render_terrain.py`) around that colony's swarm; the cause is nearly always within a dozen tiles of it.
5. A map built around the local game can still fail on others: the tournament over several map seeds and every rotation remains the evidence, and its per-start table is where AI-specific and facing-specific failures show.

## When results split by start, find the design draw

A generator of identical homes should give identical starts. When a tournament's per-start table is bimodal instead (a start at 60 units in every rotation beside a start at 15 in every rotation, as The Glacis' first Numbi run was), the difference is something the design drew per colony, not the team index: a facing, a variant, a neighbour, a road or stream that happened to pass. Record every per-colony draw in generation telemetry (`glacis.fort.facing` per colony), then join it to the per-start economy (`scripts/tournament_starts.py --detail ID --telemetry-key KEY`). For The Glacis the join was exact, facings 0 and 3 good and 1 and 2 bad on every map, which pointed straight at an axis-bound AI scan rather than at the countryside noise; the cure was to draw one facing per map.

## Four shapes a tournament result takes

Rotation tournaments of six maps by four rotations at 256x256 sort generators into a few
recognisable shapes. Name the shape before choosing a remedy:

- **A doomed start.** One slot is eliminated in three or four rotations whoever plays it, with a
  low peak (50 to 90 units) and more combat than starvation deaths. It is exposed, not poor:
  an inverse-outline Emoji rim start between two rivals, a Braided Delta island whose approaches
  favour a neighbour. Geometry is the remedy (more room, another exit), not more wheat.
- **A food-capped economy.** Every start flattens at the same modest population (Forts at 35 to
  90, Breachable Highlands at 70 to 110) with few deaths of any kind and many buildings. The map
  feeds a town but not an army; only starts beside extra water (a river bank, a second lake)
  break out. The lever is watered, harvestable frontage within the colony's reach, and the
  cheapest test is an existing control (Forts' lake count, Highlands' extra passes) before any
  code changes.
- **A peaceful stalemate.** No eliminations in twenty-four games, combat deaths in single digits,
  every game adjudicated on prestige. Sealed valleys and long pass networks (Breachable
  Highlands) produce this with Nicowar; a prestige-adjudicated "bias" of twenty points is then
  noise, not unfairness. Decide whether the concept wants contact sooner; if so open more routes
  by default rather than tuning the economy. Check the walk before the pass count: when armies
  set out but starve on the way (warrior starvation deaths high, combat deaths low), the routes
  exist but are longer than a fed unit's hunger budget. Polder's rows under crop forced detours
  the length of each row until sand lanes crossed the crops as well as the ditches; see walking
  distance as a hunger budget in [gameplay and playability](gameplay-and-playability.md).
- **AI-specific collapse.** Maxima starving by the hundreds on Emoji's dry hinterland while
  Nicowar thrives, Nicowar overbuilding swarms on premade bases (until revision 2 of the three premade-base maps), Castor stalling on a base of
  sites. The map exposes an AI habit; record it as a limit unless the concept can cheaply feed
  the habit (scattered ponds on a dry plain), and never tune the geometry to one AI's bug. A
  collapse only Numbi, Castor or Warrush shows is not a map problem at all (see
  [which AIs to play](#which-ais-to-play)).

A generator can be under the fair-map floor and still have a doomed start on a third of its
maps, because the floor averages over maps. Read the per-map counts and the per-start economy
before the headline number.

## A shared prize the AIs must want and a person must hold

A map whose whole game is one contested site (Central Quarry's stone isle, 2026-09-17) raises three questions that fairness and the economy don't answer:

- **Will the AIs go for it?** At walks of 80–100 steps from the homes, the quarry went almost unmined in the first round of games: at most 4 stone per team on seed 1. With walks capped at 70 steps (more on bigger maps and with more colonies), Nicowar and Cabino mined 20–350 stone per team. Maxima never did, because it counts stone only within 20 tiles of its own buildings. Measure stone per team (resource 3 in `GLOB2_MEASURE`) before anything else. Record an AI that ignores the prize as a limit rather than bending the map to it.
- **Is holding it decisive?** Read the top miner's share of the stone, and whether the top miner was the biggest colony, over a set of games. Over eight review rounds the share went from a shared mine (about 45%, the biggest colony in 3 of 8 games) to a contest (50%, the biggest in 6 of 8, three eliminations). The changes that did it:
  - every sand bar landing at one place on the island, where a couple of towers cover all of them;
  - building room left at each bar's shore end;
  - a small sealed garden of wheat and wood on the island, so a holder can build and feed a garrison there.
  
  Check that nobody is locked out: every colony still mined stone in every game.
- **Is the prize inside the haul, door to door?** Measure the whole walk from the swarm to the prize, not to the feature's edge. Hidden Oasis' first build (2026-09-17) started colonies a fair 70 steps from the gorge's mouth, and the gorge then spiralled 85 steps to the pond: about 170 each way, and in ten games two schools were finished, *with the towers switched off*. Read `harvested_R` against `delivered_R` in `GLOB2_MEASURE` ([`scripts/game_counters.py`](../scripts/game_counters.py)): a prize that is picked up but never brought home is too far (a fed unit is hungry at 264 walked tiles), whatever else is wrong. Pair every game with the mechanism off on the same map and seed; the difference is the mechanism and the rest is the map.
- **A lock made of buildings must be one its owner can serve.** Three engine rules decide whether granted towers are a seal or scenery (all in [the game rules](../../../../docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md)): a worker may only construct, repair or resupply a building at or below its own build level (`Building::canUnitWorkHere`), and build level comes from a school; a tower holds 12 to 20 shots and the stone for as many again, about six dead workers; a level-0 warrior does 5 a hit to a level-1 tower and 1 a hit to a level-2 one. Hidden Oasis granted level-2 towers under a lock that denied everyone a school: in every game of two review rounds each tower fired exactly 32 shots (`shots_2`), burned exactly its starting reserve (`consumed_2_3` = 4) and stood empty, and the "seal" opened by itself. With level-1 towers within a walk of their owners, the same counters read 100 to 220 shots and 29 to 74 stone, and AIs destroyed towers. Prove the mechanism with its own counters in one game before polishing anything else.
- **When two walks must both be fair, make them the same walk.** Each colony's tower stood on the gorge, so a map could be fair on the walk to the prize (the mouth) or on the walk to one's own tower, never both: placing colonies by the mouth left owners 18 to 133 steps from their towers, and placing them by their towers put mouth walks 2.6 times apart. Moving every tower's door to the same place as the prize's (box canyons that run forwards to the mouth's cliff) made one search fair for both, within 1.2 times on every map. A deeper tower then has a longer canyon, for its attackers as much as for its owner, which is a trade rather than an unfairness; record each colony's draw (`posts.depth-order`) so a tournament can be read by it.
- **A lock that works stops the AIs entirely; decide that with the maintainer.** Once the seal held, no AI finished a school in any game, so an AI game ran at level 0 throughout. That is the concept working against opponents who cannot play it. Put the consequence to the maintainer in numbers, and offer the least intrusive grant that keeps the prize (a finished level-0 school as a default-on toggle, the pond still gating every upgrade of it) rather than deciding for them. Grants have their own AI traps: check whether an AI spends the grant (upgrades its only school into a site that needs the prize).
- **Can a person hold it?** The AIs never garrison a chokepoint, so no AI game tests the holdability the geometry was built for. Say so in the PR, and give the maintainer a playtest checklist: does a two-tower landing hold, is swimming a fair counter, is the haul tolerable, and does the holder's snowball feel earned.

## Route protection: which barrier for which promise

| Promise | Mechanism | Generators | Validation |
| --- | --- | --- | --- |
| A way that can never close | Sand lane or ford (unbuildable, ungrowable) | Emoji's bypass, Forts' roads, Braided Delta's approaches, The Glacis' fords, causeways and tracks, Allotments' lanes and plot paths, Hills' stairs, Drumlin field's eskers, Savannah's exits | Walk from every colony with crops in place; check the finished sand core, not the sketch |
| A way that costs work to open, then stays open | Dry wood plug with zero fertility | Breachable Highlands' saddles, Hedgerow Country's hedges, Locust' fields | Assert zero fertility on every plug tile in the finished world; check the square growth envelope, not a circular clearance |
| A way that opens with an ability | Pure-water strait of a chosen width | Plantations (four corners, so only level-3 towers reach across), Braided river (4.4 to 7 corners), Canals | A four-connected water core so nothing steps diagonally (`channelCoreFault`); bank-to-bank arithmetic from `Channels.h` |
| No way at all | Sealed stone line | Braided river's bluffs at the seam, Forts' ramparts, The Glacis' bastioned walls, Caravanserai's courtyard walls, Breachable Highlands' ridges | `designedStone` gaps; `pieceLeak`, `checkGatePartition`, `colonyLeak` or `coloniesApart` with the doors shut |

Select whole boundaries, never independent tiles: random holes in a thin wall let eight-neighbour movement erase the clearing decision. Remove a road's mixed-terrain shoulders from a wall mask, or the validator will demand deposits on tiles that cannot hold them. A sealed line at the torus seam is sometimes part of the concept: without Braided river's bluffs the walk round the back of the torus was shorter than the braid.

## Budget before stamping, refuse before shrinking below a floor

Every robust generator negotiates its layout in the same order: compute the room each feature needs (a base's reach plus tower rows, a farm radius plus its ring plus the growth probe, a channel's width plus beaches), drop optional features first (Caravanserai's caravanserais, Plantations' neutral islands and crop band, Drumlin field's farm room), shrink homes only to a documented floor (The Glacis narrows its glacis to 4 and then its fort to 20, a drumlin's half width of 8, a plantation plot of 8, a summit fixed at 16), and refuse with a message that names the control to change. A repair that erases the concept (filling a channel, opening a saddle, planting the town) is a failed candidate, not a fix. Braided Delta translates a clipped town clearing by at most six tiles rather than shrinking it; Continents relaxes site room from four to two and says so in telemetry; Drumlin field takes the grain heading that keeps crowded homes farthest apart rather than refusing the request. Karst towers shrinks its homes two tiles at a time, then narrows its rivers, while their bowls leave a river no way through; the retry is deterministic because every draw is from a named stream and `validateWorld` replays the same loop, and it cut refusals in 2,000 random rolls from 318 to 123.


**Share a budget; don't leave one feature the remainder.** Bajada first sized its fans as whatever the playa left, clamped to a floor, and its fans stayed at the floor (34 tiles) at every range spacing from 144 to 192 while the lakes took all the extra room; a control study showed the range-spacing control changing nothing a player would see. Give each feature a share of the room (fans 60% of the slope between their floor and ceiling, the widest playa the rest) so every wider setting grows both. Size fixed features for the widest setting of the control that varies (the fans for the widest playa) so that control moves only its own feature.
### Construct a fitting, don't search for one

Hidden Oasis' tower ledges were first *searched for*: a disc of grass tried at growing distances from the gorge until its wall stood and a tower on it covered the pinch. A quarter of requests failed, and every loosening moved the failure somewhere else (the far tower out of range of the near one, the ledge of one pinch inside the next). Carving the ledge *from the constraint* ended it: the ledge is the band of ground from exactly the wall's thickness back from the gorge's floor (`stepsFrom` the floor) to a few tiles more, so the wall is right by construction, the tower hugs it, and the reach is arithmetic (wall + pinch <= range). What is left to choose is ranked, not searched (the tower site that only just covers, which leaves room for its neighbour). Rules that fell out of the failures:

- put a fitting where the curve is straight (the pinches sit where the slot crosses its axis, the bends between them), and frame it on the design's axis, not the curve's local tangent, which alternates and leaned neighbouring ledges into each other;
- a tower's reach is a square (Chebyshev), so pads and keep-outs round a footprint are squares too: a round pad left its corners in range;
- after placing a building on a fitting, check the fitting still works (a tower at its canyon's head shut its owner out of the pad behind it);
- when a design can still fail for a seed, draw the whole design again from the streams as they stand (deterministic, since `validateWorld` replays it) before refusing, and mark which failures a redraw can mend: that took seed-dependent refusals from 13% of default requests to none in 2,000 random rolls, about one map in twelve being drawn twice.

## The picture is the first review

**Maps can't be ugly.** A change made to improve mobility or playability must not destroy the aesthetic vision the map was built on. Access, fairness and economy are the reasons a repair exists; none of them is a licence to draw over the thing the map is. When a playability fix and the picture disagree, find the version of the fix that keeps the picture — it nearly always exists, usually costs a few tiles, and is the only version a maintainer will ship. Examples from one review of the fractal maps (2026-09-16):

- **A causeway paints only what it crosses.** Sierpiński Gardens' causeways were stamped as full diameters: sand out past the lake onto the land and straight across the orchard island they were built to reach. The route's protection mask still covers the whole stroke; the paint stops at the water. Access unchanged, the diagram gone.
- **A formal garden stays square.** The same causeways were struck at sixty degrees across a square lake. They now run horizontal and vertical, a third pair as parallels; the angled search survives only as a fallback when a home blocks a whole axis on a 128 map, and telemetry records every use.
- **Grass never touches water.** Farm beds and bank plots laid after the shoreline pass shipped as hard grass-against-water edges. The beach pass is a postcondition of the design, not a step at a fixed point in it.
- **Scenery must not become the map.** A scatter of copses for colour, with water added across the land for the crops, grew into forest over the whole map in a single long game. Decoration that the engine can spread is a future the preview does not show — see the overgrowth section of [gameplay and playability](gameplay-and-playability.md).
- **Connections are drawn in the map's own language.** Paths joining a formal garden's features are straight legs meeting square — an L or a Z, never a staircase or a diagonal — laid only over open ground, so a path can lead to a feature but never cross one.
- **Tidy means every join is flush.** A formal map's aesthetic is clean, neat and tidy: no bit of road jutting past a join, no bit missing, every join at a right angle and every edge straight. A two-wide path that met a home's rim showed one tile of sand, because it stopped at the rim's recorded box rather than the rim; paths now end with both tiles against the sand they join, meet a bridge along its axis and centred on it, keep a grass tile clear of anything they pass, and are not drawn at all where that is impossible. The home rim lost its decorative fray and gained a second corner of sand beyond its water, so it is the same width as the paths on every side.

- **Model the real thing, not the nearest primitive.** Rice terraces were first built from the contour-farm primitive the toolkit already had: concentric crop and water bands round point summits. The first look said centre-pivot farm; the fix lobed the rings, and the map still did not read as rice terraces, because the real thing is not a ring round a point at all. Real terraces are many narrow strips following the contour of a long slope, stacked down the hillside, winding together, joined into one landscape across the valley. Where the concept went wrong was the first step: the design brief named the mechanism (contour bands, stairs, summits) before it named the look, and each later fix decorated the mechanism instead of questioning it. Before choosing a construction, write down the three or four visual signatures a person would use to recognise the subject in a photograph, and check the planned geometry produces each one; when a first look says "this reads as X", ask whether the model is wrong before tuning the drawing. The ring map was good, so it kept its geometry under the name it looks like (Hills), and Rice terraces was rebuilt from stripes that wrap the torus with a shared sway along them (2026-09-16).

A maintainer meets a new map as a preview before any number, and what the preview says about the concept decides whether the numbers get read. Four kinds of remark come up on first looks, and each has a cheap, structural answer:

| What the eye catches | Why it jars | The answer that keeps the budget |
| --- | --- | --- |
| Invented features on a map that claims real geography (a ring of round islets round a continent) | The concept's promise is that the land looks like itself; anything the atlas does not show reads as a bug | Make the invention opt-in (`islets` off by default) rather than deleting the primitive |
| Compass-perfect shapes where the concept names something natural (terraces as concentric circles look like a centre-pivot farm) | Nature's contours are lobed and nested, not round; the eye reads the circle as machinery | Keep the geometry exact and wobble its radius: a few low harmonics of the heading, the same shift for every band so widths hold, ramped in past the summit's cap so the town stays round and the mapping along a ray stays monotone (`ContourWobble`, `contourNominal`). Spend only as much amplitude as leaves the valley floor open, so band counts do not change The wobble made rings read as hills, not as terraces: the maintainer later renamed that map Hills and asked for a new Rice terraces modelled on the real thing (see below) |
| Open ground where the concept says work (a finite-food plain with passages already through the wheat) | If the fields are the obstacle, a map with more gap than field has given the obstacle away | Raise the cover default until the open pockets are the exception; leave the cleared trails as the only free routes and keep the control's range |
| Identical modules everywhere (an archipelago whose islands are all one size and one rounded square) | Repetition reads as manufacture even when every module is right | Stamp every module nominal, deal the colonies their modules on that, and only then reshape: a colony's own modules keep their size and vary in outline alone (any that loses its guarantee reverts), the neutral ones draw the whole range of size, stretch, squareness and wobble; keep every module nominal on a crowded map. Reshaping before the deal let the deal land on unequal modules, and a rotation tournament measured a 30-point position bias for it; after the deal the bias went back to zero with the same look |
| The same home stamp on every map (the fractal maps' one parterre, Forts' yard always left of its plots, Lava shield's one plain square town) | However good the stamp, the second game looks like the first | Draw the home in one of several designs per map, give every colony the same one, and verify each design pinned against the previous release (see [shaped generators](shaped-generators.md#variety-without-dissolving-the-concept)). A natural concept may fray an edge; a formal one keeps its lines straight and varies the plan instead |
| A desert of green gravel (Bajada's second revision, which made the desert grass for building room) | Grass is one green however dry it is meant to be, so the fans vanish into a park | Zone the ground instead of mixing it: gravel in a belt along the ranges beside the fans, dune sand gathering towards the basin by distance from the range, and lone scrub and outcrops on the dry gravel |
| Every feature outlined (a sand fringe of constant width round every green shape) | A constant-width sand line on green reads as a sticker's cut edge | Fray the edge: one corner everywhere, a second and third where a noise field is high |
| Features radiating from the prize at even angles (Central Quarry's bars and streams) | Straight spokes from a centre read as a diagram, a wheel or a spider | Fan the approaches from one landing at uneven angles. Draw streams as a Catmull-Rom curve through waypoints swayed sideways (straight legs between waypoints read as canals), with the source skewed off the radial, a width that swells, and no two streams within a dozen tiles except where they meet the lake |
| Evenly spaced crossings (a sand ford every 24 tiles) | Beads on a string | Jitter each spacing (0.75–1.35×) and each width (±20%) |
| A round plot with its own sand ring inside an island's beach | A ring inside a ring: a bullseye, an eye on the island | Put the plot on the shore so the beach seals half of it, stretch it along the shore, sway the seal's radius on noise, and deal its crops by noise rather than as two blobs |
| A small resource knot grown by random frontier (a quarry) | A thin twig of single tiles | Grow it from the frontier tiles touching the most rock so far, picking at random among them: compact and still rough |
| Fields ending in ruler-straight lines where no feature is straight | The growth probe is a square, and kits plant the most fertile ground first, so fields end along its square contours | Fray every field edge up to two tiles on noise, and bound watered fields by a noise-varied round distance from water. Keep both passes 20 tiles off every home: the first version starved a seed |
| A band of dense fields across the top of the map | A reserve dealt to the first N tiles of a list in row order fills the top rows | Order any "first N" list by the preference it claims (patchiest first), never by index. Check wheat share by quarter of rows on a 512 preview. The shared Biomes dry reserve had this bug until Central Quarry's review (Continents moved 0–0.6% of its tiles when it was fixed) |
| Scenery the concept names but the map lacks (a savannah with no trees on its plain, too little water to look inhabited; a river's dry terrace that is one sheet of grass) | The concept is a picture in the maintainer's head before it is a contract | Add the scenery where it cannot break the contract: lone trees only on ground whose crop growth chance is zero, so the engine never spreads them; pools where no crop is planted; one more watering hole per area, a tile more pond radius; sand patches from a periodic noise, kept off the bank strip, the structural stone and every town's room |

None of these needs a new control. Each is a default, a toggle or a few tiles, plus a revision bump and regenerated fingerprints on both platforms, and each deserves the same paired tournament as any other tuning change before the numbers are trusted. Record the remark and the answer in the generator's header comments: the next designer will meet the same eye.

## Show the map, then measure the knobs, then roll everything

The maintainer decides what a map is from pictures, so the cheap loop comes first: generate a handful of seeds and shapes, publish the previews on a page with the settings beside each, and wait for a reaction before any game or tournament. Each round of Honeycomb isle's design (hexagons, water, fields, rings, river width) came from a look, not a number.

When the look is settled, **measure every control on its own.** Generate each control's values with everything else at defaults (six seeds at 256×256 with four colonies is enough), plus the default at every shape and colony count, plus a few extreme combinations, and tabulate the metric each control should move from the JSON report and the generator's telemetry. One 528-map study (19 s on eight cores) found:

- **Dead ranges.** Wheat amount above 100% changed nothing because the fields were already full; wood saturated at 200%. Cap a percentage at the value where it stops mattering (`GeneratorControl::percentage(id, label, maximum)`).
- **A variant that is a scale mismatch.** Square blocks used the block size as their pitch while hexagons used 150% of it, so squares had a fifth of the building sites. Give alternatives the same area, not the same number.
- **A choice that barely differs.** "Many" wheat fields added 9% over "Normal" because the edge was already all fields; a choice should move its metric visibly.
- **Ranges the map clamps anyway.** Block sizes above 21 shrank back on common maps; a minimum of 5 blocks per colony left almost no ruins.
- **Visual-only controls.** Warp moved no metric at all; a pair of previews showed it working. Look before removing a control the numbers call useless.
- **A control that only resizes barely moves its metric.** Karst towers' Sinkholes first scaled each pond's size; with three sinkholes a map, water went from 12.1% to 12.8% over the whole range. Letting it set the count too (by narrowing the trough window) gave 12.1% to 15.9% and 0 to 33 sinkholes.
- **A new control must reproduce the old map at its default.** Rewriting a hard-coded threshold as a formula of the control is easy to get wrong: Tower density's `32000 - (density - 50) * 240` looked right and changed 17,000 tiles of every default map, because the old threshold at 50 was 26,000. Check each new control by comparing terrain dumps (`--report terrain`) of the old and new binaries at defaults on several shapes, not by looking at previews.
- **A spacing control rounded to a count has dead steps.** Bajada's fan spacing (32–64 in steps of 4) became `round(width / spacing)` slots, so 40 and 44 gave the same map on 256 tiles. Expose the count itself ("Fans per range", per 256 tiles of map) when a count is what the design uses.
- **A clamp that always binds makes the top of a control dead.** Every default Bajada map narrowed its playa to fit (telemetry said so on every map), so playa 50–100 gave identical terrain. When a fallback fires at the defaults, the budget is wrong, not the control.
- **A small whole number scaled by a percentage has dead steps.** A kit's 3 outcrops per 1,000 tiles scaled by the stone amount lands on 2 at both 50% and 75%, and 1 grove per 1,000 moved only at 50, 150 and 250 (Hidden Oasis, 2026-09-17; the shared `BiomeKit` still scales that way for every other generator). Scale at a finer grain (`outcropsPer100000`, `grovesPer100000`), or scale the final count with `scaledCount`. The same study found a count written as "the colony count plus a rounded share" giving one map for 5 and 6 and five springs for 1: make a count linear in its control, `round(control x factor)` with a factor of at least 1 per step at the common size.
- **A structural quantity can hide a control in the random rolls.** Stone amount correlated at r = 0.04 with stone tiles, because the plateau's rock (r = 0.97 with the colony count) swamped it; against stone *off the plateau* it read 0.42 beside the mesa control's 0.46. Give each ambient layer its own telemetry measure and correlate against that, and scale every ambient layer of a resource by its amount (the mesas as well as the outcrops), leaving only the structure unscaled and saying so.
- **Let the report find them.** [`scripts/control_study.py`](../scripts/control_study.py) reads any generator's controls from `--list-map-generators`, runs the one-at-a-time study and the random rolls natively in parallel (about 1,000 maps a minute), and its report flags every value whose maps measure the same as the value below it, groups refusals by message, and lists each control's strongest correlations.
- **Test values must be on the control's step.** Values off a control's step (25 on a step of 10) are refused before generation; a study that uses them measures nothing for those rows. Check the refusal message before reading a row of empty metrics.

Then **roll everything at random**: every control over its full range, every shape and colony count, a few dozen maps on one page with their settings, for the maintainer to eyeball for degenerate rolls. That page is how "without the lagoon there is way too little water" was found; no metric had been asked about water on small maps.

Finally run a **reliability pass**: every control at its minimum and maximum alone and all together on a small, a default and a large shape, plus about two thousand random rolls, classifying every failure as an expected refusal or a bug. Six of 1,620 Honeycomb isle maps failed a starter-wheat distance by one or two steps, all with the largest blocks and little wheat; the fix was geometric (the cistern moves forward on big blocks, wheat is planted nearest the swarm), and the pass was repeated on fresh seeds and on the old seeds before calling it done. Rename streams, telemetry keys or controls before this pass: a stream name is part of every random draw.

On one machine, run these passes with the native CLI rather than the distributed framework: a small script that writes one `glob2 --generate-map ID ... --json FILE` line per request and runs them with `xargs -P 8` generated 2,330 Karst towers maps in under four minutes, where the tournament framework's per-experiment setup would have taken about 45 minutes for the same matrix. Read the per-map JSON reports (`terrain`, `resources`, `space`, `fertility`, `canonical_quality`, `movement`, and `generation.telemetry.records`) with a short analysis script: effect tables per control, Spearman correlations of every control with every metric over the random rolls, refusals grouped by message and shape. Keep the framework for several hosts or runs that must survive interruption ([distributed telemetry](distributed-telemetry.md)).

## Ask an independent reviewer, in rounds

The author of a generator reads its previews through the design they intended. A fresh agent that sees only the code, the handbook and the binary, with none of the author's reasoning, finds what the author cannot. Karst towers went through three such rounds (2026-09-16/17):

1. **Round one, a general review:** "leopard print, not karst", food-capped in 25,000-tick games, uneven river access between colonies, a false claim of identical homes in a code comment, a size envelope too narrow, rivers that doubled back into hairpins, fords that decided nothing because one river round a torus parts nobody. Look 5/10, playability 3/10.
2. **Round two, one question the maintainer asked:** "not enough water, not enough potential food yield". The reviewer wrote the growth-potential tool above, simulated each proposal on terrain dumps to estimate its effect before any code changed, and ranked the proposals.
3. **Round three, verification:** it confirmed the author's numbers with its own tools, played 50,000-tick games, and found the regressions the fixes had caused (halved building room, lake wheat overgrowing the routes, stamped-looking pools).

Bajada went through five rounds (2026-09-17). Look went 5, 4.5, 6, 6, 6 out of 10 and playability 6, 7, 7, 4, 6: fixes to the look traded against the economy twice, and round four blocked the merge on a starved opening the author's own games had not yet caught. Ask for the verdict in so many words (would you block a merge, and on what), and make later rounds short verification rounds that name the previous block and the target the fix must reach (wheat within 12 tiles, 24-step yield, units on the failing seed).

Central Quarry went through eight rounds (2026-09-17) with look 5, 6.5, 6, 6.5, 6.5, 7, 7.5, 8 and playability 5, 5, 5, 6, 6.5, 6.5, 7, 7.5. Twice a round's look or fairness fix cost food, and the next round caught it:
- field trims near homes starved seed 1 in round 3;
- halving the country lakes cut yield by a third on three seeds in round 6.

Once the reviewer stopped blocking (round 4), asking it for "what would raise look and playability to 8+, with the change and its expected effect" produced the structural changes that got there: a single landing, streams, zoning, a garden and a bigger centrepiece on 512. Each later round was a short verification against the targets it had set.

Hidden Oasis went through three rounds in one afternoon (2026-09-17), look 5, 6, 6.5 and playability 3.5, 4, 5.5, blocked every time, and each block was real. Round one found the unserved towers and the unfair tower walks; round two found *why* no tower was ever resupplied, an engine rule the author had not known (`Building::canUnitWorkHere`), by following one odd number in a game log (one AI resupplied its tower 4,096 ticks after its first school); round three found that a rival's tower outside the cliff could reach the shallowest ledge. The author's own forty games had shown the symptoms and none of the causes. Two things that helped: letting the reviewer read the author's game logs (it corrected the author's summary of them), and asking it to hunt for what the *latest fix* broke, by name (colonies now closer together; all doors now in one place). One thing to verify rather than trust: it stated that towers never shoot buildings, which the source contradicts (`considerScanTile` targets a building when no unit is in range).

What made the rounds work:

- **Tell it the machine's rules.** `--run-game` needs absolute paths for `--map-file` and `--output-dir` (a relative map path fails with "input file does not exist" and ten "games" finish in a second with no `GLOB2_MEASURE` lines); cap it at two games at a time when you are running your own; a subagent's harness may refuse to write a report file, so ask for the report as its reply and for scripts and images under its own directory.

- **Freeze the build.** Copy the binary and the generator source to a scratch directory and point the reviewer there, so the author can keep working without moving the reviewer's target.
- **Give it the standard, not the answer.** Point it at `AGENTS.md`, this skill and a comparable generator; list what changed since its last round and what the maintainer said, and ask for prioritised findings with evidence (seed, command, metric, file and line), a concrete fix for each and an estimated effect.
- **Keep the same reviewer across rounds.** Its tools and reference measurements carry over, so round three compared against round two on the same seeds.
- **Re-measure the economy after every look fix.** Yield, wheat near swarms and building sites are deterministic and cheap. A round that only checks the new picture misses the colonies it starved.
- **Verify its proposals, don't copy them.** Some proposals the author had already made; some were simulated on terrain rather than generated. Measure the change with the reviewer's own tool after implementing it.

## What to write down

For every tuning change keep the seed, the request, the symptom (which colony, which tick, which metric), the hypothesis, the change and the paired re-run. The generators' headers carry this record in the comments on their constants; the pull requests carry the numbers. Both are what the next map's designer reads.

## Drowned Forest: distinguish a route, a future route and a feeding route

Use the actual resource-aware walking mask to measure a timber shortcut's current
benefit. A conservative mask that treats all eventual forest grass as blocked is
useful for proving permanent roads, but can invent detours that players do not face.
Measure both mouth-to-mouth savings and a real home-to-usable-destination journey.
Prove harvesting with ordinary workers and a matched unflagged baseline; keep AI
strategy recognition separate from the physical mechanism working.

Place a found start relative to its actual farm orientation. A central-looking
swarm can have a poor opening food haul when the farm faces away from it. Conversely,
late starvation beside available wheat can mean a lost inn or a long trip to the
remaining inn. Inspect feeding buildings, real walks and losses before enlarging
farmland. Rotate several AIs through each start and distinguish farm potential from
observed food delivery.

For compact layouts, try bounded alternate orientations of a neutral destination
before abandoning otherwise good terrain. Preserve the same random state between
alternatives and keep finished-world room, arrival and shortcut requirements intact.
A larger retry cap is a latency tradeoff, not a geometry repair or universal success
proof; retain the failures that motivated it and measure the tail.

Profile the complete generator, including repeated validation and search attempts.
Bound a flood only after proving that every queried goal and reconstructed path lies
within the bound. Local building-room checks need a global fallback and checks for
blocked external roots; compare ordered footprints and failure results against the
original helper. Preserve map-byte comparisons when optimizing exact behavior.
