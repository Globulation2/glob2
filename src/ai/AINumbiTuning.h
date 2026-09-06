// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
//
// AINumbi tuning constants. Behavior-preserving names for previously-bare
// literals scattered across AINumbi.h / AINumbi.cpp. Each value here is the
// exact value that was hardcoded in the original source — nothing is
// reinterpreted, recomputed, or unified across phases. Cross-phase numeric
// coincidences (e.g. several phases passing the same FOOD adjustBuildings
// args) are preserved as separate per-phase names so a tuning change to one
// phase never silently moves another.
//
// All values are `static constexpr int` at file scope. The file has no
// runtime side effects and may be included from anywhere.

#pragma once

// ----------------------------------------------------------------------------
// Phase / attack timer defaults (init values)
// ----------------------------------------------------------------------------

// Initial ticks per AI strategy phase (timer wraps and increments `phase`).
static constexpr int AI_NUMBI_PHASE_TIME_DEFAULT_TICKS = 1024;
// Initial warrior count required before launching an attack.
static constexpr int AI_NUMBI_CRITICAL_WARRIORS_DEFAULT = 20;
// Initial timeout (ticks) before an attack is forced regardless of warrior count.
static constexpr int AI_NUMBI_CRITICAL_TIME_DEFAULT_TICKS = 1024;

// Legacy save-format compatibility: the header `mainBuilding[]` array is
// hardcoded to 15 because IntBuildingType::NB_BUILDING was 15 in older save
// versions. Today NB_BUILDING is smaller, so 15 is the upper-bound "legacy"
// dimension. [POSSIBLE BUG M7] — preserved verbatim.
static constexpr int AI_NUMBI_LEGACY_NB_BUILDING = 15;

// Round-robin slot mask: getOrder() runs one of up to 32 sub-decisions per
// tick by picking `timer & 0x1F` as the slot index.
static constexpr int AI_NUMBI_DECISION_SLOT_MASK = 0x1F;

// ----------------------------------------------------------------------------
// Phase tier boundaries (compared against `phase`)
// ----------------------------------------------------------------------------

static constexpr int AI_NUMBI_MID_GAME_PHASE = 4;     // phase < 4
static constexpr int AI_NUMBI_LATE_MID_PHASE = 6;     // phase < 6
static constexpr int AI_NUMBI_SCIENCE_PHASE  = 8;     // phase < 8
static constexpr int AI_NUMBI_DEFEND_PHASE   = 10;    // phase < 10

// ----------------------------------------------------------------------------
// Per-phase swarmsForWorkers tuples
// (minSwarmNumbers, nbWorkersFator, workers, explorers, warriors)
// ----------------------------------------------------------------------------

// phase 0: rush food
static constexpr int AI_NUMBI_PHASE0_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE0_SWARM_FACTOR   = 4;
static constexpr int AI_NUMBI_PHASE0_SWARM_WORKERS  = 7;
static constexpr int AI_NUMBI_PHASE0_SWARM_EXPLORER = 1;
static constexpr int AI_NUMBI_PHASE0_SWARM_WARRIOR  = 0;

// phase 1: rush food (more workers)
static constexpr int AI_NUMBI_PHASE1_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE1_SWARM_FACTOR   = 5;
static constexpr int AI_NUMBI_PHASE1_SWARM_WORKERS  = 14;
static constexpr int AI_NUMBI_PHASE1_SWARM_EXPLORER = 0;
static constexpr int AI_NUMBI_PHASE1_SWARM_WARRIOR  = 0;

// phase 2-3: produce units, improve health/science
static constexpr int AI_NUMBI_PHASE2_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE2_SWARM_FACTOR   = 9;
static constexpr int AI_NUMBI_PHASE2_SWARM_WORKERS  = 14;
static constexpr int AI_NUMBI_PHASE2_SWARM_EXPLORER = 0;
static constexpr int AI_NUMBI_PHASE2_SWARM_WARRIOR  = 0;

// phase 4-5
static constexpr int AI_NUMBI_PHASE4_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE4_SWARM_FACTOR   = 9;
static constexpr int AI_NUMBI_PHASE4_SWARM_WORKERS  = 14;
static constexpr int AI_NUMBI_PHASE4_SWARM_EXPLORER = 1;
static constexpr int AI_NUMBI_PHASE4_SWARM_WARRIOR  = 0;

