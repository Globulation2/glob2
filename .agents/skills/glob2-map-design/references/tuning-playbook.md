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

## Fair by construction, fair by search, or fair by measurement

Three fairness models are in use. Choose one deliberately and validate the thing it promises:

| Model | Generators | What it guarantees | What it does not |
| --- | --- | --- | --- |
| Construction: identical modules on a lattice or orbit (`latticeSites`, `Orbits`, `dealStarts`) | The Glacis, Allotments, Caravanserai, Forts, Hills, Savannah, Emoji's crossing ring | Same home, same kit, same walk to the first objective; stencils stamped by quarter turns (`turnStencilTile`) copy a whole home exactly (The Glacis' forts, Allotments' villages, Caravanserai's home oases), and Caravanserai proves equal caravanserai costs with `unevenCosts` | Neighbours: which rival is nearest and what lies between; lattice rows on rectangles give some homes two neighbours in a line |
| Search: candidate arrangements scored on the finished layout | Hedgerow Country (territory times contact ratio), Continents (fertile window, catchment floor, recentring with an undo when rivals come too close), Emoji (site refinement by the best fertile tiles plus room), Plantations and Braided Delta (farthest-point over islands, then dealt) | Bounded inequality in the measured quantity | Anything unmeasured; Hedgerow Country ships maps with a 7:1 expansion ratio and says so |
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

Read each AI's results separately; each exposes a different habit, and a map tuned for one can starve another:

