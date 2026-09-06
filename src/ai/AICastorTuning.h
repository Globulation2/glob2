// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

// AICastor tuning constants — Phase 3b deferred per-slice rename pass.
//
// Pure rename: every value here was a raw literal in glob2/src/ai/castor/*.cpp.
// Behavior is unchanged (network checksums and replay output match).
//
// Convention: every constant is prefixed `AI_CASTOR_*` and declared at file
// scope as `static constexpr int` (or `unsigned`/`Uint32` where the
// surrounding code uses that wider type). Comments cite the C++ call site
// the value originated from.
//
// Strategy-table values inside `AICastor::defineStrategy` (GetOrder.cpp:
// 257-347 — `strategy.build[X].*`, `strategy.isFreePart`, `warTimeTrigger`,
// `warAmountTrigger`, `warLevelTrigger`, `strikeWarPowerTriggerUp/Down`,
// `strikeTimeTrigger`, `maxAmountGoal`) are intentionally NOT named here;
// that table is the subject of a separate strategy-struct refactor.
//
// `Building::MAX_COUNT` (=1024) and `NB_UNIT_LEVELS` (=4) already exist in
// `building/Building.h` and `unit/UnitConsts.h` and are used directly at
// the call sites instead of being re-declared here.

// ---------------------------------------------------------------------------
// Tick / time intervals
//
// All AICastor cadences are measured in 40 ms engine ticks
// (GAME_TICKS_PER_SECOND = 25, see EngineTiming.h).
// ---------------------------------------------------------------------------

// Initial / post-upgrade cooldown (~1.3 s) before controlUpgrades fires
// the next upgrade order.
// C++: Lifecycle.cpp:145, Control.cpp:357.
static constexpr int AI_CASTOR_UPGRADE_DELAY_TICKS = 32;

// "Each 10 s" cooldown between controlSwarms invocations.
// C++: GetOrder.cpp:151.
static constexpr int AI_CASTOR_CONTROL_SWARMS_INTERVAL = 256;

// "Each 10 s" cooldown between expandFood invocations.
// C++: GetOrder.cpp:195.
static constexpr int AI_CASTOR_EXPAND_FOOD_INTERVAL = 256;

// "Each 41 s" refresh cadence for the enemy-range and enemy-warriors maps.
// C++: GetOrder.cpp:201, 205.
static constexpr int AI_CASTOR_ENEMY_RANGE_REFRESH = 1024;
static constexpr int AI_CASTOR_ENEMY_WARRIORS_REFRESH = 1024;

// Commented-out enemy-power refresh cadences ("each 5 s" under strike,
// "each 2 min 44 s" idle). The branches at GetOrder.cpp:212/217 are
// dormant; constants are defined for completeness so the future re-enable
// matches the documented intent.
// C++: GetOrder.cpp:212, 217.
static constexpr int AI_CASTOR_ENEMY_POWER_STRIKE_REFRESH = 128;
static constexpr int AI_CASTOR_ENEMY_POWER_IDLE_REFRESH = 4096;

// controlStrikes cadence (~2.6 s).
// C++: Control.cpp:380.
static constexpr int AI_CASTOR_CONTROL_STRIKES_INTERVAL = 64;

// Ignore food-lock stop-units logic until ~82 s (2048 ticks) of game time.
// C++: Control.cpp:63.
static constexpr int AI_CASTOR_FOODLOCK_GRACE_TICKS = 2048;

// "Every 41 s" swim recompute cadence.
// C++: Projects.cpp:101.
static constexpr int AI_CASTOR_NEED_SWIM_REFRESH = 1024;

// Per-project rate limit (~1.3 s) — continueProject early-out gate.
// C++: Projects.cpp:245.
static constexpr int AI_CASTOR_PROJECT_STEP_INTERVAL = 32;

// Stalled-swarm waits when a SWARM project hits a foodLock and we are
// either starving (long backoff) or just locked (short backoff).
// C++: Projects.cpp:251, 253.
static constexpr int AI_CASTOR_SWARM_STARVE_BACKOFF = 8192;   // 5 min 28 s
static constexpr int AI_CASTOR_SWARM_FOODLOCK_BACKOFF = 2048; // 1 min 22 s

