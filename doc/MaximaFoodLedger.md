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

This replaced the older rule that retired only swarms with no farm capacity at
all, which has since been removed: with `food.enabled` off, nothing is retired.

## Relocation

A building that is far from its wheat, or starving where it stands, is not
always best retired: often the same inn or swarm would be worth keeping a few
tiles away. Relocation rebuilds it there first and retires the old one only
once the replacement stands, so the settlement never loses the capacity in
between.

**Nomination** is the executor's job (`Maxima::update_food_relocation`). A
completed, retirable inn or swarm whose ledger quality stays at or above
`food.relocation_min_quality_tiles` for `food.relocation_confirm_ticks` is a
candidate; quality already charges unreachable demand at the penalty distance,
so a starving building looks far even when its wheat is close. The last inn and
the last swarm are exempt, as in retirement: early on it is the settlement's
only one, starving while its first farm grows in. Establishing colony swarms
are exempt, and so is anything that could not clear a gain floor even at a
perfect site: a swarm with coverage above 75% earns no distance credit and has
nothing left to gain. When nothing is under attack, nobody is critically
hungry, no recovery posture is active and `food.relocation_cooldown_ticks` have
passed since the last relocation ended, the worst candidate is nominated. One
nomination is live at a time; the nominated building is exempt from burden
retirement while it waits, and retirement resumes if the nomination is
abandoned. Relocation runs before retirement in the same pass for that reason.

**Siting and pricing** is the planner's job. The nomination becomes a
`Relocation` intent for the same building type carrying `replacesBuildingId`.
Candidates are generated, scored and capacity-checked exactly like a new inn or
swarm, but against a ledger from which the old building has been removed, so
its own wheat is free for the replacement. Each candidate then has to pass
`Planner::appraiseRelocation`, all in worker-ticks:

```text
flowing   = min(old claimed, new claimed)            # wheat delivered either way
distance  = flowing * 2 * carrier_ticks_per_tile
            * (old quality - new quality) * realisation_kind
coverage  = (new claimed - old claimed)
            * (carrier_fixed_ticks_per_trip + 2 * carrier_ticks_per_tile
               * (supply radius + unreachable penalty))
saving    = distance + coverage                      # per tick
cost      = margin% * sum over levels 1..current, resources r:
              units_r * (fixed + 2 * ticks_per_tile * distance to r + builder step)
viable    = saving > 0 and cost / saving <= payback horizon
            and (realised quality gain >= min gain tiles
                 or coverage gain >= min coverage gain percent)
```

The old building's quality and coverage come from the full ledger; the
candidate's come from the residual the excluded ledger leaves
(`Ledger::residualQuality`, `reachableResidual`), i.e. what it would get among
the current claimants. Coverage shortfall is priced at the ledger's own
convention for unreachable demand: a trip to the supply radius plus the
penalty. Distance savings are realised per kind
(`food.relocation_inn_distance_realisation_percent`, default 155, and
`..._swarm_...`, default 0), because the [calibration](#carrier-cost-calibration)
showed inn carriers walk less when quality improves and swarm carriers do not;
a swarm therefore relocates only for coverage. Every level the old building has
is charged again, so an upgraded inn must save proportionally more. A candidate
that fails is reported as `negative_utility`.

**Completion.** The replacement is an ordinary planner action: reserved,
issued, observed and completed like any build, and a relocation pair counts as
one building for the director's targets until the old one is gone. When the
action reaches `Completed` the executor issues `DestroyBuilding` for the old
building. Because the replacement already stands, an attack does not block this
the way it blocks a plain retirement; critical hunger or a recovery posture
does, since the old building may still hold stock, and so does the inn-seat rule
with seats weighted by their inn's coverage (a starving inn's seats are not
seats). If the build is invalidated, rejected or destroyed, if the planner
scans every site and refuses them all, if a busy planner never reaches the
offer within `food.relocation_offer_ticks`, or if the destroy stays deferred
for `food.relocation_cooldown_ticks`, the nomination is abandoned (keeping both buildings in the
last case) and the cooldown restarts. Telemetry: `food_relocation_nominated`, `_lifecycle`,
`_deferred`, `_destroy`, `_done`, `_abandoned`.