- **Nicowar** overbuilt swarms on premade bases and starved; it otherwise works most plot shapes and is the most forgiving measure of an economy.
- **Castor** cannot run a base of construction sites.
- **Maxima** clears routes other AIs cannot, hiding blocked approaches. A map that only Maxima survives is not tuned.
- **Numbi** places no food inn if the wheat plot's edge moves toward town, and has two gates a map can shut without noticing (`src/ai/AINumbiPlacement.cpp`, `AINumbiEconomy.cpp`):
  - *It builds only when its swarm stands in open ground.* `findNewEmplacement` scores the ground round the main building before searching at all; the score starts at 352 and must stay above 299, and the swarm's own footprint already costs most of that margin, so a sand path, a plot rim, a wall or a crop within about three tiles of the swarm means the colony never places a building. Caravanserai's first rebuild set its swarm a fixed distance forward, against the field ring's path: in a 45,000-tick tournament 72 of 96 Numbi colonies built nothing and died. Put the swarm on the most open ground of its home (the tile farthest from any other kind), not at an offset.
  - *It breeds only on a compact wheat block by its swarm.* `swarmsForWorkers` turns production off unless `estimateFood` of the swarm is at least three tiles per colonist (five to switch it back on). `estimateFood` walks from the wheat tile nearest a corner of the swarm along a row and a column, tolerating only a few non-wheat tiles in all, and returns width times height of that one rectangle. Narrow strip plots (Allotments' first village, four tiles wide: villages stalled at 13), a thin ring of wheat along a shore (Caravanserai), or a cistern in the middle of the patch (The Glacis) all measure small however much wheat there is. Because the scan runs along the map's axes, a home stamped by quarter turns measures differently in different facings: The Glacis' Numbi colonies reached 60 facing two ways and 15 facing the others; moving its cisterns to the back of the gardens and planting the wheat as a solid band nearest the swarm lifted the weak facings to 20 to 30 but left one facing at 60, and only one facing per map (every home a translation of the others) removes the bias. Plant the opening wheat as a block with no water or sand inside it, beside the swarm, and read `AINumbi.estimateFood.result` per colony in the AI telemetry of a short game.

## Calibrate locally before a tournament

A tournament costs an hour of a cluster; a food-capped or unbuildable home shows in a few minutes on one machine. Before the first tournament, and after every economy change:

1. Generate the map the lobby would pick (`--generate-map --generator ID --map-seed S --candidates 5 --write-map true`) and the same seed of a reference generator whose AI games are known to grow (Forts, Hedgerow Country).
2. Play both with four copies of each AI players are offered, 20,000 to 30,000 ticks, `--telemetry team-timeline` (commands in [`scripts/game_economy.py`](../scripts/game_economy.py)). Run them in the background side by side.
3. Compare per colony: wheat and wood harvested, worker births, starvation deaths, units over time and buildings. Absolute numbers depend on the AI and the tick count; the reference game is the yardstick. The Glacis' first rebuild harvested 155 to 291 wheat against Forts' 427 to 882 in the same 30,000 ticks, which found a cramped courtyard and gardens too small an hour before a tournament would have.
4. When one AI stalls, read its own telemetry (`GLOB2_AI_FINAL`) for the gate that stopped it: `AINumbi.estimateFood.result`, `AINumbi.findNewEmplacement.true` against `.calls`, a building count that never passes one. Then look at the home in a terrain close-up (`scripts/render_terrain.py`) around that colony's swarm; the cause is nearly always within a dozen tiles of it.
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
  the habit (scattered ponds on a dry plain), and never tune the geometry to one AI's bug.

A generator can be under the fair-map floor and still have a doomed start on a third of its
maps, because the floor averages over maps. Read the per-map counts and the per-start economy
before the headline number.

## Route protection: which barrier for which promise

| Promise | Mechanism | Generators | Validation |
| --- | --- | --- | --- |
| A way that can never close | Sand lane or ford (unbuildable, ungrowable) | Emoji's bypass, Forts' roads, Braided Delta's approaches, The Glacis' fords, causeways and tracks, Allotments' lanes and plot paths, Hills' stairs, Drumlin field's eskers, Savannah's exits | Walk from every colony with crops in place; check the finished sand core, not the sketch |
| A way that costs work to open, then stays open | Dry wood plug with zero fertility | Breachable Highlands' saddles, Hedgerow Country's hedges, Locust' fields | Assert zero fertility on every plug tile in the finished world; check the square growth envelope, not a circular clearance |
| A way that opens with an ability | Pure-water strait of a chosen width | Plantations (four corners, so only level-3 towers reach across), Braided river (4.4 to 7 corners), Canals | A four-connected water core so nothing steps diagonally (`channelCoreFault`); bank-to-bank arithmetic from `Channels.h` |
| No way at all | Sealed stone line | Braided river's bluffs at the seam, Forts' ramparts, The Glacis' bastioned walls, Caravanserai's courtyard walls, Breachable Highlands' ridges | `designedStone` gaps; `pieceLeak`, `checkGatePartition`, `colonyLeak` or `coloniesApart` with the doors shut |

Select whole boundaries, never independent tiles: random holes in a thin wall let eight-neighbour movement erase the clearing decision. Remove a road's mixed-terrain shoulders from a wall mask, or the validator will demand deposits on tiles that cannot hold them. A sealed line at the torus seam is sometimes part of the concept: without Braided river's bluffs the walk round the back of the torus was shorter than the braid.

## Budget before stamping, refuse before shrinking below a floor

Every robust generator negotiates its layout in the same order: compute the room each feature needs (a base's reach plus tower rows, a farm radius plus its ring plus the growth probe, a channel's width plus beaches), drop optional features first (Caravanserai's caravanserais, Plantations' neutral islands and crop band, Drumlin field's farm room), shrink homes only to a documented floor (The Glacis narrows its glacis to 4 and then its fort to 20, a drumlin's half width of 8, a plantation plot of 8, a summit fixed at 16), and refuse with a message that names the control to change. A repair that erases the concept (filling a channel, opening a saddle, planting the town) is a failed candidate, not a fix. Braided Delta translates a clipped town clearing by at most six tiles rather than shrinking it; Continents relaxes site room from four to two and says so in telemetry; Drumlin field takes the grain heading that keeps crowded homes farthest apart rather than refusing the request.

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
| Scenery the concept names but the map lacks (a savannah with no trees on its plain, too little water to look inhabited; a river's dry terrace that is one sheet of grass) | The concept is a picture in the maintainer's head before it is a contract | Add the scenery where it cannot break the contract: lone trees only on ground whose crop growth chance is zero, so the engine never spreads them; pools where no crop is planted; one more watering hole per area, a tile more pond radius; sand patches from a periodic noise, kept off the bank strip, the structural stone and every town's room |

None of these needs a new control. Each is a default, a toggle or a few tiles, plus a revision bump and regenerated fingerprints on both platforms, and each deserves the same paired tournament as any other tuning change before the numbers are trusted. Record the remark and the answer in the generator's header comments: the next designer will meet the same eye.

## What to write down

For every tuning change keep the seed, the request, the symptom (which colony, which tick, which metric), the hypothesis, the change and the paired re-run. The generators' headers carry this record in the comments on their constants; the pull requests carry the numbers. Both are what the next map's designer reads.