// Project abort backoff after exhausting placement tries.
// C++: Projects.cpp:307.
static constexpr int AI_CASTOR_PROJECT_ABORT_BACKOFF = 8192;  // 5 min 27 s

// Wheat-history rotation mask: every 512 ticks (~20.5 s) the
// oldWheatGradient[] ring rotates and re-snapshots map->ressourcesGradient.
// C++: GetOrder.cpp:105.
static constexpr int AI_CASTOR_WHEAT_HISTORY_INTERVAL_MASK = 511;

// Verbose-log mask: every 8192 ticks (~5 min 28 s) computeBuildingSum
// dumps the buildingLevels table when verbose logging is on.
// C++: State.cpp:143.
static constexpr int AI_CASTOR_VERBOSE_LOG_INTERVAL_MASK = 8191;


// ---------------------------------------------------------------------------
// computeBoot — boot-time deferred map computations
//
// `computeBoot` doubles as both a counter and an offset: ticks 0..31 are
// pure idle, ticks 32..48 select one of 17 boot-time map computations
// via `switch(computeBoot - 32)`.
// ---------------------------------------------------------------------------

// Initial idle ticks before the boot compute schedule kicks in.
// C++: GetOrder.cpp:30, 35, 37.
static constexpr int AI_CASTOR_BOOT_IDLE_TICKS = 32;

// Number of one-shot map-compute steps fired during boot.
// C++: GetOrder.cpp:35 (`computeBoot < 17 + 32`), 39-99 (case 0..16).
static constexpr int AI_CASTOR_BOOT_COMPUTE_STEPS = 17;


// ---------------------------------------------------------------------------
// controlSwarms — food / explorer / worker thresholds
// ---------------------------------------------------------------------------

// Food-warning slack offsets (units): foodWarning trips when we have only
// ~half the food production needed; foodLock trips earlier still.
// C++: Control.cpp:47, 48.
static constexpr int AI_CASTOR_FOODWARN_OFFSET = 11;
static constexpr int AI_CASTOR_FOODLOCK_OFFSET = 3;

// foodSurplus margin: too many food buildings if we have this many fewer
// units than food-production capacity.
// C++: Control.cpp:51.
static constexpr int AI_CASTOR_FOODSURPLUS_OFFSET = 4;

// Starving-warning predicate: trip if `(unitSumAll >> 5) + 3` is below the
// number of starving units (i.e. >1/32 of the population is starving plus
// a +3 bias for very small populations).
// C++: Control.cpp:53.
static constexpr int AI_CASTOR_STARVING_RATIO_SHIFT = 5;
static constexpr int AI_CASTOR_STARVING_OFFSET = 3;

// Real-foodLock multipliers: at warriorGoal>1 we tolerate 3x population
// over food, otherwise only 2x, before switching off swarm production.
// C++: Control.cpp:59, 61.
static constexpr int AI_CASTOR_REAL_FOODLOCK_MULT_WAR = 3;
static constexpr int AI_CASTOR_REAL_FOODLOCK_MULT_PEACE = 2;

// Explorer goal thresholds.
// C++: Control.cpp:101-108.
static constexpr int AI_CASTOR_EXPLORER_MIN_WORKERS = 4;     // need 4+ workers before any explorer
static constexpr int AI_CASTOR_EXPLORER_GOAL_HIGH = 2;       // spawn-explorers ratio
static constexpr int AI_CASTOR_EXPLORER_COUNT_TARGET = 3;    // desired-count cap (early)
static constexpr int AI_CASTOR_EXPLORER_RATIO_SHIFT_EARLY = 2; // 1:4 ratio (shift by 2)
static constexpr int AI_CASTOR_DISCOVERY_RATIO_SHIFT = 2;    // <25% map discovered (size << 2)
static constexpr int AI_CASTOR_EXPLORER_RATIO_SHIFT_LATE = 4;  // 1:16 ratio (shift by 4)
static constexpr int AI_CASTOR_EXPLORER_GOAL_LOW = 1;
static constexpr int AI_CASTOR_WORKER_GOAL_LOW = 1;
static constexpr int AI_CASTOR_WORKER_GOAL_HIGH = 4;


// ---------------------------------------------------------------------------
// controlFood — wheat-care thresholds and INN worker assignments
// ---------------------------------------------------------------------------

