# Maxima strategic farming

Maxima owns a private farming planner. It does not alter `Map`, the shared AI
runtime, or save-game data. Its fertility cache and derived masks are rebuilt
after construction/load and are intentionally not serialized.

## Implementation model

The runtime policy follows a plan/reconcile pipeline rather than incrementally
editing shared masks:

1. `build_farming_protection_plan` classifies wheat and wood roles and resolves
   their priority against reservations and firebreaks.
2. `resolve_wheat_invasion_clearing` gives wood beside the final protected wheat
   mask the same hard clearing priority used by maintenance. It uses this pass's
   wheat mask, so new and revoked obligations do not depend on stale maintenance.
3. `apply_farming_protection` emits only the difference between the desired plan
   and the forbidden area previously owned by this subsystem.
4. Building circulation and wood firebreaks use the same model:
   `build_maintenance_clearing_plan` produces separate hard-circulation and
   renewable-firebreak masks, then `apply_maintenance_clearing_plan` reconciles
   them once.

Active clearing flags have explicit wood and legacy-save campaign states
instead of relying on an unexplained numeric sentinel throughout the executor.

## Independently tunable behavior

All switches below default to `true` and are present in the optimizer's
`farming` parameter stage:

- `farming.farm_protection_enabled`
- `farming.maintenance_clearing_enabled`
- `farming.resource_preserving_circulation_enabled`
- `farming.wheat_invasion_clearing_enabled`
- `farming.wood_firebreak_enabled`
- `farming.proactive_clearing_enabled`

`farming.enabled` remains the master switch. Resource-preserving circulation,
wheat-invasion cleanup,
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
2. The growing edge of a wheat or wood farm, and compatible empty grass
   continuing that edge, are protected on the expansion lattice only. Wheat
   uses the 3276/65536 fertility threshold (approximately 5%) inland. Wheat
   beside a connected beach bypasses that cutoff, including empty growing
   cells immediately adjacent to live wheat, so a farm can still follow the
   most fertile coastal growth path. The exemption persists after growth so a
   low-fertility corner cannot expose the crop it just reserved. Unrelated
   empty coast remains open. Wood retains its adaptive threshold.
   Coastal backing follows eight-connected sand from actual water, including
   blended beach tiles and map wrapping; isolated inland sand does not count.
   Throughout either filled-in farm
   interior, Nicowar's permanent odd/odd cell in each 2x2 block is protected.
   At the creeping edge, protection expands to the two aligned
   matching-parity classes (even/even and odd/odd, one-in-two), leaving
   the cross-parity half as worker lanes. Every odd/odd interior seed is therefore
   also a boundary seed. Harvested holes and boundary movement cannot swap its
   protection onto the opposite checkerboard phase.
3. Wood inside the configured fertility firebreak is open, even when it would
   otherwise be part of the wood farm pattern.
4. Wheat and wood not selected by the shared farm pattern remain harvestable.
5. Other resources remain harvestable.

Wood next to protected wheat yields its protection to the wheat-invasion
clearing contract when that sub-policy is on. This final precedence also
applies to temporary fallback wood anchors.

The moving wheat and wood perimeters are forbidden before the resource spreads
into them. This closes the resource-growth/AI-update race in which a worker
could harvest a brand-new edge tile before Maxima noticed it. The frontier
advances only after the matching resource actually grows; old edge cells become
harvestable as they enter the farm interior.

Protection is restricted to the expansion lattice everywhere, including along a
beach. A shoreline run is therefore porous by construction and can never close
a land component: the cross-parity half of the 2x2 pattern always stays open as
worker lanes. Coastal sand still matters for growth, not for protection: wheat
backed by a connected sand or blended-sand component touching actual water
bypasses the inland fertility cutoff, so a farm follows its most fertile
growth path along the coast. Isolated inland sand does not qualify. A single
linear flood fill labels the full map at farming-cache initialization; cached
eight-neighbor and cardinal-neighbor masks make subsequent queries
constant-time. They persist through harvesting, growth and fog changes, and are
rebuilt on map load alongside fertility.

