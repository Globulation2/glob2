# Maxima food ledger

Maxima used to cap inns and swarms with a single team-wide corn total and site
them by distance to the nearest wheat. Neither is a local statement, so one
wheat tile could attract an unlimited number of inns, and nothing noticed that a
settlement had more food buildings than its farms could ever supply.

The food ledger replaces both. It asks one question: which inns and swarms are
actually backed by protected farm capacity, and how much capacity is unclaimed.

## Supply is protected wheat carrying wheat

Only cells in the wheat farm protection mask that currently hold wheat count.
That is deliberate: unprotected wheat is harvested away, so it is not capacity a
settlement keeps. Protected stacks are the producers, and their growth spreads
into the harvestable cells around them
(`Map::incResource`, `src/map/MapResources.cpp`).

A cell's supply, in micro-wheat per tick, is

```text
yield = fertility/65536 * 1/growth_period_ticks * absorbing_neighbours/8
```

`growth_period_ticks` (default 186) is how often `Map::growResources` samples one
cell, including wheat's one-in-three growth gate. Growth that lands on a full
stack, a building or unsuitable ground is wasted, so a protected cell hemmed in
by full wheat yields nothing. Passing units are ignored, because they move every
tick and would make a standing estimate flicker.

## Demand comes from the engine

- **Swarm:** `resourceForOneUnit / unitProductionTime`, five wheat per 150 ticks
  at full output.
- **Inn:** the modelled population it serves (`model.inn_capacity_levelN`) times
  the rate a fed unit eats, one wheat per `food.ticks_per_meal` ticks.

`ticks_per_meal` is **measured, not derived**. Hunger drains `hungriness` (350)
from `HUNGRY_MAX` (150000) once per unit *action*, not once per tick
(`Unit::handleMedical`, called from `endOfAction`), and it does not drain at all
while a unit is inside a building. An action completes when `delta` passes 256
at `stepSpeed` per tick (`Unit.cpp:292-301`), so it takes about `256/speed`
ticks: 16 for a level-0 worker walking, 32 while harvesting or building, less at
higher levels. The naive `HUNGRY_MAX / hungriness` = 428 is therefore an
**action** count, and using it as ticks understates inn demand's period by more
than an order of magnitude.

Counting real `eatOnce` calls in a headless all-Maxima game on Garden 3 gives
**11759 ticks per meal per unit**, aggregated over ticks 20000-66000; the 23
individual two-thousand-tick windows ranged 9441-14043. That sits inside the
6900-13700 band the engine constants predict, so measurement and derivation
agree. The value is calibrated on one map and one AI mix, so treat it as a
default rather than a constant of nature.

**`food.growth_period_ticks` is still only derived** from the growth rule, and
is the remaining value worth calibrating against a measured game.

## The ledger is rebuilt every pass, never stored

Claims are a pure function of the current buildings, their levels, the planned
actions and the wheat on the map. A destroyed building drops out and releases
its claim, an upgraded inn claims its new demand, and a burned or regrown farm
changes supply, all without incremental bookkeeping. Only the burden timers are
saved.

**Claim order**, in this priority:

1. Placement quality band, best first
2. Level, higher first
3. Completed buildings, then sites, then reserved parcels
4. Building id

Quality is the supply-weighted mean route distance to the wheat covering a
building's full demand, measured alone, ignoring every other claimant. Measuring
it alone keeps the order from feeding back into itself. Ranking by quality
rather than age means an early building in a poor position loses its wheat to a
later, better sited one, which is what makes retirement select the right
building. Bands (default two tiles) stop small farm changes from reordering
claimants and shuffling shortages between them.

Inns and swarms are ranked separately and then alternate, because a settlement
needs food for the citizens it has and births for the citizens it wants.

Each claimant then takes its nearest wheat first, up to its demand. What remains
on each cell is the residual that new buildings are judged against.

## Placement, upgrades, colonies

- A new inn or swarm must reach `food.placement_margin_percent` (default 120%)
  of its full demand in capacity nobody else claims. Rejections are reported as
  `food_capacity`.
- The **first inn and the first swarm are exempt**, or a settlement that has not
  started farming could never start.
- An **upgrade** is judged on what the building could reach at its new level,
  since the rebuilt ledger releases and retakes its own claim. Badly sited inns
  therefore stop collecting upgrades that would starve their neighbours.
- **Colony swarms claim like any other swarm** but keep their own placement
  rule. A colony settles land that has no protected farm yet, so the ledger
  would otherwise refuse every colony site.
- Siting now scores unclaimed capacity instead of distance to the nearest wheat.
  Settlements spread out on their own, because claimed wheat stops attracting
  buildings.

A candidate is first tested against a summed-area bound over the residual. Reach
is contained in that square, so the bound can only overstate it and never
rejects a site the exact walk would accept.

## Burden retirement

A building whose coverage stays below its burden threshold
(`food.inn_burden_coverage_percent`, default 60) for
`food.burden_confirm_ticks` (default 3000) is retired. Coverage at or above
`food.recovered_coverage_percent` (default 85) clears the confirmation, so a
brief dip does not accumulate and two similar buildings cannot trade places
forever.

The retirement threshold sits well below the placement margin on purpose:
removing a building below 60% frees less than 0.6 of its demand, while a new one
needs 1.2, so the planner can never delete a building and immediately rebuild in
the same place.

A retirement also requires all of:

- nothing under attack, no critical hunger, and no recovery posture
- not the last inn or the last swarm, and not a colony still establishing
- for an inn, the remaining reliable inn seats still cover the population. The
  ledger is an accounting view; workers stock whichever inn they reach, so seats
  people are still eating from are never removed.
- the retirement cooldown has elapsed

One building is retired at a time, lowest coverage first. Freeing its wheat
often clears the others without further deletions.

This supersedes the older rule that retired only swarms with no farm capacity at
all; `food.enabled` selects between them.

## Saturation

Inn and swarm targets are capped by what the ledger can supply: buildings that
are currently supplied, plus what the best single site's unclaimed capacity
could still support. A plain unclaimed total would add up remnants no single
building can reach.

When nothing more can be supported, the targets stop asking for food buildings,
their intents disappear, and construction slots and builders go to other work.
It resolves itself when protected cells fill with wheat or a colony opens new
land.

## Compatibility

- Save version 93 adds the per-cell supply to the saved placement snapshot.
  Version 92 saves load without it and rebuild supply on their next pass.
- AI orders are simulation inputs, so this invalidates existing replays and
  mixed-version network games.
- The ledger itself is derived and never serialized; only burden timers and the
  supported-target counts are saved, so a loaded game plans from the same
  numbers as the run that saved it.

## Tests

`test/MaximaFoodLedgerStandaloneTest.cpp` covers claiming order, quality
outranking age, inn/swarm interleaving, claim release on removal, the upgrade
check, the soundness of the summed-area bound, blocked ground, the supply
radius, and the engine-derived rates.