// Up to 8 retries to find a non-NULL building when the rotation slot lands
// on an empty Building::myBuildings cell.
// C++: Control.cpp:181.
static constexpr int AI_CASTOR_CONTROL_FOOD_RETRIES = 8;

// "Stop workers" wheat-care threshold: care above this means the field is
// being neglected, drop maxUnitWorking to zero.
// C++: Control.cpp:219.
static constexpr int AI_CASTOR_WHEATCARE_STOP_THRESHOLD = 4;

// "Reduce to 1 worker" wheat-care threshold (used between STOP and OK).
// C++: Control.cpp:231.
static constexpr int AI_CASTOR_WHEATCARE_LIMIT_THRESHOLD = 2;

// FOOD_BUILDING worker base counts.
// Note: Control.cpp:249 carries a `//TODO: random 2 or 3` comment — the
// behavior is deterministic at 3 and is preserved verbatim.
// C++: Control.cpp:249, 251.
static constexpr int AI_CASTOR_FOODWARN_INN_SITE_WORKERS = 3; // foodWarning + isBuildingSite
static constexpr int AI_CASTOR_INN_WORKERS_BASE = 1;          // peace-time

// SWARM_BUILDING worker counts under foodWarning vs. normal.
// C++: Control.cpp:261, 263.
static constexpr int AI_CASTOR_SWARM_WORKERS_FOODWARN = 1;
static constexpr int AI_CASTOR_SWARM_WORKERS_NORMAL = 2;


// ---------------------------------------------------------------------------
// controlUpgrades — gating, repair HP ratios, science rules
// ---------------------------------------------------------------------------

// Gates that must all pass before any upgrade fires.
// C++: Control.cpp:297.
static constexpr int AI_CASTOR_UPGRADE_MIN_ABLE_WORKERS = 2;  // numberOfAbleWorkers > 2
static constexpr int AI_CASTOR_UPGRADE_MIN_FREE_WORKERS = 4;  // numberOfFreeWorkers > 4
static constexpr int AI_CASTOR_UPGRADE_ABLE_FREE_RATIO_DIV = 8; // able > free / 8

// Per-class repair-trigger HP ratios: trigger repair when
// `b->hp * AI_CASTOR_REPAIR_HP_RATIO_DIV < b->type->hpMax * <NUM>`.
// (DIV is implicit at 4 — i.e., the comparisons read "less than 25/75/50%".)
// C++: Control.cpp:304, 309, 314.
static constexpr int AI_CASTOR_REPAIR_HP_RATIO_DIV = 4;
static constexpr int AI_CASTOR_REPAIR_HP_RATIO_DEFENCE_NUM = 1; // defencetower: <25%
static constexpr int AI_CASTOR_REPAIR_HP_RATIO_INSIDE_NUM = 3;  // has maxUnitInside: <75%
static constexpr int AI_CASTOR_REPAIR_HP_RATIO_OTHER_NUM = 2;   // others: <50%

// Standard `(unitsWorking, unitsWorkingFinal)` pair sent in
// `OrderConstruction(b->gid, 1, 1)` — used both for repair triggers and
// for the upgrade trigger at the end of controlUpgrades().
// C++: Control.cpp:305, 310, 315, 360.
static constexpr int AI_CASTOR_CONSTRUCTION_ORDER_UNITS = 1;

// Upgrade level goal: ceil(buildsAmount / 2), capped at 3.
// C++: Control.cpp:324, 325, 326.
static constexpr int AI_CASTOR_UPGRADE_LEVEL_FORMULA_BIAS = 1;
static constexpr int AI_CASTOR_UPGRADE_LEVEL_FORMULA_SHIFT = 1;
static constexpr int AI_CASTOR_UPGRADE_LEVEL_MAX = 3;

// SCIENCE_BUILDING: require >=2 same-level science buildings before
// triggering the upgrade.
// C++: Control.cpp:352.
static constexpr int AI_CASTOR_SCIENCE_UPGRADE_MIN_COUNT = 2;


// ---------------------------------------------------------------------------
// controlStrikes — warflag formula, scoring, flag-move thresholds
// ---------------------------------------------------------------------------

