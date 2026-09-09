# Maxima strategic farming

Maxima owns a private farming planner. It does not alter `Map`, the shared AI
runtime, or save-game data. Its fertility cache and derived masks are rebuilt
after construction/load and are intentionally not serialized.

## Implementation model

The runtime policy follows a plan/reconcile pipeline rather than incrementally
editing shared masks:

1. `build_farming_protection_plan` classifies wheat and wood roles and resolves
   their priority against reservations, strategic gates, and firebreaks.
2. `restore_passive_coastal_access` changes only that desired forbidden-area
   plan. Passive checkerboard openings never become strategic gates, clearing
   zones, or maintained pathways.
3. `connect_settlements_to_gates` audits every completed physical building,
   reserves missing inland branches to the gate network, and reports failures.
   `refresh_gate_defense` evaluates firing positions against those same routes.
4. `resolve_wheat_invasion_clearing` gives wood beside the final protected wheat
   mask the same hard clearing priority used by maintenance. It uses this pass's
   wheat mask, so new and revoked obligations do not depend on stale maintenance.
5. `apply_farming_protection` emits only the difference between the desired plan
   and the forbidden area previously owned by this subsystem.
6. Building circulation and wood firebreaks use the same model:
   `build_maintenance_clearing_plan` produces separate hard-circulation and
   renewable-firebreak masks, then `apply_maintenance_clearing_plan` reconciles
   them once.

The strategic gate selector represents each candidate as one
`StrategicGateOption` containing its cells, resource burden, relocation cost,
and home distance. Named connectivity and overlap predicates replace the old
parallel maps and nested gate-selection lambdas. Active clearing flags likewise
have explicit gate, wood, and legacy-save campaign states instead of relying on
an unexplained numeric sentinel throughout the executor.

## Independently tunable behavior

All switches below default to `true` and are present in the optimizer's
`farming` parameter stage:

- `farming.farm_protection_enabled`
- `farming.barrier_topology_enabled`
- `farming.coastal_porosity_enabled`
- `farming.gate_clearing_enabled`
- `farming.maintenance_clearing_enabled`
- `farming.resource_preserving_circulation_enabled`
- `farming.wheat_invasion_clearing_enabled`
- `farming.wood_firebreak_enabled`
- `farming.proactive_clearing_enabled`

`farming.enabled` remains the master switch. Coastal porosity is a sub-policy
of farm protection. Resource-preserving circulation, wheat-invasion cleanup,
and the wood firebreak are sub-policies of maintenance clearing. Turning off
resource preservation restores the conventional behavior of clearing the full
circulation ring; it does not remove the circulation contract itself.

## Exact fertility

The resource-growth RNG chooses independent triangular offsets
`dx,dy = U(0,15)-U(0,15)`. For a resource at `(x,y)`, its exact growth numerator
is therefore:

```text
F(x,y) = sum(dx=-15..15, dy=-15..15)
         (16-|dx|)(16-|dy|)
         water(x+dx,y+dy) * !sand(x-dx,y-dy)
```

All coordinates wrap. `F` is stored in a 32-bit cell and ranges from 0 through
65536. The denominator is 65536. The mirrored sand lookup is important: checking
sand beside the sampled water cell is not equivalent to the game rule.

The cache chooses the cheaper exact calculation:

- Four length-16 toroidal rolling boxes (two horizontal, two vertical) produce
  the separable water triangle, followed by mirrored corrections for sand.
- Direct weighted water splats test the corresponding mirrored sand position.

The estimated useful expansion capacity of a resource is:

```text
F * current_amount/8 * empty_compatible_neighbors/8
```

Wheat capacity is divided by three to match its growth-rate gate. A direction is
not compatible when it contains a resource (including the same type), building,
ground or air unit, non-grass terrain, or a disabled-growth cell.

## Policy order

Every discovered resource tile, plus empty fertile grass immediately adjacent
to wheat or wood, is evaluated in this order:

