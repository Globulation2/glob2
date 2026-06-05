# AICortex expand-vs-upgrade: engine mechanics ground truth

Scope: the exact C++ engine facts AICortex needs to decide when to build MORE
training/economy buildings (expand) vs UPGRADE an existing one (which tears the
building down to a higher-level construction site). Every claim leads with a
`file:line` cite. No Rust, no AI-logic changes here — this is a reference.

Ability enum (`src/unit/UnitConsts.h:10-44`): `WALK=3, SWIM=4, FLY=5, BUILD=6,
HARVEST=7, ATTACK_SPEED=8, ATTACK_STRENGTH=9, MAGIC_ATTACK_GROUND=11, ARMOR=15`.
`NB_ABILITY=17`. Unit types: `WORKER=0, EXPLORER=1, WARRIOR=2`, `NB_UNIT_TYPE=3`,
`NB_UNIT_LEVELS=4` (levels 0..3).

---

## 1. Idle-unit upgrade-seeking

**Trigger.** `src/unit/UnitActivity.cpp:43-62`: when a free unit (`medical==MED_FREE`,
`src/unit/UnitActivity.cpp:39`) is idle (`activity==ACT_RANDOM`) AND has been idle
for more than 32 ticks (`jobTimer>32`, `src/unit/UnitActivity.cpp:47`; the 32-tick
delay is explicitly "to allow buildings time to hire units"), it calls
`owner->findBestUpgrade(this)` (`:50`). `jobTimer` is reset to 0 whenever
`activity!=ACT_RANDOM` (`src/unit/UnitActivity.cpp:36-37`). If a building is
returned, the unit sets `activity=ACT_UPGRADING`, attaches, and subscribes itself
inside via `b->subscribeUnitForInside(this)` (`:55-61`). Upgrade-seeking is checked
BEFORE the heal check (`:64-85`), so upgrading takes priority over self-healing for
a free, near-full-HP unit.

**Selection + level gate.** `src/team/TeamRouting.cpp:206-240` `Team::findBestUpgrade`:
- Loops abilities `WALK..ARMOR-1` i.e. indices 3..14 (`:214`). Note the in-code TODO
  warning (`:212-213`) that this loop hard-depends on `WALK` being first and `ARMOR`
  last.
- Skips an ability unless `unit->canLearn[ability]` (`:216`) — see section 2.
- For each candidate building in `owner->upgrade[ability]` (`:221`), the level gate is
  **`b->type->level >= actLevel`** where `actLevel = unit->level[ability]`
  (`:220, :226`). So a unit goes to a building only if the building's level is `>=`
  the unit's CURRENT ability level. A unit already at level L is eligible at a
  level-L building (it would re-train to L+1) and at a level-(L+1) or higher
  building, but NOT at a building below its current level.
- Score = `(warpDistSquare(b,unit) << Q8_FIXED_POINT_SHIFT) / (b->maxUnitInside -
  b->unitsInside.size())` (`:228`); lower is better (`:229`). Distance-squared in the
  numerator, free inside-slots in the denominator — so a closer building with more
  open training slots wins. `[POSSIBLE BUG / quirk]` if `maxUnitInside ==
  unitsInside.size()` this divides by zero; in practice the building is removed from
  `owner->upgrade[ability]` when full (see "concurrency" below), so it should not be
  iterated while saturated, but the predicate is list-membership-dependent, not
  re-checked here.

**Which ability gets trained when a unit reaches the building.**
`src/unit/UnitDisplacement.cpp:333-351`, on `DIS_INSIDE` completion:
- If `type->upgradeInParallel` (barracks/school, see section 2): for EVERY ability
  the unit `canLearn` AND the building `type->upgrade[ability]` is set, it assigns
  `level[ability] = type->level + 1` and refreshes `performance[ability]` from
  `race->getUnitType(typeNum, level[ability])` (`:335-341`). One visit upgrades both
  barracks abilities (speed+strength) or both school abilities (build+harvest) at once.
- Otherwise (racetrack/swimmingpool — single ability), only `destinationPurpose`'s
  ability is upgraded (`:347-349`).

**Concurrency (how many train at one building at once).** The per-building cap on
units training simultaneously is `maxUnitInside` (`src/team/TeamRouting.cpp:228`
scores by `maxUnitInside - unitsInside.size()`). A building registers itself into
`owner->upgrade[ability]` only while `(signed)unitsInside.size() < maxUnitInside`
(`src/building/Construction.cpp:331-339`); once full it is removed from every
`upgrade[]` call list (`:374-379`). `maxUnitWorking` (=1 for all training buildings)
is the construction/haul-worker request, NOT the training capacity — do not conflate
them. `maxUnitInside` is the trainee capacity; `maxUnitWorking` is the builder
capacity while it is a site.