// War-flag count formula: `(warriors + 16) / 32` — so we want roughly one
// warflag per 32 warriors, with a +16 bias (round-half).
// C++: Control.cpp:386.
static constexpr int AI_CASTOR_WARFLAG_FORMULA_BIAS = 16;
static constexpr int AI_CASTOR_WARRIORS_PER_WARFLAG = 32;

// Enemy-team scoring: ATTACK and SCIENCE buildings count as 2, others as 1.
// C++: Control.cpp:427, 429.
static constexpr int AI_CASTOR_STRIKE_TEAM_SCORE_HIGH = 2;
static constexpr int AI_CASTOR_STRIKE_TEAM_SCORE_LOW = 1;

// Per-building strike-target score formula:
//   score = (1 + workRange) * (1 + level)
//   if isBuildingSite: score >>= 2  (quarter for unfinished sites)
//   if ATTACK / SCIENCE: score <<= 1 (double for high-value targets)
// C++: Control.cpp:465, 467, 471.
static constexpr int AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS = 1;
static constexpr int AI_CASTOR_STRIKE_BUILDING_SITE_SHIFT = 2;
static constexpr int AI_CASTOR_STRIKE_HIGH_VALUE_SHIFT = 1;

// Min squared distance between an existing warflag and the new target
// before we issue a "move flag" order.
// C++: Control.cpp:506.
static constexpr int AI_CASTOR_FLAG_MOVE_SQ_DIST = 2;

// Desired warriors-on-warflag count.
// C++: Control.cpp:512, 514.
static constexpr int AI_CASTOR_WARFLAG_WORKER_GOAL = 20;


// ---------------------------------------------------------------------------
// enoughFreeWorkers — buildsAmount tier balance
// ---------------------------------------------------------------------------

// Early- and mid-game balance thresholds against `buildsAmount`.
// C++: State.cpp:30, 32.
static constexpr int AI_CASTOR_BUILDS_LOW = 2;
static constexpr int AI_CASTOR_BUILDS_MID = 4;

// Late-game multiplier on excess workers (`partFree << 1`).
// C++: State.cpp:35.
static constexpr int AI_CASTOR_BALANCE_LATE_SHIFT = 1;

// foodLock balance bias: require an extra 3 free workers when we are food-locked.
// C++: State.cpp:37.
static constexpr int AI_CASTOR_FOODLOCK_BALANCE_BIAS = 3;

// "Uninitialised" sentinels for the per-step verbose log statics. -1 is
// never a real warLevel / warPowerSum so the first comparison always
// triggers a log update.
// C++: State.cpp:178, 195.
static constexpr int AI_CASTOR_WAR_LEVEL_UNSET = -1;
static constexpr int AI_CASTOR_WAR_POWER_UNSET = -1;


// ---------------------------------------------------------------------------
// computeNeedSwim — "swim helps" predicate
//
// `(baseCount << 4) > 7 * extendedCount`  i.e. swimming-extended reach
// must improve coverage by more than 16/7 (≈ 43%).
// C++: State.cpp:106.
// ---------------------------------------------------------------------------
static constexpr int AI_CASTOR_SWIM_GAIN_NUMER = 16; // (1 << 4)
static constexpr int AI_CASTOR_SWIM_GAIN_NUMER_SHIFT = 4;
static constexpr int AI_CASTOR_SWIM_GAIN_DENOM = 7;


// ---------------------------------------------------------------------------
// computeWarLevel — trigger-level promotion / cap
// ---------------------------------------------------------------------------

// Grow `warTimeTrigger` by ~1.5x once the previous threshold elapses.
// (`warTimeTrigger += (1 + warTimeTrigger) >> 1`.)
// C++: State.cpp:153.
static constexpr int AI_CASTOR_WARTIME_TRIGGER_GROWTH_BIAS = 1;
static constexpr int AI_CASTOR_WARTIME_TRIGGER_GROWTH_SHIFT = 1;

// Cap on the warTime trigger level (held to <=2).
// C++: State.cpp:156-157.
static constexpr int AI_CASTOR_WARTIME_LEVEL_CAP = 2;