1. Exact planner footprints, access rings and arteries remain open.
2. Validated gate cells and their core-side emergency escape lanes remain open.
3. The water-facing edge of a wheat or wood farm, and compatible empty grass
   continuing that edge, are protected as a continuous outer barrier. Wheat
   uses the 3276/65536 fertility threshold (approximately 5%) inland. Wheat
   beside a connected beach bypasses that cutoff, including empty growing
   cells immediately adjacent to live wheat. The exemption persists after
   growth so a low-fertility corner cannot expose the crop it just reserved.
   Unrelated empty coast remains open. Wood retains its adaptive threshold.
   Coastal backing follows eight-connected sand from actual water, including
   blended beach tiles and map wrapping; isolated inland sand does not count.
   Throughout either filled-in farm
   interior, Nicowar's permanent odd/odd cell in each 2x2 block is protected.
   At the landward creeping edge, protection expands to the two aligned
   matching-parity classes (even/even and odd/odd, one-in-two), leaving
   the cross-parity half as worker gates. Every odd/odd interior seed is therefore
   also a boundary seed. Harvested holes and boundary movement cannot swap its
   protection onto the opposite checkerboard phase.
4. Wood inside the configured fertility firebreak is open, even when it would
   otherwise be part of the wood farm pattern.
5. Wheat and wood not selected by the shared farm pattern remain harvestable,
   including on strategic barrier candidates. Topology supplies gate and access
   contracts; it does not fill the farm pattern's open cells.
6. Other resources remain harvestable.

After passive coastal access is resolved, wood next to protected wheat yields
its protection to the wheat-invasion clearing contract when that sub-policy is
on. This final precedence also applies to temporary fallback wood anchors.

The moving wheat and wood perimeters are forbidden before the resource spreads into them. This closes the
resource-growth/AI-update race in which a worker could harvest a brand-new edge
tile before Maxima noticed it. A shoreline-backed frontier is continuous, so
each farm keeps extending its productive barrier along the coast. The frontier
advances only after the matching resource actually grows; old edge cells become
harvestable as they enter the farm interior. The
water gradient identifies the continuous outer barrier so Maxima retains the
most productive shoreline land and its defensive screen without making the
settlement-facing inner barrier solid. A water-facing classification remains
hard on any tile touching water or coastal sand, including diagonal contact.
Coastal sand means a connected sand or blended-sand component touching actual
water; the entire beach qualifies, regardless of width. Isolated inland sand
does not qualify. A single linear flood fill labels the full map at farming-cache
initialization; cached eight-neighbor and cardinal-neighbor masks make subsequent
queries constant-time. They persist through harvesting, growth, and fog changes
and are rebuilt on map load alongside fertility. Harvesting behind a beach therefore cannot turn its shoreline
into a sparse landward edge. Away from that physical shoreline,
an edge is continuous only when it has waterward growth and no landward opening.
That waterward opening must have a route to actual water through cells containing
no resource or building, with water distance strictly decreasing at each step.
A neighboring wheat/wood crop or sand tile is not a landward growth
opening; that requires empty growing grass. Coastal access verification includes
walkable sand so the beach connects its grass farm edge to actual shore sources.
The same exposure check applies to empty pre-growth frontier cells. A harvested
hole behind other crops therefore does not become a new continuous outer wall;
ambiguous diagonal or concave edges are treated as landward and remain porous.
The sparse interior and landward lattice provide access behind it.
Wheat or wood omitted from that lattice is explicitly kept
out of the generic strategic barrier, so the older barrier policy cannot close
those lanes again. An
isolated off-lattice wheat patch retains one temporary local anchor until it
spreads onto the sparse lattice. A patch with no protected live resource also
retains one eligible fallback anchor. Odd/odd boundary seeds retain the normal
aligned pattern as they become interior seeds. Empty protected frontier cells do not count as live
anchors. The fallback disappears once the normal pattern protects a live seed.
Wood firebreaks, authorized wood campaigns, wheat-invasion clearing, and hard
placement or emergency-gate contracts retain their explicit precedence.