---

## 2. Which building trains which ability, for which UNIT TYPES

**`canLearn` is data-driven per unit-type-at-max-level.** `src/unit/Unit.cpp:60`:
`canLearn[i] = (bool) race->getUnitType(typeNum, 3)->performance[i]` — a unit type
can learn ability `i` iff its LEVEL-3 unit-type has nonzero performance for `i`.

**Default race performance (level-3 rows)** `src/game/entities/Race.cpp:26-131`
(performance array order is `{stopWalk, stopSwim, stopFly, walk, swim, fly, build,
harvest, attackSpeed, attackForce, magicAir, magicGround, magicWood, magicCorn,
magicAlga, armor, hp}`):
- WORKER L3 (`:53-60`): `walk=30, swim=30, build=20, harvest=11`, attack=0, magicGround=0.
  → WORKER canLearn = **WALK, SWIM, BUILD, HARVEST**.
- EXPLORER L3 (`:88-95`): `fly=28, magicGround=8`, walk=0, build=0, harvest=0, attack=0.
  → EXPLORER canLearn = **MAGIC_ATTACK_GROUND** only (plus fly/magic-create, no
  training building exists for those). Explorers do NOT use racetrack, school, or
  barracks.
- WARRIOR L3 (`:123-130`): `walk=30, swim=24, attackSpeed=28, attackForce=16`, build=0,
  harvest=0. → WARRIOR canLearn = **WALK, SWIM, ATTACK_SPEED, ATTACK_STRENGTH**.

**Building `upgrade[]` arrays** (1 = this building trains that ability index):
- Racetrack / WALKSPEED_BUILDING: `upgrade[WALK]=1` (`src/game/entities/BuildingsPartA.cpp:184,207,230`).
  Single-ability (no `upgradeInParallel`).
- Swimmingpool: `upgrade[SWIM]=1` (`src/game/entities/BuildingsPartA.cpp:252,275,298`).
  Single-ability.
- Barracks / ATTACK_BUILDING: `upgrade[ATTACK_SPEED]=1, upgrade[ATTACK_STRENGTH]=1`,
  `upgradeInParallel=1` (`src/game/entities/BuildingsPartB.cpp:27-29,51-53,75-77`).
- School / SCIENCE_BUILDING: `upgrade[BUILD]=1, upgrade[HARVEST]=1`, `upgradeInParallel=1`
  (`src/game/entities/BuildingsPartB.cpp:99-101,122-124`). At LEVEL 2 the school ALSO
  adds `upgrade[MAGIC_ATTACK_GROUND]=1` (`src/game/entities/BuildingsPartB.cpp:146`).

**Cross product (who actually trains where), combining canLearn × upgrade[]:**
- Racetrack (WALK): **workers AND warriors** (both canLearn WALK). Explorers do not.
- Swimmingpool (SWIM): **workers AND warriors**. Explorers do not.
- School L0/L1 (BUILD+HARVEST): **workers only** (only worker canLearn BUILD/HARVEST).
- School L2 (adds MAGIC_ATTACK_GROUND): the magic-ground slot is learnable **only by
  explorers**; workers still take BUILD+HARVEST there. So a level-2 school is visited
  by workers (build/harvest) and explorers (magic-ground), each picking up only the
  abilities they canLearn.
- Barracks (ATTACK_SPEED+ATTACK_STRENGTH): **warriors only**.

Key correction to the prompt's assumptions: warriors DO use the racetrack (and pool)
because warrior L3 has nonzero walk/swim. Only workers use the school's BUILD/HARVEST.
Explorers train nothing except magic-ground at a level-2 school.

---

## 3. Unit-level vs building-level cap

A level-L building trains a unit's ability to exactly **L+1**, by assignment (not
increment): `src/unit/UnitDisplacement.cpp:338` (parallel) and `:347`
(`level[destinationPurpose] = attachedBuilding->type->level + 1`). Because it is an
assignment to `type->level + 1`, reaching unit-ability level 3 requires a level-2
building (2+1=3). A level-0 building maxes a unit at level 1, a level-1 building at
level 2. The selection gate `b->type->level >= unit->level[ability]`
(`src/team/TeamRouting.cpp:226`) additionally means a unit at level L will not bother
visiting a building below level L, so the same building both gates eligibility and
sets the resulting level. Levels 0..3 = `NB_UNIT_LEVELS` (`src/unit/UnitConsts.h:44`),
so 3 is the engine ceiling.