// War-level / war-amount trigger level values.
// C++: State.cpp:163, 164, 166, 171, 173.
static constexpr int AI_CASTOR_WARLEVEL_BUILDINGS_HIGH = 1; // sum > 1 -> level 2
static constexpr int AI_CASTOR_WAR_LEVEL_HIGH = 2;
static constexpr int AI_CASTOR_WAR_LEVEL_MID = 1;

// `strikeWarPowerTriggerUp` growth divisor when we abort a strike.
// C++: State.cpp:209 (`+= strikeWarPowerTriggerUp / 2`).
static constexpr int AI_CASTOR_STRIKE_TRIGGER_GROWTH_DIV = 2;


// ---------------------------------------------------------------------------
// Project boot defaults — Lifecycle.cpp + Projects.cpp boot tiers
// ---------------------------------------------------------------------------

// Project::init defaults.
// C++: Lifecycle.cpp:38, 55, 56.
static constexpr int AI_CASTOR_PROJECT_DEFAULT_AMOUNT = 1;
static constexpr int AI_CASTOR_PROJECT_DEFAULT_PRIORITY = 1;
static constexpr int AI_CASTOR_PROJECT_TRIES_LEFT = 64;

// "Highest priority" critical-project bucket.
// C++: Projects.cpp:63, 83, 109.
static constexpr int AI_CASTOR_PROJECT_PRIORITY_CRITICAL = 0;

// FOOD boot project worker counts.
// C++: Projects.cpp:66, 67, 68, 72.
static constexpr int AI_CASTOR_BOOT_FOOD_MAIN_WORKERS = 3;
static constexpr int AI_CASTOR_BOOT_FOOD_FOOD_WORKERS = 2;
static constexpr int AI_CASTOR_BOOT_OTHER_WORKERS_OFF = 0;
static constexpr int AI_CASTOR_BOOT_FOOD_FINAL_WORKERS = 1;

// SWARM boot project worker counts.
// C++: Projects.cpp:86, 87, 92.
static constexpr int AI_CASTOR_BOOT_SWARM_MAIN_WORKERS = 10;
static constexpr int AI_CASTOR_BOOT_SWARM_FOOD_WORKERS = 1;
static constexpr int AI_CASTOR_BOOT_SWARM_FINAL_WORKERS = 2;

// SWIM and ATTACK boot project amount / mainWorkers.
// C++: Projects.cpp:106, 116.
static constexpr int AI_CASTOR_BOOT_SWIM_AMOUNT = 1;
static constexpr int AI_CASTOR_BOOT_SWIM_MAIN_WORKERS = 2;
static constexpr int AI_CASTOR_BOOT_ATTACK_AMOUNT = 1;
static constexpr int AI_CASTOR_BOOT_ATTACK_MAIN_WORKERS = 2;


// ---------------------------------------------------------------------------
// addProjects — expansion-tier loop
// ---------------------------------------------------------------------------

// `for (int li = 1; li < NB_UNIT_LEVELS; li++)` — upgrade levels start at 1
// (level 0 is the base building, not an upgrade).
// C++: Projects.cpp:178.
static constexpr int AI_CASTOR_FIRST_UPGRADE_LEVEL = 1;

// Encode tier into `buildsAmount` at three sub-phases per outer iteration:
//   buildsAmount = TIER_BASE_PRE   + (agi << SHIFT)  -> 2,4,6
//   buildsAmount = TIER_BASE_MID   + (agi << SHIFT)  -> 3,5,7
//   buildsAmount = TIER_BASE_POST  + (agi << SHIFT)  -> 4,6,8
// Workers are scaled by `(agi - 1)` on top of strategy.build[bi].newWorkers.
// C++: Projects.cpp:195, 213, 219, 232.
static constexpr int AI_CASTOR_BUILDS_TIER_BASE_PRE = 0;
static constexpr int AI_CASTOR_BUILDS_TIER_BASE_MID = 1;
static constexpr int AI_CASTOR_BUILDS_TIER_BASE_POST = 2;
static constexpr int AI_CASTOR_BUILDS_TIER_SHIFT = 1;
static constexpr int AI_CASTOR_TIER_WORKERS_SCALE_BIAS = 1; // workers + (agi - 1)


// ---------------------------------------------------------------------------
// continueProject — sub-phase logic thresholds
// ---------------------------------------------------------------------------

