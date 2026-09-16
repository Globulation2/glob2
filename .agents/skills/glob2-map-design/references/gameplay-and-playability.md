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

Distance to water is a useful first approximation, not fertility itself: the offset distribution and the sand on the opposite side matter. The `[−15,15]` range applies independently to coordinates, not to a circular radius. Use [FertilityField.h](../../../../src/map/FertilityField.h) and existing farm-placement helpers instead of inventing another approximate growth formula. Algae far from suitable sand can be an initial finite stock even when surrounded by water. An isolated dry wheat/wood patch can be consumed without renewing; expansion from an adjacent fertile source may still enter its boundary.

Separate **renewable production**, **reserve deposits**, **circulation**, and **construction land**. A practical starting layout is a compact wheat/wood growing area beside water, connected by a sand access lane to a generous clear grass town. Give the crop patch harvestable edges. A large solid resource block has much less useful frontage than its tile count implies. Keep wheat and fast-spreading wood from competing across an unrestricted fertile strip. Size plots and connections using shared planting/farm primitives; preserve their containment when adding visual irregularity.

An empty grass lane is only initially open. `openRoad` removes deposits at generation time; it does not prevent regrowth. For a route that must remain open, use a suitable sand strip, contained resource plots, or another explicit structural guarantee. Sand is a tradeoff: it protects circulation but removes building sites and can reduce adjacent fertility. Avoid solving overgrowth by covering the whole settlement in sand, or solving starvation by filling all of its grass with crops.

Check resource access at the end of placement and repair. The [resource helpers](../../../../src/map/generator/shared/Resources.h) can guarantee nearby wheat/wood and open cramped starts; their common targets are wheat within 24 walking steps, wood within 32, and at least 16 reachable 4×4 placement origins within 24 steps. These are helper targets, **not engine survival thresholds or automatic guarantees for every generator**. In particular, [reopenCrampedStarts](../../../../src/map/generator/shared/Pipeline.cpp) runs its repairs only when resource amounts are non-default; default geometry must work on its own. Repairs can remove the nearest food, so rerun the resource guarantee after clearing and then validate the actual result.

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

## Give AIs ordinary, forgiving spaces

Do not assume that an AI will recognize a beautiful farm design, clear a blocked route promptly, or reserve the exact future footprint the map requires. Existing policies differ:

- [Numbi placement](../../../../src/ai/AINumbiPlacement.cpp) scores free margins around buildings, including rings two and three tiles out. A legal footprint hemmed in by sand or crops can still be unattractive to its placement policy.
- [Nicowar farming](../../../../src/ai/nicowar/Farming.cpp) reserves patterned resource sites near water with forbidden areas and expands them into adjacent grass. Its [building placement](../../../../src/ai/nicowar/Buildings.cpp) also imposes sand distances and upgrade spacing. Equal raw grass area does not imply equal usable AI sites.
- [Maxima farming and clearing](../../../../src/AIMaximaFarmingPolicy.cpp) manages fertility, protected crops, placement/circulation maintenance, and wood-clearing campaigns. Its behavior depends on strategic budgets, discovered space, worker availability, cooldowns, and existing reservations. These mechanisms are evidence that congestion needs active management, not a promise that every overgrown map will recover.

**Design heuristic:** provide redundant local building space and multiple short routes to essential resources. Keep crop growth physically away from the town and its exits where possible. Do not make survival depend on opening a dense crop wall, building one precisely positioned swimming pool, or discovering one remote quarry first. Test several AI implementations; an advanced farmer succeeding does not establish that simpler opponents can play the map.

Run both unattended growth and staffed games. Unattended growth exposes structural invasion; real games expose farming reservations, insufficient clearing labor, building congestion, and resource depletion. At several times after generation record reachable building sites, available resource frontage, wheat/wood travel distance, open exits, starvation, completed buildings/upgrades, and whether armies actually reach opponents. Inspect the **first cause** of a collapse, rather than accepting a final winner as evidence of a healthy match. A single clearing operation that restores the AI's expansion is strong evidence to revisit containment or starting space.

## Building space means expansion and access

The swarm footprint is 4×4, and shared [planting](../../../../src/map/generator/shared/Planting.h) uses a two-tile clearance around it. This is only the first reservation. Count reachable pure-grass building origins after beaches, resources, swarms, and workers exist. Placement origins overlap: 16 valid 4×4 origins do **not** mean 16 disjoint buildings fit. Measure contiguous usable patches and test a plausible settlement layout with movement lanes between structures.

[Building::tryToBuildingSiteRoom](../../../../src/building/Update.cpp) checks the target upgrade footprint with its own offsets, ignoring only the upgrading building's current occupancy. Resources, shoreline, neighbors, and temporarily occupying ground units can block it. Consult the relevant [building tables](../../../../src/game/entities/) for the final widths, heights, and offsets rather than assuming every level has the initial footprint. Allow construction, harvesting, feeding, and army traffic to pass simultaneously. A row of inns can disconnect a narrow peninsula even if it was connected at tick zero.

