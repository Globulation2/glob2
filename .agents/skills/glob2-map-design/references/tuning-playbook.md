# Tuning a generator until its games work

Read this after the design brief and before the first AI game. It records what the September 2026 batch of sixteen generators (Emoji, Forts, Braided Delta, Breachable Highlands, Hedgerow Country, The Glacis, Allotments, Caravanserai, Braided river, Drumlin field, Continents, Savannah, Rice Terraces, Vultures, Plantations, plus the shared primitives they added) had to change between their first playable map and their merged revision, and which technique each chose for the same recurring problem. The generator sources cited are the authoritative record; numbers are what the tuning runs measured, not balance promises.

## The first failure is always the opening economy

Every generator in the batch that ran AI games found the same first defect, and it was never geometry: one or more colonies stopped growing before any fight. The symptom in telemetry is a colony whose unit count peaks under about a dozen by tick 15,000, with `hungry`/`critical` counts near its population and starvation deaths accumulating; the map still validated, every route was open, and the start scorer was content. Check this before measuring fairness, because a map whose starts starve has no fairness to measure.

The causes found, in order of frequency:

1. **Dry starter crops.** A kit planted on ground the growth probe cannot water is one harvest, and the AI eats it before its own farm exists. Forts' first prototype had 1% water; Glacis' compound, Allotments' home fields and Caravanserai's capital were dry by design; Vultures makes this the concept and must therefore forbid renewable repairs.
2. **Renewable crops too far from the swarm.** Glacis' nearest bank was 45 tiles beyond the gate; Allotments' nearest ditch two blocks away; Breachable Highlands' due-west start made the first food route circle the farm ring. Distance to food is measured from the workers' tiles by walking, after crops and beaches exist, not from the swarm centre.
3. **Too little frontage or too small a pond.** Hedgerow's 16-water-tile pond fed a mirror game for a while and then starved it; a 100-tile pond with wider capped plots fixed it. Merely adding starter wheat made Maxima expand faster and starve harder.
4. **The first inn's food arriving after the first hunger.** Rice Terraces' summit cap put the first stocked inn on the far side of a sand ring from the crops; a small completed, stocked inn halfway to the first stair supplies the first meal. Preplaced towers with an empty stone store recruited three of four workers for stone before that inn had food.
5. **A single starter patch on the wrong side.** Rice's one patch made the opening haul depend on which side of town the AI chose; seeding wheat beside every entrance fixed it. Wood needs one patch only, because its faster spread must not compete with wheat at every entrance.
6. **Ambient furnishing consuming the kit's ground.** Braided Delta at wheat 300%/wood 0% let ambient wheat, stone and fruit occupy the wood kit's bank, and the emergency top-up then planted wood inside the sand-rimmed town. Plant guaranteed kits before any ambient layer.

The remedies the batch converged on, compared:

| Remedy | Used by | What it costs | When it is the wrong tool |
| --- | --- | --- | --- |
| A small pond inside or beside the home with wheat and wood on its shore ("a well") | Glacis (2x2 vertices, 3 tiles inside the back wall), Allotments (one per home field pad), Caravanserai (capital pond), Continents (dig up to three 32-corner ponds until mean fertility reaches 2,500), Hedgerow (radius-5 pond) | Beach spoils 4 tiles behind the pond for building; the pond's sand slows regrowth, which is the point in a compound | Finite-food concepts (Vultures); maps whose water is the front (Braided river keeps towns dry on purpose and puts renewal on the bank strip and bars) |
| Sand-capped crop plots so growth fills the plot, not the town | Savannah (sealed grass islands in sand), Hedgerow (sand-ringed pond plots with a divider), Breachable Highlands (farm ring plus spokes), Rice (concentric caps), Plantations (one-vertex plot ring), Braided Delta (town rim) | Sand beside a crop reduces its regrowth; every cap is unbuildable | Maps that want overgrowth as pressure (Everglades, Drumlin tails) |
| More guaranteed tiles, kept unscaled at 0% | Emoji (48+48 shore farm plus 24/16 close supply), Forts (32 per plot plus scaled surplus), Caravanserai (24 to 36 wheat, 16 to 20 wood) | Larger farms cost building room; Hedgerow showed more starter wheat alone can make hunger worse | When the shortfall is regrowth rather than stock |
| Separate wheat plots from wood plots | Breachable Highlands (sand spokes; three quarters of the annulus to wheat), Hedgerow (divider), Drumlin (12% wood versus 30% wheat cover) | A plot per crop needs two seeds and two frontages | Not needed where sand or water already separates them |
| A stocked completed inn or granted buildings | Rice (one small inn), Plantations (pool and inn on every granted island), premade bases (whole base) | Changes the feel: the opening is skipped or shortened | Ordinary landscapes, where the opening is the game |