// Low-free-workers threshold: BALANCE_MAIN clamps `mainWorkers` toward 3
// when isFree is at or below this.
// C++: Projects.cpp:350, 352.
static constexpr int AI_CASTOR_FREE_WORKERS_LOW = 3;

// "Have a spare worker" gate that re-enters FIND_PLACE during a
// multipleStart project's BALANCE_MAIN phase.
// C++: Projects.cpp:442.
static constexpr int AI_CASTOR_FREE_WORKERS_SPARE = 1;


// ---------------------------------------------------------------------------
// findGoodBuilding — placement scoring (Placement.cpp:22-180)
// ---------------------------------------------------------------------------

// Initial floor for the "best work score" scan: any cell with workAbility
// above 2 wins this seed.
// C++: Placement.cpp:38.
static constexpr int AI_CASTOR_BEST_WORK_SCORE_FLOOR = 2;

// `minWork = bestWorkScore * 2`, then clamped to either 15*4 (critical) or
// 30*4 (normal). The trailing `* 4` is "4 corners per building footprint".
// C++: Placement.cpp:47, 50, 51, 55.
static constexpr int AI_CASTOR_MINWORK_MULT = 2;
static constexpr int AI_CASTOR_MINWORK_CRITICAL_CAP_PER_CORNER = 15;
static constexpr int AI_CASTOR_MINWORK_NORMAL_CAP_PER_CORNER = 30;
static constexpr int AI_CASTOR_CORNERS = 4;

// Wheat-gradient comparisons sample the four corners of the candidate
// footprint, so the limit is `(255 - <offset>) * AI_CASTOR_CORNERS`. Larger
// offsets are more permissive (lower limit means more cells qualify).
// FOOD buildings want HIGH wheat (wheatGradient must be >= limit) so a
// SMALL offset is the strict variant ("critical" path picks 16, normal 4).
// NON-FOOD buildings want LOW wheat (wheatGradient must be < limit) so a
// SMALL offset is the strict variant ("critical" picks 5, normal 7).
// C++: Placement.cpp:64, 66, 71, 73.
static constexpr int AI_CASTOR_WHEAT_GRADIENT_PEAK = 255;
static constexpr int AI_CASTOR_WHEAT_GRADIENT_CRITICAL_FOOD_OFFSET = 16;
static constexpr int AI_CASTOR_WHEAT_GRADIENT_NORMAL_FOOD_OFFSET = 4;
static constexpr int AI_CASTOR_WHEAT_GRADIENT_CRITICAL_OTHER_OFFSET = 5;
static constexpr int AI_CASTOR_WHEAT_GRADIENT_NORMAL_OTHER_OFFSET = 7;

// "Too close to enemy" reject threshold:
//   enemyRange (sum over 4 corners) > AI_CASTOR_CORNERS * (255 - <offset>).
// C++: Placement.cpp:133.
static constexpr int AI_CASTOR_ENEMY_RANGE_REJECT_OFFSET = 8;

// Bit packing used by `buildingNeighbourMap`:
//   bit 0       : "dirty" flag (this cell is too close to a neighbour)
//   bits [1..3] : direct-neighbours count (mask 7, shift 1)
//   bit 4       : zero / centre-flag bit (cleared via `& ~16`)
//   bits [5..7] : far-neighbours count (mask 7, shift 5)
// C++: Placement.cpp:140-142, Maps.cpp:172, 198, 202, 221.
static constexpr int AI_CASTOR_NEIGHBOUR_DIRECT_SHIFT = 1;
static constexpr int AI_CASTOR_NEIGHBOUR_FAR_SHIFT = 5;
static constexpr int AI_CASTOR_NEIGHBOUR_MASK = 7;
static constexpr int AI_CASTOR_NEIGHBOUR_DIRTY_BIT = 1;
static constexpr int AI_CASTOR_NEIGHBOUR_MAX_DIRECT = 1;
static constexpr int AI_CASTOR_NEIGHBOUR_DIRECT_INCR = 2;
static constexpr int AI_CASTOR_NEIGHBOUR_CENTRE_BIT = 16;
static constexpr int AI_CASTOR_NEIGHBOUR_FAR_INCR = 32;
static constexpr int AI_CASTOR_NEIGHBOUR_OUT_OF_VISION = 127;