---

## 4. Does UPGRADE take the building fully offline? (yes — entirely)

`Building::launchConstruction` `src/building/Construction.cpp:93-148`. For an ALIVE,
finished, full-HP building with a `nextLevel` (`:95,103-108`) it sets
`constructionResultState=UPGRADE` (`:107`) and then:
- `owner->removeFromAbilitiesLists(this)` (`:110`) — the building is pulled from ALL
  `owner->upgrade[]`, `canFeedUnit`, `canHealUnit`, swarm/turret lists. From this
  instant it trains nobody, feeds nobody, heals nobody, shoots nobody.
- Evicts in-transit `unitsInside` (`:114-132`), then `buildingState =
  WAITING_FOR_CONSTRUCTION`, `maxUnitWorking=0`, `maxUnitInside=0` (`:135-137`),
  re-requests `unitWorking` builders (`:143-145`).

State machine to "back online": `Building::updateConstructionState`
`src/building/Construction.cpp:394-423`: while `WAITING_FOR_CONSTRUCTION`, once all
working+inside units have left (`:408`) it becomes `WAITING_FOR_CONSTRUCTION_ROOM`
and paints a forbidden upgrade zone (`:412-415`); when the larger footprint is clear
it converts to a real building SITE of `nextLevel`. The SITE only completes in
`Building::updateBuildingSite` `src/building/Update.cpp:24-83`: completion is gated on
**`isRessourceFull()`** (`:28`) — the site must be hauled full of the next level's
`maxRessource` (wood/stone, e.g. inn L0→L1 needs 8 wood `BuildingsPartA.cpp:65`;
barracks L0→L1 needs 3 wood + 10 stone `BuildingsPartB.cpp:42`; school L0→L1 needs
5 wood + 5 stone + 12 alga `BuildingsPartB.cpp:113`). Only then does it consume the
resources (`:31-32`), flip `type=nextLevel`, restore `maxUnitInside`/feeding/training,
and re-enter the call lists (`:35-81`).

**So the building is NON-FUNCTIONAL for the entire interval** from `launchConstruction`
until `updateBuildingSite` succeeds. The gap length is NOT a fixed timer — it is gated
serially by: (a) all current trainees/builders leaving, (b) the larger footprint
becoming clear of units/forbidden conflicts (`isHardSpaceForBuildingSite`,
`:401`; failure here reverts via `cancelConstruction(1)` `:406`), and (c) enough
worker round-trips to haul the full next-level resource cost from scattered deposits.
With one builder (`maxUnitWorking=1` on every site variant) and distant stone, this is
many hundreds to a couple thousand ticks — consistent with the observed ~1900-tick
barracks blackout. A single barracks mid-upgrade trains zero warriors that whole time.

---

## 5. Inn (FOOD_BUILDING) feeding capacity by level + training-building inside caps

Inn feeding capacity = `type->maxUnitInside` (trainee/feeding slots) gated by
`ressources[CORN] > unitsInside.size()` (`src/building/Construction.cpp:344`). Per
level (`src/game/entities/BuildingsPartA.cpp`):