// phase 6-7: improve science
static constexpr int AI_NUMBI_PHASE6_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE6_SWARM_FACTOR   = 4;
static constexpr int AI_NUMBI_PHASE6_SWARM_WORKERS  = 14;
static constexpr int AI_NUMBI_PHASE6_SWARM_EXPLORER = 0;
static constexpr int AI_NUMBI_PHASE6_SWARM_WARRIOR  = 0;

// phase 8-9: produce good units, defend
static constexpr int AI_NUMBI_PHASE8_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE8_SWARM_FACTOR   = 9;
static constexpr int AI_NUMBI_PHASE8_SWARM_WORKERS  = 14;
static constexpr int AI_NUMBI_PHASE8_SWARM_EXPLORER = 1;
static constexpr int AI_NUMBI_PHASE8_SWARM_WARRIOR  = 1;

// phase 10+: produce warriors
static constexpr int AI_NUMBI_PHASE10_SWARM_MIN      = 1;
static constexpr int AI_NUMBI_PHASE10_SWARM_FACTOR   = 10;
static constexpr int AI_NUMBI_PHASE10_SWARM_WORKERS  = 3;
static constexpr int AI_NUMBI_PHASE10_SWARM_EXPLORER = 1;
static constexpr int AI_NUMBI_PHASE10_SWARM_WARRIOR  = 14;

// ----------------------------------------------------------------------------
// Per-phase adjustBuildings tuples (numbers, numbersInc, workers)
// Each phase has its own tuple even when the values match, so a future tuning
// change to one phase doesn't silently move another.
// ----------------------------------------------------------------------------

// phase 0
static constexpr int AI_NUMBI_PHASE0_INN_NUMBERS      = 4;
static constexpr int AI_NUMBI_PHASE0_INN_NUMBERS_INC  = 1;
static constexpr int AI_NUMBI_PHASE0_INN_WORKERS      = 3;

// phase 1
static constexpr int AI_NUMBI_PHASE1_INN_NUMBERS      = 4;
static constexpr int AI_NUMBI_PHASE1_INN_NUMBERS_INC  = 1;
static constexpr int AI_NUMBI_PHASE1_INN_WORKERS      = 3;

// phase 2-3
static constexpr int AI_NUMBI_PHASE2_INN_NUMBERS          = 4;
static constexpr int AI_NUMBI_PHASE2_INN_NUMBERS_INC      = 1;
static constexpr int AI_NUMBI_PHASE2_INN_WORKERS          = 1;
static constexpr int AI_NUMBI_PHASE2_HEAL_NUMBERS         = 44;
static constexpr int AI_NUMBI_PHASE2_HEAL_NUMBERS_INC     = 1;
static constexpr int AI_NUMBI_PHASE2_HEAL_WORKERS         = 1;
static constexpr int AI_NUMBI_PHASE2_SCIENCE_NUMBERS      = 40;
static constexpr int AI_NUMBI_PHASE2_SCIENCE_NUMBERS_INC  = 1;
static constexpr int AI_NUMBI_PHASE2_SCIENCE_WORKERS      = 2;
static constexpr int AI_NUMBI_PHASE2_RACETRACK_NUMBERS     = 70;
static constexpr int AI_NUMBI_PHASE2_RACETRACK_NUMBERS_INC = 1;
static constexpr int AI_NUMBI_PHASE2_RACETRACK_WORKERS     = 0;
static constexpr int AI_NUMBI_PHASE2_BARRACKS_NUMBERS      = 70;
static constexpr int AI_NUMBI_PHASE2_BARRACKS_NUMBERS_INC  = 1;
static constexpr int AI_NUMBI_PHASE2_BARRACKS_WORKERS      = 0;
static constexpr int AI_NUMBI_PHASE2_DEFENSE_NUMBERS       = 25;
static constexpr int AI_NUMBI_PHASE2_DEFENSE_NUMBERS_INC   = 1;
static constexpr int AI_NUMBI_PHASE2_DEFENSE_WORKERS       = 1;