// Score formula coefficients (Placement.cpp:149/151/153). Each formula is:
//   defense: ((work<<1) + wheatGradient + (enemyRange<<4)) * (16 + (direct<<2) + far)
//   food   : ((wheatGrowth<<8) + work + (wheatGradient>>1) - enemyRange)
//            * (8 + (direct<<2) + far)
//   normal : (4096 + work - (wheatGrowth<<8) - enemyRange)
//            * (8 + (direct<<2) + far)
// C++: Placement.cpp:149, 151, 153.
static constexpr int AI_CASTOR_SCORE_DEFENSE_WORK_SHIFT = 1;
static constexpr int AI_CASTOR_SCORE_DEFENSE_ENEMY_SHIFT = 4;
static constexpr int AI_CASTOR_SCORE_DEFENSE_NEIGHBOUR_BIAS = 16;
static constexpr int AI_CASTOR_SCORE_FOOD_GROWTH_SHIFT = 8;
static constexpr int AI_CASTOR_SCORE_FOOD_GRADIENT_SHIFT = 1;
static constexpr int AI_CASTOR_SCORE_FOOD_NEIGHBOUR_BIAS = 8;
static constexpr int AI_CASTOR_SCORE_NORMAL_BIAS = 4096;
static constexpr int AI_CASTOR_SCORE_NORMAL_GROWTH_SHIFT = 8;
static constexpr int AI_CASTOR_SCORE_NORMAL_NEIGHBOUR_BIAS = 8;
static constexpr int AI_CASTOR_SCORE_NEIGHBOUR_DIRECT_SHIFT = 2; // (direct << 2)


// ---------------------------------------------------------------------------
// computeRessourcesCluster
// ---------------------------------------------------------------------------

// Cluster id space = Uint16::MAX + 1.
// C++: Placement.cpp:195, 196.
static constexpr int AI_CASTOR_CLUSTER_ID_SPACE = 65536;

// Starting cluster id (id 0 means "unset").
// C++: Placement.cpp:218.
static constexpr int AI_CASTOR_CLUSTER_FIRST_ID = 1;


// ---------------------------------------------------------------------------
// updateGlobalGradient(NoObstacle) — propagation sentinels
// ---------------------------------------------------------------------------

// Sentinel value an obstacle cell holds in the no-obstacle gradient; the
// propagation loops skip cells equal to this. Also the "not grass" marker
// stamped into notGrassMap by computeNotGrassMap (Maps.cpp:519, 520).
// C++: Placement.cpp:271, 302, 334, 363; Maps.cpp:520.
static constexpr int AI_CASTOR_GRADIENT_OBSTACLE_NO_OBSTACLE = 16;

// "Wall" sentinel for the standard updateGlobalGradient propagation: a
// cell already at this value is a fixed source, propagation stops.
// Also seeded into enemyRangeMap by computeEnemyRangeMap (Maps.cpp:711)
// as the "enemy here" gradient source — `GRADIENT_AT_GOAL` (defined in
// MapInternal.h) is the canonical name and is used at the call site.
// C++: Placement.cpp:404, 435, 467, 496.
static constexpr int AI_CASTOR_GRADIENT_WALL = 255;


// ---------------------------------------------------------------------------
// Maps.cpp — terrain ranges, gradient stamps, hydration / wheat
// ---------------------------------------------------------------------------

// Terrain ID layout (matches the engine's tile-set encoding):
//   [0,    16)  : grass (16 tiles)
//   [256,  272) : water (16 tiles)
//   [256,  272) : sand  (16 tiles, same range as water in this code)
// `Maps.cpp:42` uses `>=256 && <256+16` for "is water"; `Maps.cpp:469` uses
// the identical range for "is sand"; `Maps.cpp:67` uses `>=16` and
// `Maps.cpp:519` uses `>16` for "is not grass".  See bug M6 — the >=16
// vs >16 asymmetry in `notGrassMap` vs `obstacleBuildingMap` is preserved
// verbatim by this rename pass (do NOT change the operator).
// C++: Maps.cpp:42, 67, 469, 519.
static constexpr int AI_CASTOR_TERRAIN_GRASS_COUNT = 16;
static constexpr int AI_CASTOR_TERRAIN_WATER_FIRST = 256;
static constexpr int AI_CASTOR_TERRAIN_WATER_COUNT = 16;
static constexpr int AI_CASTOR_TERRAIN_SAND_FIRST = 256;
static constexpr int AI_CASTOR_TERRAIN_SAND_COUNT = 16;