## Wood pressure

The default integer scores are:

```text
wood_supply = clamp(0,100, accessible_wood * 300 / (population + 30))
construction_pressure = clamp(0,100, recent_construction_failures * 25)
wood_clear_pressure = clamp(0,100,
    50 + (50-space_capacity)/2 + (wood_supply-50)/3
       + construction_pressure/3 - (growth_demand-50)/4)

minimum_wood_fertility = 15% + round(10% * wood_clear_pressure / 100)
```

The resulting wood cutoff ranges from 15% through 25%, with a neutral pressure
of 50 producing a 20% cutoff. The terms and divisors are tunable, while the
formulas remain monotone with wood
surplus, construction failures, and loss of space. Remote forests are not
actively cleared merely because they fall below the protection threshold.
Buildings do not suppress nearby farm protection; only exact planner footprints,
access rings, arteries, and maintained strategic openings override it.

## Barrier-backed farming, barriers and gates

Farming classification, resource components and access repair use the full map,
independent of discovery. Forbidden-area orders still apply to discovered cells.

The topology check runs every 500 ticks, or when terrain or owned/known enemy
buildings change. A separate geometry signature retains existing gates through
ordinary growth and harvesting. Military guard areas consume this geometry;
they no longer feed it back into the next farm contour.

Coastal candidates are grass beside a water-connected beach, within 20 grass
path steps of a completed owned building. Connected economic cores within half
that envelope group the buildings into local settlements; the remaining grass
is assigned to the nearest core. Fragments around the same settlement share two
disjoint, connected three-cell mouths. Separate colonies receive their own pair.
The largest settlement is considered first for emergency servicing. Inland
military choke analysis remains a separate defense policy.

Each mouth is validated with every other candidate wall cell closed. Its
channel descends to an actual building entrance and includes a clearable beach
outlet. The channel has a three-cell ribbon where terrain permits; it has no
arbitrary depth cutoff. Its first interior steps provide defensive staging
points. The routes cannot slide along and cut additional coastal wall cells.

Initial selection minimizes resource burden plus a bounded relocation penalty.
Among equal-cost choices it prefers legal tower pads covering complete mouths
and their interior approaches, with a bonus for a shared firing position, then
maximizes separation. Existing structurally valid pairs and routes persist
through regrowth. They are infrastructure obligations, not disposable harvest
shortcuts. New topology can invalidate them; loading rebuilds the derived plan.

After passive farm porosity, an independent building-to-beach flood assumes only
promised channels are cleared. A disconnected building receives a minimum-cost
inland branch to an existing beach-connected gate route. Empty forbidden cells
cost no resource; protected wheat costs more than ordinary crops. These branches
remain reserved after clearing and share links where possible. This explicit
access contract applies to the starting mainland as well as remote islands and
may override a seed when necessary. A missing legal connection is reported as
`unresolved_access`; it is not treated as successful access.

See [the redesign rationale and validation](MaximaBarrierRedesign.md) for the
Garden 3 failure, assumptions, comparative results and reproduction commands.

The selected barrier is applied only on existing wheat or wood farms and their
eligible immediate growth frontier; unrelated empty coastline stays open. Exact building and circulation reservations, gates and escape
channels take priority. The passive access repair still checks for trapped
farm pockets after this protection is applied to the plan.

Access repair first tries the inland protected cells, preserving the coastal
wall wherever those openings restore access. It falls back to coastal openings
only for pockets that remain trapped.

Island access handling is restricted to growing regions separated from the
starting base by non-grass terrain. The region calculation uses terrain alone;
resource growth, harvesting, discovery and new buildings cannot move its boundary.
Ordinary mainland farming does not undergo this repair.

Within an exceptional island region, a fully protected multi-tile wheat patch
receives stable lattice openings before the pocket check. Singleton seeds stay
protected for regrowth. Every repair envelope excludes the fixed odd/odd seed
reserve, including shortest-path repairs. A route that would require sacrificing
that reserve is not cut. Explicit building/path/gate contracts remain overrides.
The strategic barrier proposal is immutable during passive repair; a local
opening cannot change the next protection pass's topology input.