An isolated off-lattice wheat patch retains one temporary local anchor until it
spreads onto the sparse lattice. A patch with no protected live resource also
retains one eligible fallback anchor. Odd/odd boundary seeds retain the normal
aligned pattern as they become interior seeds. Empty protected frontier cells
do not count as live anchors. The fallback disappears once the normal pattern
protects a live seed. Wood firebreaks, authorized wood campaigns,
wheat-invasion clearing, and hard placement contracts retain their explicit
precedence.

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

## Fertility firebreak

The default 5%-through-14% fertility band receives a persistent clearing area.
It forms a contour between productive shoreline wood (15% and above) and sterile
inland wood (below 5%). Wood in that middle band is not farm-protected, so normal
clearing labor cuts and maintains the contour. Empty cells, wheat cells and
permanent resources are excluded; newly grown wood joins on the next policy pass.

Wheat and its pre-growth frontier have priority over this ordinary firebreak.
Wood uses the same farming policy everywhere else, but the wood-only inner
firebreak deliberately overrides wood protection inside its fertility band.
Hard building and path reservations still have higher priority.

## Maintenance clearing areas and emergency flag

Maxima continuously applies owned clearing areas to the fertility firebreak,
actual or pending building footprints, access rings, circulation arteries,
and settlement access branches. Reserving a future upgrade
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

1. If an internal building-access branch is obstructed, it receives emergency
   service. Emergency staffing uses the configured clearing crew or up to three
   workers, bounded by workforce size. One worker was too slow against fertile
   regrowth.
2. Otherwise, a bounded wood-clearing campaign may run for construction pressure,
   abundant nearby wood, or wood intruding into a maintained placement lane.
   The flag starts unstaffed, installs a wood-only resource selector, and only then
   assigns its worker, so its radius may safely overlap a wheat farm.

Construction-space pressure is derived continuously. It becomes active when the
unpenalized local space-capacity score falls below 35, or after three placement
cycles are recently blocked by clearable resources. This ensures resource
regrowth can activate the campaign instead of merely invalidating placements.

Ordinary proactive clearing still pauses during recovery and remains worker-,
quota-, duration- and cooldown-bounded. It is renewable rather than capped at two
campaigns, and unrelated wounded units do not suppress it. Maintenance areas
remain standing spatial invariants.

## Scheduling and telemetry

The complete policy normally runs every 512 ticks, and every 64 ticks while an
urgent farming condition holds. Derived masks are reused and only real
forbidden-area deltas produce orders. The `farming_policy` record reports
`microseconds`, `protected_seeds`, `protected_frontier`, `protected_wheat_edges`,
`protected_wheat_bootstraps`, `protected_wood_edges`, `protected_wood_bootstraps`,
`protected_interior_seeds`, `expected_capacity`, `blocked_directions`,
`wood_pressure`, `wood_fertility`, `added` and `removed`. Clearing emits
`land_clearing_started`, `land_clearing_finished` and `maintenance_clearing`
with its reason.

## Stability regression contract

`MaximaFarmingIntegrationTest::seedStabilityAcrossMaps` runs twelve adversarial
harvest/regrowth cycles on Holiday Island 2, Archipelago, Isles, Migration,
Garden 3, A big pond, Wild River and Sand River. Every currently harvestable
wheat tile is removed, then neighbors of surviving seeds regrow. Fixed seeds
must survive and remain protected through every cycle. The Archipelago
regression separately checks harvest access while its triggering interior tile
appears and disappears. Discovery consistency, inland sand, map wrapping and
ordinary seed survival retain separate tests.

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
next maintenance update. Physical access and construction clearing
contracts retain their independent scope. Unlike protected wheat seeds,
firebreak clearing is not retained after losing its nearby building anchor.