| Inn level | maxUnitInside | maxUnitWorking | CORN maxRessource | timeToFeedUnit | cite |
|-----------|---------------|----------------|-------------------|----------------|------|
| 0 (#3)    | 4             | 1              | 10                | 24             | `:55-57,51,53` |
| 1 (#5)    | 7             | 1              | 30                | 15             | `:78-79,74,76` |
| 2 (#7)    | 17            | 1              | 50                | 9              | `:99-104,101` |

While an inn is mid-UPGRADE its feeding capacity is REMOVED entirely:
`launchConstruction` pulls it from `canFeedUnit` via `removeFromAbilitiesLists`
(`src/building/Construction.cpp:110`) and sets `maxUnitInside=0` (`:137`); the
construction-site inn variants (#4 inn1c, #6 inn2c) have no `foodable`/`canFeedUnit`
flag (`src/game/entities/BuildingsPartA.cpp:60-68,84-93`), so they cannot feed.
Upgrading the colony's only/last inn while `feedCapacity` barely covers population
deletes that capacity for the full blackout window (section 4) → starvation. This is
the engine basis for a "build a spare inn first, then upgrade" gate.

Training-building inside caps (units training at once) and worker request, by level:

| Building | level | maxUnitInside | maxUnitWorking | cite |
|----------|-------|---------------|----------------|------|
| Racetrack | 0 (#15) | 2 | 1 | `BuildingsPartA.cpp:187,176` |
| Racetrack | 1 (#17) | 4 | 1 | `BuildingsPartA.cpp:210` |
| Racetrack | 2 (#19) | 6 | 1 | `BuildingsPartA.cpp:233` |
| Swimmingpool | 0 (#21) | 2 | 1 | `BuildingsPartA.cpp:255` |
| Swimmingpool | 1 (#23) | 4 | 1 | `BuildingsPartA.cpp:278` |
| Swimmingpool | 2 (#25) | 6 | 1 | `BuildingsPartA.cpp:296+` |
| Barracks | 0 (#27) | 2 | 1 | `BuildingsPartB.cpp:31` |
| Barracks | 1 (#29) | 4 | 1 | `BuildingsPartB.cpp:55` |
| Barracks | 2 (#31) | 5 | 1 | `BuildingsPartB.cpp:79` |
| School | 0 (#33) | 4 | 1 | `BuildingsPartB.cpp:103` |
| School | 1 (#35) | 7 | 1 | `BuildingsPartB.cpp:126` |
| School | 2 (#37) | 9 | 1 | `BuildingsPartB.cpp:150` |

(`maxUnitWorking` is the builder/repair request while a SITE, always 1 — it is NOT the
trainee throughput. Trainee throughput is `maxUnitInside`.)

---

## 6. What the observation already surfaces (and what is missing)

Source for the level histograms: `src/TeamStat.cpp:231-237` populates
`upgradeState[ability][level]++` and `upgradeStatePerType[type][ability][level]++` for
EVERY ability where `u->performance[ability]` is nonzero (so e.g. a worker contributes
to WALK/BUILD/HARVEST/SWIM buckets; a warrior to WALK/SWIM/ATTACK_SPEED/ATTACK_STRENGTH).
`TeamStat` fields: `src/TeamStat.h:53-54`.

**Already in `CortexObservation`** (`src/ai/cortex/CortexTypes.h`, filled in
`src/ai/cortex/CortexObservation.cpp:255-263`):
- `buildLevel[0..3]` = `upgradeState[BUILD][lvl]` — any unit type, but only workers
  have BUILD performance, so effectively per-worker (`CortexTypes.h:311`).
- `attackSpeedLevel[0..3]` = `upgradeState[ATTACK_SPEED][lvl]` (warriors) (`:312`).
- `attackStrengthLevel[0..3]` = `upgradeState[ATTACK_STRENGTH][lvl]` (warriors) (`:313`).
- `workerSwimLevel[0..3]` = `upgradeStatePerType[WORKER][SWIM][lvl]` (`:314`).
- `explorerMagicGroundLevel[0..3]` = `upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][lvl]` (`:315`).
- `maxBuildLevel` = `team->maxBuildLevel()` (`CortexTypes.h:324`,
  `CortexObservation.cpp:95`) — the engine gate on whether ANY building can be upgraded
  (`gui/GameGUIInput.cpp:426`: upgrade allowed iff `maxBuildLevel > buildingType->level`).
- `upgradableCount[type]` (`CortexTypes.h:332`, `CortexObservation.cpp:236-250`) — per
  building type, count of finished instances passing the full engine Upgradable
  predicate (ALIVE, not a site, hp==hpMax, `constructionResultState==NO_CONSTRUCTION`,
  `nextLevel!=-1`, `maxBuildLevel > type->level`, footprint fits).
- `buildingCountPerLevel[type][longLevel]` + helpers `cortexFinishedBuildings*`,
  `cortexBuildingSites`, `cortexMaxFinishedLevel`, `cortexBuildingsUpgrading`
  (`CortexTypes.h:337,448-488`).
- `feedCapacity` (`CortexTypes.h:292`) — sum of `maxUnitInside` over finished inns;
  `starvingUnits`/`needFood*` food-pressure signals (`:293-296`).
- Per-swarm/inn `TrackedBuilding` arrays with CORN buffer, maxUnitWorking, occupancy,
  nearestWheatDist, priority (`CortexTypes.h:258-269,392-395`).

**MISSING for an expand-vs-upgrade policy** (the observation does NOT carry these):
- **WALK-level distribution** (racetrack gate). No `walkLevel[]` field. Engine source
  to mirror: `TeamStat.upgradeState[WALK][lvl]` (`TeamStat.h:53`) for the any-type
  count, or `upgradeStatePerType[WORKER][WALK][lvl]` and
  `upgradeStatePerType[WARRIOR][WALK][lvl]` (`TeamStat.h:54`) — both workers AND
  warriors train WALK (section 2), so a racetrack gate must consider both types.
- **HARVEST-level distribution** (school's second ability). `buildLevel[]` covers BUILD
  but there is no `harvestLevel[]`. Source: `upgradeState[HARVEST][lvl]`
  (or `upgradeStatePerType[WORKER][HARVEST][lvl]`).
- **Per-unit-TYPE counts for the existing ability slices.** `buildLevel`,
  `attackSpeedLevel`, `attackStrengthLevel` are the any-type `upgradeState[ABILITY]`
  rows, not `upgradeStatePerType`. To compute "% of WORKERS already at the current max
  level" or "% of WARRIORS maxed", mirror
  `upgradeStatePerType[WORKER][BUILD][lvl]`, `[WORKER][HARVEST][lvl]`,
  `[WARRIOR][ATTACK_SPEED][lvl]`, `[WARRIOR][ATTACK_STRENGTH][lvl]`,
  `[WARRIOR][WALK][lvl]`, `[WORKER][WALK][lvl]` (`TeamStat.h:54`). The any-type rows
  are mostly fine today only because each of BUILD/HARVEST (workers) and
  ATTACK_* (warriors) is single-type — but WALK/SWIM are dual-type, so a WALK/SWIM gate
  specifically needs the per-type rows.
- **SWIM per warrior.** Only `workerSwimLevel[]` is surfaced; warrior SWIM
  (`upgradeStatePerType[WARRIOR][SWIM][lvl]`) is not (relevant only if a pool gate ever
  matters for amphibious warriors).
- **Denominator for a "≥X% maxed at current building level" gate.** Per-type unit
  totals exist (`obs.workers/explorers/warriors`, `CortexTypes.h:283-285`), so once the
  per-type/per-ability numerator rows above are mirrored, the percentage is computable
  without new totals.

---

## Design implications for AICortex expand-vs-upgrade

Computable from EXISTING obs fields (no new plumbing):
- "Can I upgrade type T at all right now?" → `upgradableCount[T] > 0` (already encodes
  the `maxBuildLevel > level`, hp==max, not-already-a-site, footprint-fits predicate).
- "Is a type-T upgrade already in flight (don't stack)?" → `cortexBuildingsUpgrading(obs,T)`.
- "Do I have a spare inn so upgrading one won't starve me?" → `feedCapacity` vs
  population, plus `cortexFinishedBuildings(obs, FOOD)`; combined with section-5 fact
  that a mid-upgrade inn contributes 0 capacity for the full ~hundreds-to-2000-tick
  blackout, the gate is "keep `feedCapacity_after_removing_one_inn >= fed_population`".
- "Are my workers' BUILD already maxed at the current school level?" →
  `buildLevel[]` distribution vs `cortexMaxFinishedLevel(obs, SCIENCE)+1`.
- "Are my warriors' attack already maxed at the current barracks level?" →
  `attackSpeedLevel[]`/`attackStrengthLevel[]` vs barracks finished level.

Require NEW obs plumbing (mirror these exact TeamStat sources):
- Racetrack gate (% of WALK-eligible units below max for current racetrack level):
  add `walkLevel[type][lvl]` from `TeamStat.upgradeStatePerType[WORKER][WALK][lvl]` and
  `[WARRIOR][WALK][lvl]` (`TeamStat.h:54`). Both types train WALK.
- School HARVEST gate: add `harvestLevel[lvl]` from
  `TeamStat.upgradeStatePerType[WORKER][HARVEST][lvl]` (`TeamStat.h:54`).
- Generic "≥X% of units of type T are maxed at the current building level" gate: add
  per-type/per-ability slices `upgradeStatePerType[T][ABILITY][lvl]` for the
  (T,ABILITY) pairs the policy gates on — concretely
  `[WORKER][{BUILD,HARVEST,WALK}]`, `[WARRIOR][{ATTACK_SPEED,ATTACK_STRENGTH,WALK}]`
  (all `TeamStat.h:54`). The any-type `upgradeState[ABILITY]` rows already in the obs
  suffice only for the single-type abilities (BUILD/HARVEST/ATTACK_*); WALK/SWIM need
  the per-type rows. Denominators (`obs.workers/warriors`) already exist.

Decision-shape consequence of section 4: because an upgrade takes the building fully
offline for a resource-gated, footprint-gated, serially long window (not a timer), an
upgrade of a SINGLE-instance training/feeding building is a hard capability gap for its
whole duration. Expand-first (build a second instance) before upgrading the last
inn/barracks/school is the engine-justified safe policy; upgrade-in-place is safe only
when a redundant same-type building already covers the role.