Wheat carries the engine's extra one-in-three growth gate and wood does not (Growth.h). Equal wheat and wood areas therefore produce a food-poor farm; several generators give wheat two to three times wood's area or cover, and the batch found no map where equal areas fed an AI.

## Ground a colony can reach beats ground it can see

The second recurring defect was measuring room or fertility as area rather than as walkable catchment:

- Continents' fertile-grass window scored a colony inside an Amazon loop at a thousand while its 24-step catchment held 394 tiles; the fix was a walkable-catchment floor (900 tiles within 24 steps) and a river clearance, on top of the fertility window. Counting grass alone had moved sites inland away from the water and traded one failure for the other.
- Hedgerow's equal straight-line home spacing left some starts closest to four times as much land as others; it now scores each candidate arrangement by sampled walking territory (one tile per 4x4 block, eight-neighbour flood over the designed obstacles) times a nearest-rival contact ratio, keeping the arrangement with the best product.
- Braided Delta's fords were intact while every colony pair was unreachable: bank crops had closed the ground between town and ford. A ford is not an accessible ford; every crossing end now gets a sand approach to the town rim on its own island.
- Rice's summit room was 16 tiles wide by design, but the summit spill found in bulk (seed 34101) came from the generic crop rescue planting on the dry town. Give repairs a mask of where emergency crops may go.

Use `StartQuality`, `windowCount`, catchment floods from the actual worker tiles, and the report's per-colony metrics together; none alone predicted the failures above.

## Fair by construction, fair by search, or fair by measurement

The batch used three fairness models. Choose one deliberately and validate the thing it promises:

| Model | Generators | What it guarantees | What it does not |
| --- | --- | --- | --- |
| Construction: identical modules on a lattice or orbit (`latticeSites`, `Orbits`, `dealStarts`) | Glacis, Allotments, Caravanserai, Forts, Rice, Savannah, Emoji's crossing ring | Same home, same kit, same walk to the first objective (Glacis and Caravanserai validate equal ford/outpost costs with `unevenCosts`) | Neighbours: which rival is nearest, which expansion ground lies between; lattice rows on rectangles give some homes two neighbours in a line |
| Search: candidate arrangements scored on the finished layout | Hedgerow (territory x contact ratio), Continents (fertile window, catchment floor, recentring with an undo when rivals come within 70% of the spread), Emoji (site refinement by best 48 fertile tiles plus room), Plantations and Braided Delta (farthest-point over islands, then dealt) | Bounded inequality in the measured quantity | Anything unmeasured; Hedgerow still ships maps with a 7:1 expansion ratio and says so |
| Measurement after the fact: the lobby keeps the best of five rolls by the start scorer | Everyone; explicitly the whole model for Continents, Drumlin, Braided river | Rejects the worst rolls | Position bias in games; the scorer's rank correlation with win share ran 0.6 on good maps and negative on others in the fairness study |

Whatever the model, deal sites to team indices with `dealStarts` before any per-team array is indexed, and sequence named-stream draws in separate statements: Forts and Rice both hit compiler-dependent evaluation order of two `bounded()` calls inside one expression, which changed the lattice per platform.

## The measurement loop

Tune with real games, not only the scorer. The loop the batch settled on, with the distributed tools:

1. **Build a Linux bundle** of the branch and register it (`tools.tournaments bundle`). Build on the host with the oldest glibc among the workers so one bundle runs everywhere.
2. **Play a rotation tournament** with `tools.tournaments.fairness`: four to six map seeds at 256x256 with four colonies, every cyclic rotation of team indices over the starts, one AI in every slot, 40,000 to 45,000 ticks, `outputs.telemetry` set to `team-timeline` so `GLOB2_MEASURE` rows are retained. Set `settings.prefetch` to 0 and list the biggest host first, or the small hosts queue three waves of games while the big one idles.
3. **Read per-start economy before win counts.** For every start slot, pooled over rotations: final and peak units, buildings, prestige, eliminations, starvation deaths (`deaths_<unit>_1` in the final `GLOB2_MEASURE` row) and `hungry`. A start that never passes twenty units is the defect, whoever wins.
4. **Then read position bias.** `reanalyze` gives wins by start pooled over rotations, the exact test per map and the RMS position bias per generator. With one game seed and four rotations a map has four games; only gross dominance (4 of 4) is visible, which is what tuning needs to catch.
5. **Look at the maps that failed.** Render the played map (`--preview-map`) and its final save. Most defects are visible: a start on a cape, a kit across water, a town inside a crop belt.
6. **Change one thing, re-run the same seeds, then fresh seeds.** Emoji's five candidates and 320 games, Forts' paired original-versus-final sets and Hedgerow's 80 matchups all used matched seeds; each also found that fresh seeds kept some favoured position. Report that honestly.

Read the per-AI results separately. Nicowar overbuilds swarms on premade bases and starves; Castor cannot run a base of construction sites; Numbi places no food inn if the wheat plot edge moves toward town (Savannah); Maxima clears routes other AIs cannot, hiding blocked approaches. A map that only Maxima survives is not tuned.

## Route protection: which barrier for which promise

| Promise | Mechanism | Generators | Validation |
| --- | --- | --- | --- |
| A way that can never close | Sand lane or ford (unbuildable, ungrowable) | Emoji bypass, Forts roads, Braided Delta approaches, Glacis fords and gate lanes, Allotments lanes, Rice stairs, Drumlin eskers, Savannah exits | Walk from every colony with crops in place; check the finished sand core, not the sketch |
| A way that costs work to open, then stays open | Dry wood plug (fertility zero at the plug) | Breachable Highlands saddles, Hedgerow hedges, Vultures fields | Assert zero fertility on every plug tile in the finished world; check the square growth envelope, not a circular clearance (Hedgerow seeds 30002, 1012498260) |
| A way that opens with an ability | Pure-water strait of a chosen width | Plantations (4 corners: only level-3 towers reach across), Braided river (4.4 to 7 corners), Canals | 4-connected water core so nothing steps diagonally (`channelCoreFault`); bank-to-bank arithmetic from `Channels.h` |
| No way at all | Sealed stone line | Braided river bluffs at the seam, Forts ramparts, Glacis walls, Highlands ridges | `designedStone` gaps, `pieceLeak`/`checkGatePartition`/`colonyLeak`/`coloniesApart` with the doors shut |

Whole boundaries, never independent tiles: Hedgerow selects wooded edges per boundary because random holes in a thin wall let eight-neighbour movement erase the clearing decision. Remove a road's mixed-terrain shoulders from a wall mask or the validator will demand deposits on tiles that cannot hold them.

## Budget before stamping, refuse before shrinking below a floor

Every robust generator in the batch negotiates its layout in the same order: compute the room each feature needs (base reach plus tower rows, farm radius plus ring plus growth probe, channel width plus beaches), drop optional features first (Glacis wadis, Plantations' neutral islands and crop band, Drumlin's farm room), shrink homes only to a documented floor (Drumlin half width 8, Plantations plot 8, Rice summit fixed at 16), and refuse with a message that names the control to change. A repair that erases the concept (filling a channel, opening a saddle, planting the town) is a failed candidate, not a fix. Braided Delta translates a clipped town clearing by at most six tiles rather than shrinking it; Continents relaxes site room from 4 to 2 and says so in telemetry.

## What to write down

For every tuning change keep the seed, the request, the symptom (which colony, which tick, which metric), the hypothesis, the change and the paired re-run. The generators' headers carry this record in their constants' comments; the PR descriptions carry the numbers. Both are what the next map's designer reads.