// phase 4-5
static constexpr int AI_NUMBI_PHASE4_INN_NUMBERS         = 5;
static constexpr int AI_NUMBI_PHASE4_INN_NUMBERS_INC     = 1;
static constexpr int AI_NUMBI_PHASE4_INN_WORKERS         = 1;
static constexpr int AI_NUMBI_PHASE4_HEAL_NUMBERS        = 37;
static constexpr int AI_NUMBI_PHASE4_HEAL_NUMBERS_INC    = 1;
static constexpr int AI_NUMBI_PHASE4_HEAL_WORKERS        = 1;
static constexpr int AI_NUMBI_PHASE4_SCIENCE_NUMBERS     = 32;
static constexpr int AI_NUMBI_PHASE4_SCIENCE_NUMBERS_INC = 1;
static constexpr int AI_NUMBI_PHASE4_SCIENCE_WORKERS     = 2;
static constexpr int AI_NUMBI_PHASE4_DEFENSE_NUMBERS     = 25;
static constexpr int AI_NUMBI_PHASE4_DEFENSE_NUMBERS_INC = 1;
static constexpr int AI_NUMBI_PHASE4_DEFENSE_WORKERS     = 1;
// mayUpgrade(ptrigger=16, ntrigger=8)
static constexpr int AI_NUMBI_PHASE4_UPGRADE_PTRIGGER = 16;
static constexpr int AI_NUMBI_PHASE4_UPGRADE_NTRIGGER = 8;

// phase 6-7
static constexpr int AI_NUMBI_PHASE6_INN_NUMBERS         = 5;
static constexpr int AI_NUMBI_PHASE6_INN_NUMBERS_INC     = 1;
static constexpr int AI_NUMBI_PHASE6_INN_WORKERS         = 1;
static constexpr int AI_NUMBI_PHASE6_HEAL_NUMBERS        = 34;
static constexpr int AI_NUMBI_PHASE6_HEAL_NUMBERS_INC    = 2;
static constexpr int AI_NUMBI_PHASE6_HEAL_WORKERS        = 1;
static constexpr int AI_NUMBI_PHASE6_SCIENCE_NUMBERS     = 32;
static constexpr int AI_NUMBI_PHASE6_SCIENCE_NUMBERS_INC = 2;
static constexpr int AI_NUMBI_PHASE6_SCIENCE_WORKERS     = 4;
// mayUpgrade(ptrigger=16, ntrigger=4)
static constexpr int AI_NUMBI_PHASE6_UPGRADE_PTRIGGER = 16;
static constexpr int AI_NUMBI_PHASE6_UPGRADE_NTRIGGER = 4;

// phase 8-9
static constexpr int AI_NUMBI_PHASE8_INN_NUMBERS           = 5;
static constexpr int AI_NUMBI_PHASE8_INN_NUMBERS_INC       = 1;
static constexpr int AI_NUMBI_PHASE8_INN_WORKERS           = 1;
static constexpr int AI_NUMBI_PHASE8_HEAL_NUMBERS          = 32;
static constexpr int AI_NUMBI_PHASE8_HEAL_NUMBERS_INC      = 2;
static constexpr int AI_NUMBI_PHASE8_HEAL_WORKERS          = 1;
static constexpr int AI_NUMBI_PHASE8_SCIENCE_NUMBERS       = 40;
static constexpr int AI_NUMBI_PHASE8_SCIENCE_NUMBERS_INC   = 2;
static constexpr int AI_NUMBI_PHASE8_SCIENCE_WORKERS       = 3;
static constexpr int AI_NUMBI_PHASE8_RACETRACK_NUMBERS     = 70;
static constexpr int AI_NUMBI_PHASE8_RACETRACK_NUMBERS_INC = 1;
static constexpr int AI_NUMBI_PHASE8_RACETRACK_WORKERS     = 5;
static constexpr int AI_NUMBI_PHASE8_DEFENSE_NUMBERS       = 20;
static constexpr int AI_NUMBI_PHASE8_DEFENSE_NUMBERS_INC   = 1;
static constexpr int AI_NUMBI_PHASE8_DEFENSE_WORKERS       = 1;
static constexpr int AI_NUMBI_PHASE8_BARRACKS_NUMBERS      = 70;
static constexpr int AI_NUMBI_PHASE8_BARRACKS_NUMBERS_INC  = 1;
static constexpr int AI_NUMBI_PHASE8_BARRACKS_WORKERS      = 3;
// checkoutExpands(numbers=80, workers=5)
static constexpr int AI_NUMBI_PHASE8_EXPAND_NUMBERS = 80;
static constexpr int AI_NUMBI_PHASE8_EXPAND_WORKERS = 5;
// mayUpgrade(ptrigger=16, ntrigger=4)
static constexpr int AI_NUMBI_PHASE8_UPGRADE_PTRIGGER = 16;
static constexpr int AI_NUMBI_PHASE8_UPGRADE_NTRIGGER = 4;