| Parameter | Default | Role |
| --- | --- | --- |
| `food.relocation_enabled` | true | switch |
| `food.relocation_min_quality_tiles` | 5 | never nominate closer buildings |
| `food.relocation_confirm_ticks` | 3000 | quality must persist |
| `food.relocation_cooldown_ticks` | 6000 | between relocations; also how long a deferred retirement may wait |
| `food.relocation_offer_ticks` | 3000 | how long a busy planner gets before the offer lapses |
| `food.relocation_payback_horizon_ticks` | 15000 | the cutoff |
| `food.relocation_cost_margin_percent` | 125 | bias toward keeping |
| `food.relocation_min_gain_tiles` | 3 | churn floor on distance |
| `food.relocation_min_coverage_gain_percent` | 25 | churn floor on coverage |
| `food.carrier_ticks_per_tile` | 23 | measured |
| `food.carrier_fixed_ticks_per_trip` | 105 | measured |
| `food.builder_ticks_per_step` | 32 | one build action |
| `food.relocation_inn_distance_realisation_percent` | 155 | measured |
| `food.relocation_swarm_distance_realisation_percent` | 0 | measured |

The horizon only binds for expensive buildings. At the defaults a level-1 inn
(three wood) pays back within about two tiles of improvement, so the gain
floors govern it; a level-3 inn needs roughly seven tiles at 15 000 ticks and
half that at 30 000, which is where the horizon is the decision.

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

## Carrier cost calibration

Relocating an inn or swarm is worth its rebuild only if the carriers it saves
repay the cost, so distance has to be priced in worker-ticks. Rather than derive
that from unit speed tables, the engine times real round trips: when a carrier
leaves a building after a delivery it stamps `Unit::fetchStartTick`, remembers
the cell it harvests, and on the next delivery `Building::recordDeliveryTrip`
adds the trip's ticks and its wrap-safe Chebyshev distance from the harvested
cell to the footprint edge. Those counters are diagnostics only: never saved,
never checksummed, and read solely by Maxima's `food_delivery` telemetry, which
publishes them cumulatively on every building pass next to the ledger's
per-building `food_consumer` quality.

`tools/calibrate_maxima_carrier_cost.py` joins the two streams per building and
fits the windows between ledger samples:

```bash
build/src/glob2 -test-games-nox 2 --map Garden_3 \
    --matchup maxima,maxima,maxima,maxima -maxima-telemetry > garden3.log
python3 tools/calibrate_maxima_carrier_cost.py garden3.log triangle.log ...
```

Seven all-Maxima headless games (Garden 3 x2, Triangle x2, balanced_for_2 x2,
Isles), 19 896 timed round trips in 1 663 windows, measured on 2026-09-12:

| Fit | Intercept | Slope | Weighted R^2 |
| --- | --- | --- | --- |
| trip ticks vs harvest tiles, all | 105 | 46.2 per tile | 0.66 |
| trip ticks vs harvest tiles, inns | 42 | 44.9 per tile | 0.72 |
| trip ticks vs harvest tiles, swarms | 107 | 51.4 per tile | 0.55 |

The binned medians rise monotonically from ~95 ticks at 0-2 tiles to ~860 at
14-16, so the line is not an artefact of outliers. That gives
**`carrier_ticks_per_tile` = 23** (half the round-trip slope) and
**`carrier_fixed_ticks_per_trip` = 105** (42 for inns; a busy swarm's carriers
queue at the door). Median carrier utilisation was 0.76, so idle time is not
inflating the trips.

The same data tests the proxy the relocation policy prices with. Ledger
`quality` is the route distance to the *protected* wheat that would cover full
demand, and carriers do not harvest protected cells; they harvest whatever the
gradient finds nearest, mostly the spread around the farms.

| Kind | harvest tiles vs quality tiles | Weighted R^2 | Median harvest tiles by quality band |
| --- | --- | --- | --- |
| inn | 1.9 + 1.55 x | 0.12 | 5.3 (0-2), 8.0 (2-4), 9.9 (4-6), 12.2 (6-8), 14.8 (8-10) |
| swarm | 6.6 + 0.02 x | 0.00 | 5.5 (2-4), 7.4 (4-6), 6.6 (6-8), 5.4 (8-10), 8.5 (10-12) |

For inns quality is a usable proxy: each tile of quality is about 1.5 tiles of
real walking, monotonically. For swarms it predicts nothing. A swarm's twenty-odd
carriers strip the nearby spread and walk 5-8 tiles wherever the protected
stacks are, so moving a swarm closer to protected wheat does not shorten its
trips; what a swarm gains from a better site is *coverage*, i.e. enough supply
to keep those carriers delivering at all. The relocation policy therefore
realises distance savings per kind (`food.relocation_distance_realisation_percent`,
155 for inns, 0 for swarms) and prices coverage shortfall separately.
