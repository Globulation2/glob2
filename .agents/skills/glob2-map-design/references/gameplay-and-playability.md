# Design for the game that runs on the map

Read this when choosing geometry, economy, travel routes, barriers, or a target play style. The existing [game-rules guide](../../../../docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md) is a useful overview; the linked implementation is authoritative when a description and code differ. Numerical values below describe the current checkout, not permanent balance promises. Recommendations and acceptance targets are design heuristics unless identified as engine rules.

## Begin with the decisions players should make

Write a short play contract before painting terrain: where players establish themselves, what first brings them into contact, what they compete for, and what changes later. For example: “Each colony has a safe, fertile farm and room for an initial town; two roads lead to a contested orchard; swimming opens a third approach.” Translate each clause into a region, route, resource allocation, and validator. Name the intended pressure: short supply lines and early raids, prolonged border fighting, staged island expansion, or an economic race for a central prize.

A silhouette is insufficient. Ask whether the player can understand home, danger, and opportunity from the rendered map; whether the resource prize repays the travel and exposure; and whether there is a meaningful response to losing the first fight. A perfectly balanced corridor with one unbeatable tower position can still be tedious. A visually irregular landscape with comparable economies and multiple choices can be fair and enjoyable.

## Terrain is a rules surface

**Engine rules.** [MapTerrain.cpp](../../../../src/map/MapTerrain.cpp) derives each displayed tile from four undermap corner values. Pure grass requires four grass corners; pure water requires four water corners. A sand corner therefore affects four tiles. Buildings require pure grass, whereas ground units can walk sand and mixed shoreline tiles. Pure water blocks ground units without swimming capability. Check [Map.h](../../../../src/map/Map.h) and [MapQuery.cpp](../../../../src/map/MapQuery.cpp): `isSand` means *pure* sand, while `hasSand` covers transition graphics too. Those predicates are not interchangeable, particularly for resource growth.

Grass/water transitions have no proper mixed graphic. Use the shared beach pass to separate them with sand, then rebuild terrain before measuring buildable area or placing resources. Treat that pass as a postcondition of the whole design rather than a step at a fixed point in it: anything stamped afterwards — a late pond, a farm plot laid against a lake — needs another pass, and a plot sized for three rows of crops beside water has to lay down four, because the beach takes the innermost one. A generator that skips this ships a hard grass-against-water edge that reads as a bug on sight, however correct its measurements are. [Channels.h](../../../../src/map/generator/shared/Channels.h) records the resulting geometry: a straight channel `w` water corners wide has only `w - 1` pure-water tiles across and spoils `w + 3` grass tiles. Thus a one-corner water line is not a swimming barrier. Evaluate actual tile masks, including diagonal contacts and toroidal seams, rather than believing the corner sketch.

Choose a barrier by its consequences:

| Element | Ground movement | Building room | Long-term consequence |
| --- | --- | --- | --- |
| Grass | Walkable when empty | Buildable when empty | Wheat/wood may invade |
| Sand road or beach | Walkable when empty | Unbuildable | Prevents land crops occupying the road; consumes adjacent grass through corner geometry |
| Pure water | Requires swimming for ground units | Unbuildable | Delays ground contact; swimming may bypass intended fronts |
| Wheat/wood | Resource tiles block passage | Unbuildable | Can be harvested/cleared, but can grow back |
| Stone deposit | Permanent ground obstruction | Unbuildable | Permanent quarry and boundary, including against swimmers |
| Fruit deposit | Persistent ground obstruction | Unbuildable | Regrows in place; also a strategic food/happiness asset |

These are ground-unit rules; flying explorers do not respect the same barriers. A map has toroidal topology, so an apparent outside edge is another route unless the generated geometry seals it.

## Build an economy that does not eat its own town

**Engine rules.** Read [MapStep.cpp](../../../../src/map/MapStep.cpp), [MapResources.cpp](../../../../src/map/MapResources.cpp), and the [resource table](../../../../src/game/entities/Resources.cpp) together. Growth visits selected resource tiles, draws offsets `dx, dy` in `[-15, 15]`, and applies these gates:

- Wheat and wood require pure water at `(x + dx, y + dy)` and no pure sand at `(x - dx, y - dy)`. Wheat alone then passes an additional one-in-three random gate. Wood does **not** have that throttle.
- Algae requires pure water at `(x + dx, y + dy)` and pure sand at `(x + 2*dy, y + 2*dx)`. Preserve that exact swapped/doubled probe when reasoning about fertility; it is not literally a quarter-turn rotation.
- Fruit regrows in place without this water gate; it does not spread. Stone neither grows nor depletes through harvesting and cannot be cleared. Wheat, wood, and algae are clearable and spreadable.
- A visit may increase the amount or attempt an eight-neighbor expansion. The destination must permit growth, match the resource's terrain, and, for a new deposit, have no building or ground unit. An initially empty fertile tile is not a permanent building reservation.

Distance to water is a useful first approximation, not fertility itself: the offset distribution and the sand on the opposite side matter. What regrows a field is the density of pure water within a few tiles of it, weighted towards the nearest: a one-row channel or a pond whose water is narrower than its own beach feeds almost nothing, and water forty tiles from any home is scenery. Measure growth potential per colony with [`scripts/growth_potential.c`](../scripts/growth_potential.c) ([tuning playbook](tuning-playbook.md#measure-growth-potential-not-water-share)). The `[−15,15]` range applies independently to coordinates, not to a circular radius. Use [FertilityField.h](../../../../src/map/FertilityField.h) and existing farm-placement helpers instead of inventing another approximate growth formula. Algae far from suitable sand can be an initial finite stock even when surrounded by water. An isolated dry wheat/wood patch can be consumed without renewing; expansion from an adjacent fertile source may still enter its boundary.

Separate **renewable production**, **reserve deposits**, **circulation**, and **construction land**. A practical starting layout is a compact wheat/wood growing area beside water, connected by a sand access lane to a generous clear grass town. Give the crop patch harvestable edges. A large solid resource block has much less useful frontage than its tile count implies. Keep wheat and fast-spreading wood from competing across an unrestricted fertile strip. Size plots and connections using shared planting/farm primitives; preserve their containment when adding visual irregularity.

An empty grass lane is only initially open. `openRoad` removes deposits at generation time; it does not prevent regrowth. For a route that must remain open, use a suitable sand strip, contained resource plots, or another explicit structural guarantee. Sand is a tradeoff: it protects circulation but removes building sites and can reduce adjacent fertility. Avoid solving overgrowth by covering the whole settlement in sand, or solving starvation by filling all of its grass with crops.

Check resource access at the end of placement and repair. The [resource helpers](../../../../src/map/generator/shared/Resources.h) can guarantee nearby wheat/wood and open cramped starts; their common targets are wheat within 24 walking steps, wood within 32, and at least 16 reachable 4×4 placement origins within 24 steps. These are helper targets, **not engine survival thresholds or automatic guarantees for every generator**. In particular, [reopenCrampedStarts](../../../../src/map/generator/shared/Pipeline.cpp) runs its repairs only when resource amounts are non-default; default geometry must work on its own. Repairs can remove the nearest food, so rerun the resource guarantee after clearing and then validate the actual result.

**Prove the starter supply independently of ambient abundance.** When starts are assigned existing plots, preserve the plot owner and reachable entry tile from the walking search. Plant from that entry, not the Euclidean-nearest bank across a pond or wall. Validate the completed workers' access to their assigned wheat and wood, so nearby ambient crops cannot hide an inaccessible starter plot. Faulted City's low-resource, one-worker rolls exposed this despite abundant settings passing. Build those labels from final terrain after beaches, and budget fertile, usable tiles rather than the plot's nominal area. These are access checks, not a universal prescription to fill every starter plot.

An empty crop plot is not permanent building room. Exclude the possible future crop footprint when promising long-term construction space; low resource settings otherwise count unsown farmland as town room that disappears during play. Test disjoint footprints and their circulation after proposed buildings are placed, not only overlapping placement origins.

Resource controls must affect the whole economic design: reserve size, scatter, planted plots, and optional strategic deposits, subject to the documented starting guarantees. Separate deliberately permanent structural stone from optional economic stone using a protected mask and the existing primitive contract. Do not silently dilute boundary stone when turning resource abundance down, or hide most of the map's economy in fixed unscaled deposits.

## Wood overgrowth: the failure a preview cannot show

The commonest way a finished map turns out unplayable is timber. A deposit thickens in place until its amount passes a random 0–7, and from then on every visit **extends it to one of its eight neighbours**. Wood has no throttle (wheat's one-in-three gate does not apply to it), so a scatter of copses across open land is not decoration, it is a forest with a delay. Fifty thousand ticks later the land between the design's features is woods, walking routes are gone, and building room with them. None of this appears in a preview, in the map report, or in any measurement of the map as generated: the map ships correct and becomes wrong while it is played.

Measure it the only way it shows: count wood tiles on a final save against the same count at tick zero, or look at the game. Then contain it with terrain — the only containment a generated map may use:

- **Ground where the probe fails.** A deposit whose growth chance is zero — dry land, or sand behind it — never thickens or extends, however fertile its neighbours. Finite stock by geography. Scenery that must not spread goes here or nowhere.
- **Sand around the plot.** A deposit cannot occupy sand at all, so a sand-capped plot contains its own crops permanently. One row of sand corners is enough: the tile rows either side of it are no longer pure grass, and nothing can extend across them.
- **Leaving it out.** A copse on fertile ground that sand would look wrong around does not belong on the map.

**No-growth zones are forbidden.** The engine's saved `canResourcesGrow` flag belongs to hand-made scenarios such as the tutorial. A generated map may not set it on a single tile, and `validateGeneratedWorld` refuses one that does. The fractal maps briefly used it — to freeze copses, quarries, shoreline wheat and a home's service apron — and it was stripped out on 2026-09-16: a frozen tile is invisible to players, stops farmland regrowing where they expect it, and papers over an overgrowth problem the design should have solved. The overgrowth measurement that motivated it still stands: before any containment, one 50,000-tick game grew the fractal maps from about 4% wood to 27–32%.

Scattering water makes all of this worse at once, because the probe reaches fifteen tiles on each axis: a map that adds ponds across its open land has made almost every tile fertile, and any timber on it will eventually take the map. Budget the two together.

## Scenery that cannot grow

The engine spreads wheat, wood and algae only from a deposit whose own tile has water within its random probe (up to fifteen tiles on each axis) and no sand behind it; a deposit with a crop growth chance of zero never spreads, however fertile its neighbours. That rule is a design lever: trees and wheat on ground the growth field marks zero are scenery and finite stock, safe inside a containment design that otherwise forbids crops outside sealed plots. Prove it where the map is checked, not where it is drawn: a validator that admits a deposit outside its plots should ask the growth field of the finished terrain, not the design's intent, and keep the field's own probe reach as its only assumption.

The lever costs ground wherever water arrives. Honeycomb isle's first rubble was wood on zero-growth ground only, so a cleared ruin stayed cleared; every pond, crater and later the lagoon and river took a band of about fifteen tiles out of that ground, and 30–40% of the intended rubble was left as bare lots. When the map needs water and wood side by side, seal the wood in instead (streets or sand round each block) and accept that it regrows inside its plot.

## Give AIs ordinary, forgiving spaces

Do not assume that an AI will recognize a beautiful farm design, clear a blocked route promptly, or reserve the exact future footprint the map requires. Existing policies differ:

- [Numbi placement](../../../../src/ai/AINumbiPlacement.cpp) scores free margins around buildings, including rings two and three tiles out. A legal footprint hemmed in by sand or crops can still be unattractive to its placement policy.
- [Nicowar farming](../../../../src/ai/nicowar/Farming.cpp) reserves patterned resource sites near water with forbidden areas and expands them into adjacent grass. Its [building placement](../../../../src/ai/nicowar/Buildings.cpp) also imposes sand distances and upgrade spacing. Equal raw grass area does not imply equal usable AI sites.
- [Maxima farming and clearing](../../../../src/ai/maxima/AIMaximaFarmingPolicy.cpp) manages fertility, protected crops, placement/circulation maintenance, and wood-clearing campaigns. Its behavior depends on strategic budgets, discovered space, worker availability, cooldowns, and existing reservations. These mechanisms are evidence that congestion needs active management, not a promise that every overgrown map will recover.

**Design heuristic:** provide redundant local building space and multiple short routes to essential resources. Keep crop growth physically away from the town and its exits where possible. Do not make survival depend on opening a dense crop wall, building one precisely positioned swimming pool, or discovering one remote quarry first. Test several AI implementations; an advanced farmer succeeding does not establish that simpler opponents can play the map.

**Decide whether the essential pairing is granted or earned.** On Plantations' islands, AIs built swarms without a local pool and pools without a local swarm; the chosen remedy was a premade pair with upgrade room (2026-09-16). That is a precedent for an opening whose necessary pairing is granted, not a rule that every island map must supply a pool. If building swimming infrastructure is part of the intended progression, first budget enough local town, farm and service space to earn it. Who Ate the Map? kept ordinary starts by excluding cramped fragments from settlement while preserving them as scenery. Where a pairing truly must exist at the opening, grant it, preserve its access and upgrade room, and validate it. Where it is earned, prove local construction, training and eventual contact in a game.

**Make the opening legible to the AIs' own measurements.** The AIs do not see a home the way a player does; they measure a few things near their swarm and act only when the measurement passes. This matters for the newer AIs the maps are tuned for (Nicowar, Cortex, Cabino, Maxima). The Numbi rules below explain failures in older generators, but Numbi is no longer a tuning target ([which AIs to play](tuning-playbook.md#which-ais-to-play)). Numbi places its first building only when the tiles round its swarm are open, and breeds only while the wheat nearest its swarm forms one compact rectangle of about three tiles a colonist (the [playbook](tuning-playbook.md) gives the code and the cases). A home that is generous in total but puts a sand path beside the swarm, plants its wheat as a thin ring or narrow strips, or sets a pond inside the wheat patch can feed Nicowar and starve Numbi. Put the swarm on the most open ground of its home, plant the opening wheat as a solid block beside it with its water behind rather than inside, and read each AI's telemetry in a short game before a tournament.

**Grant a start, not a population.** A premade population is not a growing economy. In rotation tournaments of the premade-base maps (45,000 ticks, 2026-09-16), a finished base of fifty-odd units bred 14 to 15 times a colony with Nicowar and 4 to 9 with Numbi, and on Caravanserai two colonies in three starved out; only Allotments' base of construction sites, which the AI had to build, bred. Their rebuilds with an ordinary swarm and four workers peaked above a hundred units with Nicowar. Grant what the map's rules make impossible to discover (the Plantations pool above), not a head start.

Run both unattended growth and staffed games. Unattended growth exposes structural invasion; real games expose farming reservations, insufficient clearing labor, building congestion, and resource depletion. At several times after generation record reachable building sites, available resource frontage, wheat/wood travel distance, open exits, starvation, completed buildings/upgrades, and whether armies actually reach opponents. Inspect the **first cause** of a collapse, rather than accepting a final winner as evidence of a healthy match. A single clearing operation that restores the AI's expansion is strong evidence to revisit containment or starting space.

## Building space means expansion and access

The swarm footprint is 4×4, and shared [planting](../../../../src/map/generator/shared/Planting.h) uses a two-tile clearance around it. This is only the first reservation. Count reachable pure-grass building origins after beaches, resources, swarms, and workers exist. Placement origins overlap: 16 valid 4×4 origins do **not** mean 16 disjoint buildings fit. Measure contiguous usable patches and test a plausible settlement layout with movement lanes between structures.

[Building::tryToBuildingSiteRoom](../../../../src/building/Update.cpp) checks the target upgrade footprint with its own offsets, ignoring only the upgrading building's current occupancy. Resources, shoreline, neighbors, and temporarily occupying ground units can block it. Consult the relevant [building tables](../../../../src/game/entities/) for the final widths, heights, and offsets rather than assuming every level has the initial footprint. Allow construction, harvesting, feeding, and army traffic to pass simultaneously. A row of inns can disconnect a narrow peninsula even if it was connected at tick zero.

### Budget detached islands per assigned colony

Count pure-grass area, inland service space and **disjoint** footprints with circulation gaps on each inhabited component, multiplied by its assigned colony count. Overlapping origins and total walkable area can flatter a thin crescent. Recheck clear service footprints and reachable crops after kits and repairs: making building room can erase the opening food or timber that made the start viable. Check worker access to the buildings in play. Match the AI's terrain predicates: `hasSand` includes mixed beach tiles; a pure-sand mask can overstate eligible pool space.

[Who Ate the Map?](../../../../docs/map-generators/WHO_ATE_THE_MAP.md)'s failed crescent had 1,343 walkable tiles but only 835 pure-grass tiles and six spaced service footprints. Its replacement budget required 1,200 grass tiles, 400 inland service tiles and ten disjoint 4×4 footprints with two-tile gaps per detached colony, with two clear footprints retained after settlement. These are **map-specific tested budgets**, not universal engine minima or a guarantee of future AI choices. Prefer leaving a small fragment uninhabited to filling a signature bite, adding a bridge, or granting a building the concept intended players to earn. Novelty maps may be unequal; their starts still need functioning economies.

## Design worker routes and warrior routes separately

Workers need repeat trips between resources, construction, swarms, and feeding. Warriors need approaches, deployment width, retreat, and access to food/training. A long winding path can make an otherwise abundant economy slow, while a direct military shortcut can expose that economy before it matures. Compare **walkable** distances and reachable harvesting neighbors, not straight-line distances to deposits. Resource tiles themselves are blocked; units collect from touching tiles. See [MapQuery.cpp](../../../../src/map/MapQuery.cpp) and the [resource pathfinder](../../../../src/map/pathfind/MapPathfindRessource.cpp).

Use eight-neighbor connectivity when checking actual ground reachability, and intentionally stricter cardinal routes where a robust engineered corridor is wanted. Inspect diagonal leaks, wraparound shortcuts, bridge entrances, and bottlenecks after all deposits are placed. [Roads.h](../../../../src/map/generator/shared/Roads.h) supplies route-opening tools; pass protected wall masks so repairs cannot punch through the arena. `openColonyRoutes` may add sand fords: use it only when such a repair respects the play contract, never to silently remove an island map's promised isolation.

Connectivity is a minimum, not a throughput test. Make heavily shared lanes wider or offer parallel routes, then watch traffic in populated games. Recheck routes with swimmers and after plausible construction at chokepoints. A map intended to open after swimming needs reachable training space and the upgrade resources that make swimming attainable.

**Walking distance is a hunger budget, so mobility decides whether armies arrive.** A fed unit walks about 264 tiles before it is hungry and 352 before it starves ([game rules](../../../../docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md)), and that is walked tiles, detours and all, not the straight line between bases. A map whose rows, ditches, walls or crops force long detours can look compact in a preview and still starve every attack on the way: on Polder the dykes crossed only the ditches, so each row under crop was a wall a unit walked the length of, and units sent against an enemy base starved before they reached it; the game stalled into peace. Sand lanes through the crop rows as well as over the water (`FarmBridges`, 2026-09-16) fixed it without touching the concept. When a design lays long linear obstacles (farm rows, canals, hedges, ridges):

- Cross them often, and cross every kind: a bridge over the water is no use if the crop beside it is a wall. Prefer permanent sand crossings to grass lanes that crops refill.
- Measure the walk, not the distance: the route from each colony to its nearest rival, on final terrain with crops treated as blocking, should sit well inside the hunger budget, with room for a forward inn on the way (hamlets, plots).
- Check the late game too: crops spread, so a lane that is open at tick 0 can close by tick 30,000. Only sand (or water for swimmers) keeps a crossing open.
- Read tournament deaths by cause: many starvation deaths among warriors, few combat deaths and no eliminations is the signature (the peaceful stalemate in the [tuning playbook](tuning-playbook.md)).

## Tower geometry and permanent boundaries

**Engine rules.** [Defence towers](../../../../src/game/entities/BuildingTypesDefence.cpp) occupy 2×2 tiles and have ranges 5, 7, and 9 at player-facing levels 1–3 (table levels 0–2). [Target selection](../../../../src/building/TypeSteps.cpp) scans square rings using [turretScanTile](../../../../src/building/BuildingUtils.cpp); it does not perform a wall/water line-of-sight test. Towers can threaten units and buildings across a barrier, although range alone does not promise a shot: ammunition, target selection, and target motion also matter. Stone access supplies ammunition as well as upgrades.

Three more tower rules decide whether granted towers do anything (Hidden Oasis, 2026-09-17; each verified in the source and in games): **a worker serves only buildings at or below its build level** (`Building::canUnitWorkHere`: construction, repair *and resupply*), and build level comes from a school, so a colony without one can only ever serve level-1 towers (table level 0); **a tower is an ammunition counter**, 12, 16 or 20 shots plus a reserve of stone for as many again, about six workers, after which it is scenery unless its owner's workers can walk to it and find stone; and **armour decides who can break it**: a level-0 warrior hits for 13 less the tower's armour of 8, 12 or 15, so 5 a hit on a level-1 tower's 480 hp and 1 a hit on a level-2 tower's 1,440. A tower with no unit in range shoots buildings, so two towers in range of each other duel from the first tick. Workers never avoid an enemy tower's range: a site that asks for a resource behind one marches them under it.

Use `towerReach` from [Walls.h](../../../../src/map/generator/shared/Walls.h) and channel arithmetic from [Channels.h](../../../../src/map/generator/shared/Channels.h). For straight banks, `bankToBank(w) = w + 4`: a one-corner channel allows range-5 coverage of the nearest opposing grass; five corners allow range-9 coverage. Curved banks, bridges, and diagonal corners require actual footprint checks. Define whether border towers should trade fire, suppress a crossing, or remain out of range; choose widths from that intention. Test all tower levels and potential forward positions, including seams.

Stone resources make useful permanent ridges, arena walls, and sealed coasts. They are not destructible player-built stonewall buildings. Deposit stone only on pure grass; beach construction can erase potential wall cells. Use `sealCoasts`, `labelBorders`, `designedStone`, and their validators to stop eight-neighbor leaks. A swimmer may land on the outside beach yet be unable to cross the stone ring; roads accidentally connected to that beach can invalidate the seal. Preserve intended doorways and distinguish inside land, outside beach, protected wall, and ordinary quarry masks.

A permanent boundary also provides permanent mining frontage. Consider whether it gives everyone effortless tower ammunition or makes a supposedly scarce upgrade resource abundant. Players can build their own walls on grass: test whether a one-tile door becomes an absolute lock, and provide a wider front or an eventual alternative if permanent stalemate is not the intended game.

## A scarce resource as the prize

A map about one resource is designed from the engine's cost tables outward (`src/game/entities/BuildingTypes*.cpp`). For stone:

- **Every level-0 building is stone-free.** Inns, hospitals, schools, pools, barracks and swarms cost none. Towers can be built without it but fire it, and walls, racetracks, markets and every level-1 and level-2 upgrade cost it. Colonies without stone still grow and train warriors, so a monopoly shows up as unarmed towers and unupgraded buildings, not as starvation.
- **One quarry is permanent,** because stone is eternal. Its output is limited by how many workers can stand beside it, so the quarry's size is a throughput control, and its shape decides frontage: a compact knot of 9 tiles has more standing room than a line of 9.
- **The holder has to live there.** Nothing grows on a bare island, so a holder hauls food across the only approaches. A small garden sealed against the shore (wheat and wood that can't spread over the island) gives a holder a garrison's food and wood without turning the prize into a farm.
- **Holdable, not lockable.** All approaches landing at one place let a few towers cover them. Shore grass within tower range of that landing lets attackers answer, and swimming (a level-0 pool) is a counter the holder can't close. A player can still wall a narrow landing with the holder's unlimited stone. Decide whether that is the game, and check it in human play.
- **Distance is part of the price.** A 70–80-step walk each way is a tax on the holder and puts the prize outside some AIs' working range entirely. Cap it in steps, not as a share of the map.

## Food scarcity: contest access and production separately

For a map promising exposed food, record the closest **two rival walking distances**
to each district's harvesting edges. Two colonies reaching it within a generous
ceiling does not make it contested: a Hungry Marches prototype admitted fields
3 steps from one colony and 73 from another. Choose relative-access bounds for
that map's intended response time, and inspect tower coverage and the alternative
fields too. There is no universal distance that establishes contestedness. A duel
may need a different food arrangement from a many-colony game: a ring around two
opposite homes tends to give each its own half, whereas middle-ground fields can
face both colonies. Measure the finished, stocked terrain and the future crop
footprint, including toroidal shortcuts.

Balance the usable banks, not just the pond centres. A timber section at one end
of a shared field can give the opposite colony the only nearby grain, even when
the pond centre is equidistant. Orient or distribute the grain and timber so both
approaches reach useful crops. Check colony-to-colony connectivity with mature
fields occupied as well as each colony's access to food: disconnected groups can
each have enough fields while the intended raiding routes have disappeared.

Separate **opening stock, seeded renewable capacity, and delivered food**. Finite
home wheat needs zero growth probability under the actual kernel; a low initial
tile count alone does not make it finite. A control promising a richer centre
should change productive bank geometry or fertility, not just its initial wheat
density, which natural growth can erase. Keep rebuilding possible: making all
wood finite by copying the dry-wheat policy introduces a second scarcity. Use
separate grass components for renewable timber and wheat so faster tree growth
cannot take over the food banks.

A field split by sand or water may contain several independent growing banks.
Measure potential only on components a planted crop can reach, or deliberately
seed every productive component. A per-district minimum of two randomly selected
seeds can leave some banks empty forever. Check this especially at low resource
settings; summing fertility over all designed farmland overstates the actual
supply when some of it can never be colonised by wheat.

Check whether an AI models finite stock at all before enlarging the opening kit.
On Hungry Marches' dry-start duels, Maxima completed an inn but held its population
at four: its fertility-weighted food estimate funded zero birth workers, including
scouts. More dry wheat could not change that estimate. Distinguish this planning
assumption from an inaccessible field; use another AI or a mirrored game to study
the map, and track an AI fix separately rather than breaking the scarcity promise.

## Rewards, fairness, and evidence of fun

Fruit is more than decorative variety. [Inn happiness](../../../../src/building/Misc.cpp) counts stocked fruit kinds; [food selection](../../../../src/team/TeamRouting.cpp) considers enemy food sharing, conversion eligibility, happiness, reachability, and starvation-limited travel. [Unit armor](../../../../src/unit/UnitStats.cpp) also responds to fruit consumption. A contested three-fruit orchard is therefore a meaningful strategic reward, not automatically the best prize in every setting. Compare each fruit kind and access route across starts, and test the intended conversion opportunity in a real game.

The [start-quality scorer](../../../../src/map/generator/shared/StartQuality.h) measures wheat, wood, fertility, deposit depth, room, and rival isolation, and the [fitted fairness model](../../../../docs/map-generators/FAIRNESS_MODEL.md) turns those into each colony's chance of winning. Its aggregate is useful for selecting asymmetric starts, but does not establish equal access to fruit/stone/algae, fair tower positions, late swimming routes, or equal exposure to multiple opponents. Use it alongside design-specific measurements. Symmetry gives comparable initial geometry; placement order, rasterization, resources, and AI behavior still need inspection.

Keep a small playtest record per candidate: seed and every parameter; before/after screenshots; the intended first conflict and observed first conflict; the weakest economy; congestion or overgrowth; what broke a stalemate; and what decision players found enjoyable. Vary team count, aspect ratio, resource extremes, and meaningful design parameters. Include AI mirrors with starting seats swapped, mixed-AI matches, and human play at defaults. Replays and measurements establish reproducibility and failures; maintainer play assesses pacing, readability, tactical choice, and whether players would choose the map again. Report untested cases instead of calling a successful generation or an AI victory proof of fun.