// computeWorkPowerMap: max gradient radius and the "/2" half-map cap.
// C++: Maps.cpp:324, 325, 326.
static constexpr int AI_CASTOR_WORK_POWER_MAX_RANGE = 64;
static constexpr int AI_CASTOR_HALFMAP_DIV = 2;

// `(u->hungry - u->trigHungry) >> 1` divides remaining hunger by 2 when
// computing per-worker gradient range.
// C++: Maps.cpp:338, 413.
static constexpr int AI_CASTOR_HUNGER_RANGE_SHIFT = 1;

// Power-stamping reducer (>>3, i.e. /8) applied to `range` before adding
// to the per-cell gradient — also called `reducer` in the source.
// C++: Maps.cpp:346, 627.
static constexpr int AI_CASTOR_POWER_STAMP_REDUCER = 3;

// computeWorkAbilityMap: normalise `(workPower * workRange)` by 32.
// C++: Maps.cpp:443.
static constexpr int AI_CASTOR_WORK_ABILITY_NORM_SHIFT = 5;

// Uint8 clamp value, used at half a dozen sites that clamp Uint16 sums or
// `range` counters into the [0, 255] Uint8 range.
// C++: Maps.cpp:107, 351, 362, 373, 385, 416, 443, 496, etc.
static constexpr int AI_CASTOR_UINT8_MAX_VALUE = 255;

// computeHydratationMap: stamp radius and the >>4 divide on the final
// per-cell accumulator.
// C++: Maps.cpp:464, 496.
static constexpr int AI_CASTOR_HYDRATATION_RANGE = 16;
static constexpr int AI_CASTOR_HYDRATATION_NORM_SHIFT = 4;

// computeWheatCareMap: per-cell predicates (notGrass exact-15 sentinel,
// previous-care threshold, and the two "high"/"low" care values written
// into wheatCareMap[0]).
// C++: Maps.cpp:546, 547, 550, 552.
static constexpr int AI_CASTOR_NOTGRASS_NEIGHBOUR_VAL = 15;
static constexpr int AI_CASTOR_WHEATCARE_PREV_HIGH_THRESHOLD = 7;
static constexpr int AI_CASTOR_WHEATCARE_HIGH = 10;
static constexpr int AI_CASTOR_WHEATCARE_LOW = 8;
// Wheat-gradient sentinel comparisons in the same predicate
// (`==255`, `<255`, `<254`).
// C++: Maps.cpp:548, 550.
static constexpr int AI_CASTOR_WHEAT_GRADIENT_NEAR_PEAK = 254;

// computeWheatGrowthMap: base growth offset + hydratation divisor;
// minimum-growth floor.
// C++: Maps.cpp:576, 583, 589.
static constexpr int AI_CASTOR_WHEAT_GROWTH_BASE = 1;
static constexpr int AI_CASTOR_WHEAT_GROWTH_HYDRATATION_SHIFT = 3;
static constexpr int AI_CASTOR_WHEAT_CARE_SUBTRACT_THRESHOLD = 1;
static constexpr int AI_CASTOR_WHEAT_GROWTH_MIN = 1;

// computeEnemyPowerMap stamp radius (max 32 per the inline comment).
// C++: Maps.cpp:628.
static constexpr int AI_CASTOR_ENEMY_POWER_RANGE = 32;

// computeEnemyWarriorsMap: `gradient[i] = 32` seeds the warrior gradient
// so it propagates only ~32 cells (vs. enemyRangeMap which uses
// `GRADIENT_AT_GOAL` = 255 to propagate the whole map).
// C++: Maps.cpp:746.
static constexpr int AI_CASTOR_ENEMY_WARRIOR_GRADIENT_SEED = 32;

// `guid >> 10` extracts the team number from a ground-unit gid
// (gid = team * Unit::MAX_COUNT + index, with Unit::MAX_COUNT = 1024).
// C++: Maps.cpp:743.
static constexpr int AI_CASTOR_GUID_TEAM_SHIFT = 10;