// phase 10+
static constexpr int AI_NUMBI_PHASE10_INN_NUMBERS           = 6;
static constexpr int AI_NUMBI_PHASE10_INN_NUMBERS_INC       = 2;
static constexpr int AI_NUMBI_PHASE10_INN_WORKERS           = 1;
static constexpr int AI_NUMBI_PHASE10_HEAL_NUMBERS          = 37;
static constexpr int AI_NUMBI_PHASE10_HEAL_NUMBERS_INC      = 2;
static constexpr int AI_NUMBI_PHASE10_HEAL_WORKERS          = 1;
static constexpr int AI_NUMBI_PHASE10_SCIENCE_NUMBERS       = 38;
static constexpr int AI_NUMBI_PHASE10_SCIENCE_NUMBERS_INC   = 2;
static constexpr int AI_NUMBI_PHASE10_SCIENCE_WORKERS       = 2;
static constexpr int AI_NUMBI_PHASE10_RACETRACK_NUMBERS     = 70;
static constexpr int AI_NUMBI_PHASE10_RACETRACK_NUMBERS_INC = 2;
static constexpr int AI_NUMBI_PHASE10_RACETRACK_WORKERS     = 5;
static constexpr int AI_NUMBI_PHASE10_DEFENSE_NUMBERS       = 20;
static constexpr int AI_NUMBI_PHASE10_DEFENSE_NUMBERS_INC   = 2;
static constexpr int AI_NUMBI_PHASE10_DEFENSE_WORKERS       = 2;
static constexpr int AI_NUMBI_PHASE10_BARRACKS_NUMBERS      = 70;
static constexpr int AI_NUMBI_PHASE10_BARRACKS_NUMBERS_INC  = 2;
static constexpr int AI_NUMBI_PHASE10_BARRACKS_WORKERS      = 3;
// mayAttack numberRequested arg (warriors per war flag).
static constexpr int AI_NUMBI_WAR_FLAG_UNITS = 10;
// checkoutExpands(numbers=40, workers=5)
static constexpr int AI_NUMBI_PHASE10_EXPAND_NUMBERS = 40;
static constexpr int AI_NUMBI_PHASE10_EXPAND_WORKERS = 5;
// mayUpgrade(ptrigger=16, ntrigger=4)
static constexpr int AI_NUMBI_PHASE10_UPGRADE_PTRIGGER = 16;
static constexpr int AI_NUMBI_PHASE10_UPGRADE_NTRIGGER = 4;

// ----------------------------------------------------------------------------
// Corn scan (estimateFood)
// ----------------------------------------------------------------------------

// Tolerated gap-cells when scanning a CORN patch row/column for size.
static constexpr int AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE = 2;
// Maximum scan radius along each direction when measuring a CORN patch.
static constexpr int AI_NUMBI_CORN_SCAN_MAX_RADIUS = 32;

// ----------------------------------------------------------------------------
// Food-per-unit thresholds (swarmsForWorkers)
// ----------------------------------------------------------------------------

// If estimated food < nbu*LOW - 1, the swarm is starved → numberRequested=0.
static constexpr int AI_NUMBI_LOW_FOOD_PER_UNIT  = 3;
// If estimated food < nbu*HIGH + 1 (and currently 0 workers), keep at 0.
static constexpr int AI_NUMBI_HIGH_FOOD_PER_UNIT = 5;

// ----------------------------------------------------------------------------
// nextMainBuilding: building-index round-robin mask
// ----------------------------------------------------------------------------

// Mask applied to (i+id) when scanning team->myBuildings[]. [POSSIBLE BUG H1]:
// the mask is 0xFF (256) but Building::MAX_COUNT is 1024; preserved verbatim.
static constexpr int AI_NUMBI_BUILDING_INDEX_MASK = 0xFF;

// ----------------------------------------------------------------------------
// nbFreeAround: placement-score knobs
// ----------------------------------------------------------------------------

// Initial score before subtracting penalties.
static constexpr int AI_NUMBI_PLACEMENT_SCORE_INIT = 256 + 96;
// Outer-margin scan radius range (inclusive).
static constexpr int AI_NUMBI_OUTER_MARGIN_R_MIN = 2;
static constexpr int AI_NUMBI_OUTER_MARGIN_R_MAX = 3;
// Outer-edge penalty base (multiplied by (r-2)*4 + this base).
static constexpr int AI_NUMBI_OUTER_EDGE_PENALTY = 4;
// Inner-edge penalty (single-tile margin block).
static constexpr int AI_NUMBI_INNER_EDGE_PENALTY = 12;
// Free-region scan range (max ring distance to look for clear space).
static constexpr int AI_NUMBI_FREE_REGION_SCAN_RANGE = 8;