## Design worker routes and warrior routes separately

Workers need repeat trips between resources, construction, swarms, and feeding. Warriors need approaches, deployment width, retreat, and access to food/training. A long winding path can make an otherwise abundant economy slow, while a direct military shortcut can expose that economy before it matures. Compare **walkable** distances and reachable harvesting neighbors, not straight-line distances to deposits. Resource tiles themselves are blocked; units collect from touching tiles. See [MapQuery.cpp](../../../../src/map/MapQuery.cpp) and the [resource pathfinder](../../../../src/map/pathfind/MapPathfindRessource.cpp).

Use eight-neighbor connectivity when checking actual ground reachability, and intentionally stricter cardinal routes where a robust engineered corridor is wanted. Inspect diagonal leaks, wraparound shortcuts, bridge entrances, and bottlenecks after all deposits are placed. [Roads.h](../../../../src/map/generator/shared/Roads.h) supplies route-opening tools; pass protected wall masks so repairs cannot punch through the arena. `openColonyRoutes` may add sand fords: use it only when such a repair respects the play contract, never to silently remove an island map's promised isolation.

Connectivity is a minimum, not a throughput test. Make heavily shared lanes wider or offer parallel routes, then watch traffic in populated games. Recheck routes with swimmers and after plausible construction at chokepoints. A map intended to open after swimming needs reachable training space and the upgrade resources that make swimming attainable.

## Tower geometry and permanent boundaries

**Engine rules.** [Defence towers](../../../../src/game/entities/BuildingTypesDefence.cpp) occupy 2×2 tiles and have ranges 5, 7, and 9 at player-facing levels 1–3 (table levels 0–2). [Target selection](../../../../src/building/TypeSteps.cpp) scans square rings using [turretScanTile](../../../../src/building/BuildingUtils.cpp); it does not perform a wall/water line-of-sight test. Towers can threaten units and buildings across a barrier, although range alone does not promise a shot: ammunition, target selection, and target motion also matter. Stone access supplies ammunition as well as upgrades.

Use `towerReach` from [Walls.h](../../../../src/map/generator/shared/Walls.h) and channel arithmetic from [Channels.h](../../../../src/map/generator/shared/Channels.h). For straight banks, `bankToBank(w) = w + 4`: a one-corner channel allows range-5 coverage of the nearest opposing grass; five corners allow range-9 coverage. Curved banks, bridges, and diagonal corners require actual footprint checks. Define whether border towers should trade fire, suppress a crossing, or remain out of range; choose widths from that intention. Test all tower levels and potential forward positions, including seams.

Stone resources make useful permanent ridges, arena walls, and sealed coasts. They are not destructible player-built stonewall buildings. Deposit stone only on pure grass; beach construction can erase potential wall cells. Use `sealCoasts`, `labelBorders`, `designedStone`, and their validators to stop eight-neighbor leaks. A swimmer may land on the outside beach yet be unable to cross the stone ring; roads accidentally connected to that beach can invalidate the seal. Preserve intended doorways and distinguish inside land, outside beach, protected wall, and ordinary quarry masks.

A permanent boundary also provides permanent mining frontage. Consider whether it gives everyone effortless tower ammunition or makes a supposedly scarce upgrade resource abundant. Players can build their own walls on grass: test whether a one-tile door becomes an absolute lock, and provide a wider front or an eventual alternative if permanent stalemate is not the intended game.

## Rewards, fairness, and evidence of fun

Fruit is more than decorative variety. [Inn happiness](../../../../src/building/Misc.cpp) counts stocked fruit kinds; [food selection](../../../../src/team/TeamRouting.cpp) considers enemy food sharing, conversion eligibility, happiness, reachability, and starvation-limited travel. [Unit armor](../../../../src/unit/UnitStats.cpp) also responds to fruit consumption. A contested three-fruit orchard is therefore a meaningful strategic reward, not automatically the best prize in every setting. Compare each fruit kind and access route across starts, and test the intended conversion opportunity in a real game.

The [start-quality scorer](../../../../src/map/generator/shared/StartQuality.h) measures wheat, wood, fertility, deposit depth, room, and rival isolation. Its aggregate is useful for selecting asymmetric starts, but does not establish equal access to fruit/stone/algae, fair tower positions, late swimming routes, or equal exposure to multiple opponents. Use it alongside design-specific measurements. Symmetry gives comparable initial geometry; placement order, rasterization, resources, and AI behavior still need inspection.

Keep a small playtest record per candidate: seed and every parameter; before/after screenshots; the intended first conflict and observed first conflict; the weakest economy; congestion or overgrowth; what broke a stalemate; and what decision players found enjoyable. Vary team count, aspect ratio, resource extremes, and meaningful design parameters. Include AI mirrors with starting seats swapped, mixed-AI matches, and human play at defaults. Replays and measurements establish reproducibility and failures; maintainer play assesses pacing, readability, tactical choice, and whether players would choose the map again. Report untested cases instead of calling a successful generation or an AI victory proof of fun.