Passive coastal openings always use the cross-parity half of the same global
2x2 pattern. Connectivity changes cannot choose the opposite phase or depend on
component numbering. When a contour remains sealed, the minimum-cost cardinal
repair can open additional cells; access takes precedence in those local repairs.
Access is checked separately for every open farm pocket, including harvestable
wheat and wood without a water-gradient interior marker. An accessible farm
elsewhere on the same island cannot satisfy a trapped pocket's contract. Only
coastal contours enclosing trapped pockets receive checkerboard openings; each
remaining trapped pocket receives its own verified repair route. Porosity
component telemetry counts these pockets rather than entire landmasses.
These openings remove forbidden protection only and never request clearing flags. Gate cells are never protected.

Tower placement uses a dedicated coverage field, weighted by
`military.tower_barrier_bonus` (default 6). It measures the engine's square firing
range around the tower footprint and requires coverage of the whole mouth and
its interior approach. Existing towers suppress rewards for already-covered
mouths. Gate geometry does not increase generic protectedness, which would also
attract economic buildings into the firing lane. New construction and upgrade
footprints cannot occupy a maintained route; circulation can share it.

A food-secure settlement with at least `farming.gate_clearing_workers_min` workers
(default 50) and a local threat of at least
`military.emergency_barracks_threat_min` (default 4) may request towers for
uncovered, feasible gates. The total for this additional demand is capped at
`military.tower_active_count + 1` (normally two). Ordinary emergency defense can
request more. Existing preemptive guard staffing may use the interior staging
points; gate planning does not itself recruit more warriors.

## Fertility firebreak

The default 5%-through-14% fertility band receives a persistent clearing area.
It forms a contour between productive shoreline wood (15% and above) and sterile
inland wood (below 5%). Wood in that middle band is not farm-protected, so normal
clearing labor cuts and maintains the contour. Empty cells, wheat cells and
permanent resources are excluded; newly grown wood joins on the next policy pass.

Wheat and its pre-growth frontier have priority over this ordinary firebreak.
Wood uses the same farming policy everywhere else, but the wood-only inner
firebreak deliberately overrides wood protection inside its fertility band.
Hard building/path reservations and emergency gates still have higher priority.

## Maintenance clearing areas and emergency flag

Maxima continuously applies owned clearing areas to the fertility firebreak,
actual or pending building footprints, access rings, circulation arteries,
strategic gates, complete core-side escape channels and settlement access branches. Reserving a future upgrade
footprint or an unused campus slot does not clear its resources. Once an upgrade
wins selection, its immediate target footprint receives a temporary clearing
contract. The upgrade waits for clearing and retains its quota slot; revoked
authorization releases that temporary contract. With
resource-preserving circulation enabled, each actual or pending campus member
keeps an entrance connected to worker-reachable land. If resources obstruct that
connection, maintenance clears a path within the reserved circulation, minimizing
resource burden before path length. An enclosed empty entrance does not count as
connected. Smaller initial buildings
also receive an entrance at their current boundary inside their reserved parcel.
Farming protection is removed from cells that need clearing. When a clearing
obligation disappears, Maxima removes only clearing cells previously owned by
this maintenance subsystem, including obsolete whole-parcel masks from saves.

The existing serialized clearing-flag ID and timestamps are reused for urgent
directed work. Priority is:

1. If both gates of any local settlement or their maintained channels are physically
   blocked, the gate-clearing flag starts
   immediately, regardless of tick, population, spare labor, hospitals, health,
   food headroom or recovery state. It enables only resource types actually
   obstructing the selected clearing target and persists until a complete
   gate-and-approach route reopens. When an approach is blocked, directed work
   advances from a worker-reachable obstruction toward the gate in small steps.
   Its configured radius (one by default) expands only when needed to cover
   diagonal gate cells under the engine's circular range rule.
   Being boxed in preempts an ordinary construction-space clearing campaign.