// ----------------------------------------------------------------------------
// findNewEmplacement: scan + scoring + corn proximity
// ----------------------------------------------------------------------------

// Minimum acceptable placement score (used for both pre-check and per-cell test).
static constexpr int AI_NUMBI_PLACEMENT_SCORE_MIN = 299;

// Search radius for swarm placement. [POSSIBLE BUG L9]: `maxr` is computed
// from this but never read — the spiral scan below uses a literal iteration
// count instead. Preserve both values; do not "fix".
static constexpr int AI_NUMBI_SWARM_SEARCH_RADIUS    = 64;
// Search radius for non-swarm placement. (Same dead-computation note.)
static constexpr int AI_NUMBI_NONSWARM_SEARCH_RADIUS = 16;

// Padding around a swarm building when looking for a placement.
static constexpr int AI_NUMBI_SWARM_MARGIN = 2;

// Square-spiral scan iteration count. NOT derived from the search-radius
// constants above; used as a literal in the original code.
static constexpr int AI_NUMBI_SCAN_ITERATIONS = 4096;

// Distance bias added to width*height when checking corn proximity.
static constexpr int AI_NUMBI_CORN_DISTANCE_BIAS = 64;
// Building-type cutoff for "must be near corn" heuristic.
// (FOOD_BUILDING and SWARM_BUILDING short-type-num indices fall in [0..1].)
static constexpr int AI_NUMBI_NEAR_CORN_TYPE_CUTOFF = 1;

// ----------------------------------------------------------------------------
// mayAttack
// ----------------------------------------------------------------------------

// Stop-attack threshold divisor: `ft <= critticalMass / DIVISOR` ends attack.
static constexpr int AI_NUMBI_STOP_ATTACK_DIVISOR = 2;
// 1-in-32 chance per enemy-building scan to drop a war flag.
static constexpr int AI_NUMBI_ENEMY_FLAG_CHANCE_MASK = 0x1F;
// Maximum simultaneous war flags during an attack.
static constexpr int AI_NUMBI_MAX_WAR_FLAGS = 5;
// OrderCreate war-flag init: unitsInside / unitsWorking flags (=1, =1).
static constexpr int AI_NUMBI_WAR_FLAG_INIT_UNITS_WORKING = 1;
static constexpr int AI_NUMBI_WAR_FLAG_INIT_FLAG_RADIUS   = 1;
// Exponential backoff multiplier applied to critticalWarriors and critticalTime
// after a stop-attack.
static constexpr int AI_NUMBI_ATTACK_BACKOFF_MULTIPLIER = 2;

// ----------------------------------------------------------------------------
// adjustBuildings demand multipliers
// ----------------------------------------------------------------------------

// Hungry-unit pressure multiplier on inn count.
static constexpr int AI_NUMBI_HUNGRY_INN_DEMAND_MULT = 2;
// Damaged-unit pressure multiplier on hospital count.
static constexpr int AI_NUMBI_DAMAGED_HEAL_DEMAND_MULT = 4;

// ----------------------------------------------------------------------------
// mayUpgrade tuning
// ----------------------------------------------------------------------------

// School-count weighting in the "upgrade potential" formula:
//   potential = wun[L+1..3] + WEIGHT * sum(numberScience[L..3])
static constexpr int AI_NUMBI_SCHOOL_POTENTIAL_WEIGHT = 4;
// Allow one extra upgrading school slot before throttling (rounding tolerance).
static constexpr int AI_NUMBI_SCIENCE_UPGRADE_TOLERANCE = 1;
// OrderConstruction(b->gid, level=1, repair=1) target args.
static constexpr int AI_NUMBI_UPGRADE_ORDER_LEVEL  = 1;
static constexpr int AI_NUMBI_UPGRADE_ORDER_REPAIR = 1;

// adjustBuildings/checkoutExpands OrderCreate args (unitsWorking=1, flagRadius=1).
static constexpr int AI_NUMBI_BUILD_ORDER_UNITS_WORKING = 1;
static constexpr int AI_NUMBI_BUILD_ORDER_FLAG_RADIUS   = 1;
