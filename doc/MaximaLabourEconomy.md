# Maxima labour economy (design)

Status: **experimental**. The labour budget, the technology ordering and the
first military pull rules are implemented on a development branch
(`src/AIMaximaLabour.h`); see [Results so far](#results-so-far). The rest is
design.

## The system

Maxima's economy is a production system. Its inputs are wheat and wood, its
output is trained warriors doing damage to the enemy base, and the resource that
every stage competes for is **worker time**:

```text
worker-ticks x productivity per tick      (walk/build level, haul distance)
  -> wheat, wood                          (hauling)
  -> buildings, births, training          (investment)
  -> warriors x level                     (military capital)
  -> damage on enemy units and buildings  (output, recycled by healing)
```

Today each stage is governed separately: population thresholds gate buildings,
every inn and swarm asks for its own carriers ([staffing](MaximaStaffing.md)),
and the swarm controller funds births from food. Nothing prices one use of a
worker against another, so the colony keeps every worker busy and still loses.

## What the games showed

Measured on 2026-09-17 at master `e6d99cbeb`: 19 random generated maps (17
generators, 128 and 256 tiles), Maxima against Cabino in both seats, 38 games,
90 000 tick cap, plus mirror games and six strategy-layer variants (228 games).
Worker time was sampled every tick by a diagnostic counter in `TeamStats`
(phase 0 below); instrumented games reproduced the uninstrumented results
exactly.

**Maxima loses 7-25** (6 unresolved), but not on growth. Both AIs reach 25, 50
and 100 units at the same ticks, and Maxima has more births. Both take in the
same wheat (about 880 loads by tick 41 000). Cabino turns it into 2.1 times the
damage:

| Cumulative median at tick 40 960 | Maxima | Cabino |
| --- | --- | --- |
| wheat loads harvested | 899 | 867 |
| wood loads harvested | 84 | 198 |
| buildings completed (to tick 30 720) | 12 | 24 |
| warriors born | 44 | 33 |
| ability levels gained per warrior born | 2.6 | 4.4 |
| own HP lost / healed | 13.8k / 3.2k | 8.0k / 8.8k |
| warriors killed | 22 | 0 |
| melee damage dealt per 100 wheat | 778 | 1646 |

**The economy is a walking economy.** About 70% of all worker time is walking to
a resource or carrying it back; harvesting itself is about 5%. Wheat is cut
11-16 tiles from the swarm or inn it feeds, for both AIs.

| Median share of worker-ticks | 2k-10k Maxima | Cabino | 10k-20k Maxima | Cabino |
| --- | --- | --- | --- | --- |
| swarm supply | 55 | 34 | 39 | 26 |
| inn supply | 10 | 10 | 12 | 15 |
| construction | 9 | 20 | 14 | 17 |
| training | 5 | 5 | 5 | 17 |
| eating | 8 | 9 | 17 | 12 |
| hungry with no inn, hurt with no hospital | 0 | 0 | 2.4 | 0.3 |
| idle | 8 | 15 | 5 | 8 |

**Slack is what trains workers.** The engine sends a unit to a training building
only after it has been free and unassigned for more than 32 ticks
(`Unit::handleActivity`, `Team::findBestUpgrade`); no order can do it. Cabino
leaves workers free and builds a racetrack and school early, so by tick 20 480
its workers have gained (median) walk 13, swim 12, build 20 and harvest 20
levels. Maxima's have gained swim 14 and nothing else: the pool opens at
population 5, the racetrack at 69 and the school at 48, and its median free
worker count is 1.

**Thresholds cannot fix it.** Earlier hospitals, later attacks, more inn
capacity and earlier technology each did their local job (hospitals doubled
healing; earlier technology raised wood and schools) and each lowered the win
count (3-5 against 7), because every addition was paid for with labour taken
from something else. Maxima sits on its labour frontier; the frontier itself
has to move.

## Productivity model

Worker speeds come from the unit table (`src/game/entities/Race.cpp`): walk
16/21/26/30, build 8/12/16/20, harvest 8/9/10/11 by level. With `w` the share of
a worker's time spent walking, a trained worker's throughput is

```text
m(level) = 1 / (w / (walk[level] / walk[0]) + (1 - w))
```

| Walk level | 0 | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| throughput at w = 0.7 | 1.00 | 1.20 | 1.37 | 1.49 |

Harvest level is worth little (12% on 5% of time). Build level matters as a
gate: upgrades to level 2 and 3 need builders of that level.

A worker training visit costs about **1 000 worker-ticks** (690 inside, 320
walking; the same for both AIs) and yields 1.2-1.5 levels. Walk level 1 is
therefore about 0.2 of a worker for 1 000 ticks: 5 000 worker-ticks per
worker-equivalent, repaid in about 5 000 ticks. A born worker costs 5 wheat,
2 500 to 5 000 worker-ticks of hauling at measured rates, and repays in about
the same time. The two investments are comparable; what differs is their limit.
Births are limited by food, and carriers added beyond what food funds return
little (Maxima's swarms ran 40% utilised with 14 carriers against a funded 8).
Training is limited by seats and by slack, needs no meals or inn seats, and is
what Maxima never bought. A first racetrack adds its materials to the cost, so
it repays inside the horizon only once about eight workers can use it.

`w`, trip distances and inn distance differ by map, so they are measured in each
game rather than configured (see the labour ledger below).

## Design

### One currency

Every activity is a flow of worker-ticks per tick, and every building or
training decision is an investment with a cost and a return in that unit.
`Planner::appraiseRelocation` ([food ledger](MaximaFoodLedger.md)) already
prices a decision this way; this generalises it.

### Labour ledger (observation)

At the existing building cadence Maxima samples its own workers' states
(`medical`, `activity`, `displacement`, attached building class) and keeps
rolling shares: eating, training, idle, swarm supply, inn supply, construction,
and within hauling the walking share `w`; plus mean haul distance by building
class and mean distance to an inn when hungry. These are AI-side reads of its
own team, on Maxima's deterministic schedule. The ledger is an observation; it
issues nothing.

### Labour budget (allocation)

Each building pass observes the workforce and sets the labour left for births:

1. **Subsistence.** The carriers inns actually hold. Inn requests are upper
   bounds that routinely exceed the workforce, so requests are not budgeted.
2. **Planned slack.** A training reserve equal to open training seats that have
   an untrained worker to fill them, plus a small buffer, never more than a
   quarter of the workforce and nothing below eight workers. Slack is a budgeted
   use of labour, not a failure to assign it.
3. **Construction.** What sites are asking for.
4. **Swarm supply.** The remainder, never more than the swarm controller's
   food-funded birth labour, and never less than a floor (half the workforce
   while the colony has fewer than twelve workers, a quarter afterwards) so the
   budget cannot strangle an opening.

The budget publishes one **cap on swarm carriers**. It does not apportion
workers between individual buildings and does not estimate wheat near any
building, which is what the retired colony budget got wrong. It is a pure
function of the live colony (`src/AIMaximaLabour.h`). Its observations, plan,
and per-swarm allowances are retained between scheduler passes and persisted
from save version 109. Older saves remain readable: the loader reconstructs
the observation and plan and uses the restored swarm requests as allowances.
Exact pre-save labour state cannot be recovered from those older files.

### The per-building controller stays

The stock-band loop in [staffing](MaximaStaffing.md) remains the low-level
control: each inn and swarm still adds or returns one carrier at a time from its
own stock, with anti-windup. The budget only bounds it. When swarm requests sum
above the cap they are trimmed one worker at a time from the largest request,
never below the controller's minimum, and a trimmed swarm is "not receiving what
it asked for", so its loop holds rather than winding up. Trimming by ledger
quality instead of size is a possible refinement.

### Construction by investment appraisal

Population gates (`economy.*_population_min`, `upgrades.level*_population_min`,
target counts) are replaced by one rule: each candidate has a cost (materials x
measured haul trip + build time) and a benefit, both in worker-ticks, and the
planner builds the shortest payback under a horizon.

| Candidate | Benefit |
| --- | --- |
| racetrack, school | untrained workers x (m - 1), limited by slot throughput |
| inn | eating walk and wait saved x meals per tick, plus starvation avoided |
| inn or swarm nearer wheat | haul distance saved x flow (the relocation appraisal) |
| pool | only the labour or land it unlocks on this map |
| upgrade | the same benefit at the next level, less downtime |

Capacity is sized for population at now plus the build lead time. Maxima sets
its own birth schedule, so this is its plan, not a forecast.

### Military stage

The same currency extends to the output. A warrior's damage rate is its attack
speed times what its strength leaves after armour (warriors have armour 10), so
from the unit table:

| Warrior level | 0 | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| attack speed x (strength - 10) | 36 | 64 | 110 | 168 |

A level-2 warrior is worth three untrained ones before numbers compound, and it
costs the same wheat. Barracks level is therefore the largest multiplier in the
whole system, and an untrained warrior in the field is inventory thrown away.
Healing is the second: a defender beside its hospitals returns to the fight, and
in the measured games Cabino took 8 000 damage and lost no warriors.

Implemented so far:

- **Pull, not push.** Warrior births are paced to barracks seats: the untrained
  backlog may hold two per seat and never less than the home guard.
- **Barracks with the workforce.** The first barracks follows the first
  racetrack once fourteen workers can carry it, not a population mark.
- **Hospitals.** One from the first barracks on, then one per eight warriors.
- **Attack gate.** A new attack needs the configured minimum force and enough
  available combat power to match the fitted median enemy power. The
  [force model](../docs/maxima-force-model.md) uses fog-visible observations and
  their history; older saves retain their original attack-strength policy.
- **Seats are the constraint.** While the backlog limit is holding warrior
  births and warriors are still wanted, another barracks is requested (up to
  three). Measured neutral so far.

Still design: valuing a warrior in worker-ticks, withdrawing an attack whose
exchange has turned, keeping scouts out of tower range, and sizing hospitals to
expected damage intake.

## The production chain end to end

The output is damage on the enemy base. Nothing produces it directly; it is the
end of a chain of transformations, each with its own lead time, and the colony
is only as fast as the slowest link that is not yet started:

```text
worker-ticks ──> wheat, wood        (hauling; ~70% of the time is walking)
     │
     ├──> births          (5 wheat each; swarm site 35 wheat = seven births)
     ├──> buildings       (haul materials + build)
     └──> free time ──> school visits ──> BUILD levels
                                              │
                                              └──> barracks upgrades
                                                        │
                        warrior births ──────────> warrior training ──> damage
```

Two links are **unlocks, not rates**, and an appraisal that prices only rates
will always defer them:

- A **school** is the only source of the BUILD levels an upgrade needs.
  `Planner` caps concurrent level-one upgrades at `trained builders /
  upgrades.level1_trained_units_per_slot`, so with no school the colony cannot
  upgrade anything at all, at any price.
- A **barracks level** is the only source of warrior levels, and those are the
  largest multiplier in the game: by the unit table a level-2 warrior deals
  three times a level-0 one for the same 5 wheat.

### Measured lead times

From 114 headless games against Cabino, median ticks from the planner selecting
an action to the engine completing it:

| Action | Build | Upgrade |
| --- | --- | --- |
| inn | 1518 | 1517 |
| hospital | 1452 | 2029 |
| barracks | 1832 | 3019 |
| racetrack | 1843 | 3065 |
| pool | 2010 | 4030 |
| tower | 3038 | 3462 |
| school | — | 4578 |
| swarm | 5054 | — |

A worker or warrior training visit costs about **1000 worker-ticks** and yields
1.2-1.5 ability levels. First contact — the first raid of four or more enemy
warriors inside the colony — has a median of **tick 25 300**.

### The critical path to a trained army

Working backwards from contact at 25k, with those lead times:

| Stage | Must complete by | Because |
| --- | --- | --- |
| warriors at level 2 | 25 000 | contact |
| barracks at level 1 | ~22 000 | warrior visits, ~1000 ticks each |
| 8 builders at BUILD 1 | ~19 000 | barracks upgrade takes 3019 |
| school standing | ~17 000 | 8 visits through 4 seats ≈ 2000 ticks |
| school started | ~15 000 | build lead ~1800 |

Maxima used to complete its school at tick 15 900 and its first barracks
upgrade at 26 100 — one full link behind, arriving after the enemy. That is not
a tuning error in any single threshold; it is a chain whose start was scheduled
by a worker count rather than by a deadline.

### What this changes

Nothing yet. The chain says the school should be started by about tick 15 000
and Maxima starts it later, but the obvious remedy did not survive measurement
(below). What the chain has earned so far is the diagnosis and the numbers to
schedule against.

### What the chain does not explain

Four changes the chain argues for were measured and did **not** pay. Each is
recorded because the measurement is the useful part.

- **Starting the chain earlier.** Treating the first racetrack, school and
  barracks as enabling investments wanted at eight workers moved the school
  from tick 15 900 to 11 300 and the first barracks from 14 600 to 10 800. On
  the maps Maxima never won this turned four losses into stalemates. Over all
  114 games it was much worse: 31 wins to 62 losses against 41 to 47. Early tech
  labour costs more everywhere else than it buys on the hard maps, and the
  thirty-game subset it was tuned on was not representative.
- **Enforcing the training reserve.** The reserve was fiction: it was
  subtracted from the swarm cap, but nothing stopped construction sites from
  absorbing the workers, so measured idle was zero against a reserve of three.
  Binding site crews to the budget's construction share raised worker ability
  gains by two fifths and still lost more games, because on the maps that decide
  these matches construction throughput binds before training does.
- **Towers.** Cabino builds about three a game and loses almost no buildings;
  Maxima built none and lost its inns and hospitals faster than it replaced
  them. That was correlation: giving Maxima ground-threat towers left the
  record unchanged.
- **Gating swarms on saturation.** A swarm site costs 35 wheat, seven births,
  so it should only be bought when births are capacity-limited rather than
  wheat-limited. Measured, it only delayed the second swarm and changed no
  games. Carriers also turned out not to be the waste they looked like: a swarm
  at full rate consumes 33 wheat per thousand ticks and a carrier delivers
  about two, so seventeen carriers saturate one swarm and the returns to the
  eighth are diminishing but real.

## Where the deficit actually is

Measured per worker-kilotick over the games Maxima loses, the economy divides
cleanly into three conversions. Two of them are already at parity:

| Conversion (ticks 10 000-20 000) | Maxima | Cabino |
| --- | --- | --- |
| wheat delivered per food-carrier kilotick | 2.36 | 2.26 |
| births per swarm-carrier kilotick | 0.40 | 0.38 |
| **buildings completed per construction kilotick** | **0.123** | **0.253** |

Maxima hauls and breeds as well as Cabino and builds at half the rate. It then
spends 26-30% of its worker time on construction against Cabino's 16-17% and
still finishes fewer buildings, and that extra labour comes out of the slack
that would have trained it — a loop that tightens itself.

The cause is visible in the ability counters: by tick 20 480 Maxima's workers
have gained **8 build levels to Cabino's 22**. Worker build performance is
8/12/16/20 by level, so an untrained builder is half the builder a level-two one
is, before haul distance is counted at all.

This is why every reallocation experiment failed. Moving worker-ticks between
births, buildings and training cannot help when the problem is what a
worker-tick is worth. The returns have come only from raising that: the recon
calibration, which stopped wasting decisions on a fiction, and pricing the
carrier route when siting food buildings.

### A caution about the appraisal

`racetracksWorthBuilding` overrides `economy.first_racetrack_target` and
`economy.racetrack_population_min` for the first racetrack, so those two
strategy values no longer reach the decision. Anything that gates a building by
appraisal should say so where the parameter is documented, or the parameter
becomes a lie: setting the target to zero changed nothing in 114 games.

## Phases

0. **Measure.** Land the `TeamStats` worker time-use counters as proper
   diagnostic telemetry (not saved into the simulation path, not in checksums),
   and add Maxima's read-only labour ledger with telemetry. No behaviour change.
1. **Budget.** Labour budget with class caps over the existing controller, the
   training reserve, and technology ordered by payback.
2. **Appraisal.** Investment appraisal replaces construction gates and target
   counts; lead-time sizing.
3. **Military.** Warrior valuation, attack gating, hospital and barracks sizing.

Each phase is judged on the 38-game harness above and against the other AIs, so
the result is not fitted to Cabino. Win rate is the ground truth; population,
labour shares and damage per wheat are diagnostics, not targets.

## Results so far

All against Cabino on the 38-game harness (medians; "old" is master).

| | old | budget only | budget + military |
| --- | --- | --- | --- |
| swarm supply share of worker time, 10k-20k | 38% | 25% | 25% |
| wheat loads per food-carrier kilotick, 10k-20k | 1.69 | 2.03 | - |
| worker levels gained by tick 30 720 | 54 | 100 | 108 |
| warrior combat deaths by tick 40 960 | 22 | 13 | 8 |
| Maxima eliminated by tick 45 000 | 16 | 21 | 14 |
| Cabino eliminated by tick 45 000 | 5 | 1 | 2 |
| full games: won / lost / unresolved | 7 / 25 / 6 | 2 / 31 / 5 | 9 / 26 / 3 |

With scouts capped as well (`economy.explorer_cap` 14 to 3, floor 3 to 2) the
full-game result is 11 / 24 / 3. A combat-death diagnostic showed about seven
explorers a game dying over the enemy base, some 6% of all births; the rest of
Maxima's losses are at home, during Cabino's attack (workers 6.0, warriors 4.5
a game by tick 40 960, against 1.7 Cabino warriors lost there).

The budget makes the mid-game labour profile match Cabino's (swarm 25/26%,
construction 18/17%) and removes the over-staffed swarm carriers, whose marginal
wheat was small. Births equal Cabino's to tick 20 000. What the budget does not
change is the ten thousand ticks after that: Maxima still loses about 25 units
by tick 30 000 to Cabino's 8 (scouts over towers, workers and warriors on
offensive operations), so Cabino's economy pulls away and it wins the later
fight on numbers. Lessons kept in the code:

- Building requests are upper bounds that routinely exceed the workforce, so a
  budget built from requests strangles the colony. The budget uses actual inn
  carriers and site demand.
- Any cap on the opening slows it. Below sixteen workers the budget is off.
- `demands.mobility` is positive on land maps; only
  `environment.mobility_opportunity` says whether swimming pays.

## Compatibility and review

- Peers run AI controllers locally, so changed Maxima decisions require network
  protocol 34 and reject older clients. Replay playback executes recorded orders;
  its existing version floor remains 99. New saves use version 109, and the
  minimum readable save version remains 58.
- New director or controller state that must survive a save needs version-gated
  loading; the ledger's rolling shares should be rebuilt rather than saved where
  that is safe.
- Most `economy.*` thresholds and several `upgrades.*` and `construction.*` keys
  become unused. Saved games carry their resolved strategy, so removed keys must
  still parse from existing saves.
- This changes how an existing AI plays. It needs maintainer play-testing and
  approval, not only harness results.