2. If an internal building-access branch is obstructed, it receives emergency
   service after completely blocked gate pairs. This prevents a remote branch
   from monopolizing the single clearing flag while the main base is sealed.
   Emergency staffing uses the configured clearing crew or up to three workers,
   bounded by workforce size. One worker was too slow against fertile regrowth.
3. If only one gate is blocked, wood-only obstruction may be serviced at the
   ordinary proactive-clearing worker threshold. Wheat or another costly resource
   is deferred until the colony has at least 50 workers, spare clearing labor, a
   hospital, no hunger or recovery emergency, and mature-economy food headroom.
4. Otherwise, a bounded wood-clearing campaign may run for construction pressure,
   abundant nearby wood, or wood intruding into a maintained placement/gate lane.
   The flag starts unstaffed, installs a wood-only resource selector, and only then
   assigns its worker, so its radius may safely overlap a wheat farm.

Construction-space pressure is derived continuously. It becomes active when the
unpenalized local space-capacity score falls below 35, or after three placement
cycles are recently blocked by clearable resources. This ensures resource
regrowth can activate the campaign instead of merely invalidating placements.

Ordinary proactive clearing still pauses during recovery and remains worker-,
quota-, duration- and cooldown-bounded. It is renewable rather than capped at two
campaigns, and unrelated wounded units do not suppress it. Maintenance areas and
boxed-in gate recovery remain standing spatial invariants.

## Scheduling and telemetry

The complete policy normally runs every 512 ticks and every 64 ticks while a gate
is obstructed, a gate is reopening, or another urgent farming condition holds. Derived
masks are reused and only real forbidden-area deltas produce orders. Telemetry
reports cache/path time, policy time, protected seeds, expected expansion,
blocked directions, barrier/gate counts, wood pressure/threshold, buffer radius,
area deltas, and clearing reason. Access telemetry distinguishes blocked maintained
branches (`blocked_access`) from buildings with no legal repair (`unresolved_access`).
`defendable_gates` counts uncovered mouths with a feasible tower pad, not towers
that have already been funded or built.

## Stability regression contract

`MaximaFarmingIntegrationTest::seedStabilityAcrossMaps` runs twelve adversarial
harvest/regrowth cycles on Holiday Island 2, Archipelago, Isles, Migration,
Garden 3, A big pond, Wild River and Sand River. Every currently harvestable
wheat tile is removed, then neighbors of surviving seeds regrow. Fixed seeds
must survive and remain protected through every cycle. The Archipelago
regression separately checks harvest access while its triggering interior tile
appears and disappears. Discovery consistency, gate contracts, inland sand,
map wrapping and ordinary seed survival retain separate tests.

This tests persistence across time, rather than only counting forbidden tiles
in one snapshot. Growth-frontier protection may still change as resources grow;
fixed live seeds must not be exposed by those changes or by access repair.

## Farm management distance

`farming.management_radius` defaults to 16 tiles and is exposed through the
strategy parameter registry for optimization (range 0–128; 0 disables the
cutoff). New protection requires an allied or own physical building within
the inclusive Euclidean radius, measured from its map position with wrapping.
Virtual flags do not anchor farms. Building sites count as physical buildings.
The limit applies to wheat, wood, and empty expansion protection, after the
normal classification and before access repair. Established protected odd/odd
wheat seeds are retained during the running game if the building is lost;
empty expansion cells are not grandfathered. The previous-plan cache is rebuilt
on load, so loading also reapplies the configured radius to legacy protection.
This restriction does not add a tactical digout override.

The wood firebreak clearing zone uses this same radius and shared proximity
calculation. Existing firebreak clearing outside the radius is removed on the
next maintenance update. Physical access, gate, and construction clearing
contracts retain their independent scope. Unlike protected wheat seeds,
firebreak clearing is not retained after losing its nearby building anchor.
