// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault
//
// AINicowarTuning.h
//
// Behavior-preserving tuning constants for AINicowar (the "NewNicowar"
// AIEcho-based AI), extracted from glob2/src/ai/nicowar/*.cpp during the
// magic-number cleanup pass that prepares the codebase for the Rust port.
// Every value here is byte-for-byte identical to the literal it replaces;
// nothing in the AI's decision logic changes.
//
// Sentinel-style constants that already had names from Phase 3a are NOT
// redeclared here:
//   - AI_NICOWAR_NO_TARGET (in AINicowar.h)
//
// Constants are file-scope `static constexpr int` per the slice convention.

#pragma once

// ---------------------------------------------------------------------------
// Save-format minor-version gates (NewNicowar::load).
// versionMinor >= 59 enables the bulk of the Nicowar-specific section; >= 60
// adds the per-strategy name and `can_swim` flag; >= 66 adds the
// defense_flags / explorer_attack_flags vectors. Below the V59 cutoff, load
// silently returns true with a default-initialized AI (legacy save-compat).
// ---------------------------------------------------------------------------
static constexpr int AI_NICOWAR_SAVE_FORMAT_V59 = 59;
static constexpr int AI_NICOWAR_SAVE_FORMAT_V60 = 60;
static constexpr int AI_NICOWAR_SAVE_FORMAT_V66 = 66;

// ---------------------------------------------------------------------------
// Lifecycle / decision-cycle scheduling (NewNicowar::tick).
// On the first tick (timer == AI_NICOWAR_INIT_TICK), the AI selects a
// strategy, evaluates its phase booleans, and initializes existing buildings.
// Thereafter the decision cycle runs every AI_NICOWAR_DECISION_CYCLE_TICKS
// ticks, split across six staggered sub-phases (each phase fires once per
// cycle at its own offset within that cycle).
// ---------------------------------------------------------------------------
static constexpr int AI_NICOWAR_INIT_TICK              = 1;
static constexpr int AI_NICOWAR_DECISION_CYCLE_TICKS   = 100;
static constexpr int AI_NICOWAR_QUEUE_BUILDINGS_PHASE  = 0;
static constexpr int AI_NICOWAR_CHECK_PHASES_PHASE     = 17;
static constexpr int AI_NICOWAR_MANAGE_BUILDINGS_PHASE = 33;
static constexpr int AI_NICOWAR_UPGRADE_PHASE          = 50;
static constexpr int AI_NICOWAR_CONTROL_ATTACKS_PHASE  = 67;
static constexpr int AI_NICOWAR_DEFENSE_FLAG_PHASE     = 84;

// Farming runs on its own slower 250-tick cycle, with two phases:
//   timer % 250 == 0  -> update_farming
//   timer % 250 == 85 -> update_fruit_flags
static constexpr int AI_NICOWAR_FARMING_INTERVAL_TICKS = 250;
static constexpr int AI_NICOWAR_FRUIT_PHASE_OFFSET     = 85;

// Explorer-attack repositioning runs on a 1000-tick cycle, fired at offset
// 570 within that cycle (so once every 1000 ticks).
static constexpr int AI_NICOWAR_EXPLORER_ATTACK_INTERVAL_TICKS = 1000;
static constexpr int AI_NICOWAR_EXPLORER_ATTACK_OFFSET         = 570;

// ---------------------------------------------------------------------------
// Ressource tracker depth: AddRessourceTracker(N, CORN, id) records the last
// N resource samples per tracked building. Used at every tracker-creation
// site (initialization, every newly-ordered inn/swarm, and the per-level
// inn assignment math which multiplies a wheat-trigger threshold by this
// same N to convert per-tick wheat into the tracker's accumulated total).
// ---------------------------------------------------------------------------
static constexpr int AI_NICOWAR_RESSOURCE_TRACKER_DEPTH = 25;

// ---------------------------------------------------------------------------
// Phase iteration / level dimensions (Phases.cpp).
// Globulation 2 building/skill levels run 0..3 (four levels total).
// ---------------------------------------------------------------------------
// Inclusive upper bound when summing trained warriors at minimum-level..3
// in the war-preparation gate (`for(i = strategy.min_warrior_level...; i<=3; ...)`).
static constexpr int AI_NICOWAR_MAX_UPGRADE_LEVEL = 3;

// Avoid div-by-zero when computing the starvation percentage:
// only compute `needFoodNoInns * 100 / totalUnit` when totalUnit > 1.
static constexpr int AI_NICOWAR_STARVATION_MIN_UNITS = 1;

// Number of skill levels (0..3 inclusive) iterated when summing
// upgradeStatePerType[WORKER][SWIM][i] for the can-swim phase test.
static constexpr int AI_NICOWAR_LEVEL_COUNT = 4;

// Hardcoded "max-level explorer" index used by the explorer-attack-phase
// gate: only level-3 (fully upgraded) MAGIC_ATTACK_GROUND explorers are
// counted for the threshold.
static constexpr int AI_NICOWAR_EXPLORER_MAX_LEVEL = 3;

// ---------------------------------------------------------------------------
// Building-order tuning (Buildings.cpp). Each constraint constant is named
// after its (building-type, gradient-source, constraint-kind) triple.
//
// Naming convention: AI_NICOWAR_<BUILDING>_<WHAT>.
//   _ORDER_WORKERS  -> initial worker count passed to BuildingOrder
//   _<RES>_PREF     -> MinimizedDistance (we want to be near RES)
//   _<RES>_MIN      -> MinimumDistance (we don't want to be too close to RES)
//   _<RES>_MAX      -> MaximumDistance (hard cap on distance from RES)
//   _BUILDING_PREF  -> MinimizedDistance to friendly buildings
//   _CONSTRUCTION_MIN -> MinimumDistance to friendly buildings under construction
//   _ENEMY_<...>    -> distance to any enemy team building
// ---------------------------------------------------------------------------

// --- Inn (FOOD_BUILDING) ---
static constexpr int AI_NICOWAR_INN_ORDER_WORKERS       = 2;
static constexpr int AI_NICOWAR_INN_WHEAT_MIN_DIST      = 8;
static constexpr int AI_NICOWAR_INN_WHEAT_MAX_DIST      = 10;
static constexpr int AI_NICOWAR_INN_WATER_MIN_DIST      = 6;
static constexpr int AI_NICOWAR_INN_BUILDING_PREF       = 4;
static constexpr int AI_NICOWAR_INN_CONSTRUCTION_MIN    = 4;
static constexpr int AI_NICOWAR_INN_ENEMY_MAX_DIST      = 1;
static constexpr int AI_NICOWAR_INN_FRUIT_PREF          = 1;

// --- Swarm (SWARM_BUILDING) ---
static constexpr int AI_NICOWAR_SWARM_ORDER_WORKERS     = 4;
static constexpr int AI_NICOWAR_SWARM_WHEAT_PREF        = 6;
static constexpr int AI_NICOWAR_SWARM_WATER_MIN_DIST    = 6;
static constexpr int AI_NICOWAR_SWARM_BUILDING_PREF     = 1;
static constexpr int AI_NICOWAR_SWARM_CONSTRUCTION_MIN  = 2;

// --- Racetrack (WALKSPEED_BUILDING) ---
static constexpr int AI_NICOWAR_RACETRACK_ORDER_WORKERS    = 6;
static constexpr int AI_NICOWAR_RACETRACK_WOOD_PREF        = 4;
static constexpr int AI_NICOWAR_RACETRACK_WATER_MIN_DIST   = 6;
static constexpr int AI_NICOWAR_RACETRACK_STONE_PREF       = 1;
static constexpr int AI_NICOWAR_RACETRACK_STONE_MIN        = 2;
static constexpr int AI_NICOWAR_RACETRACK_BUILDING_PREF    = 2;
static constexpr int AI_NICOWAR_RACETRACK_SAND_MIN         = 2;
static constexpr int AI_NICOWAR_RACETRACK_CONSTRUCTION_MIN = 4;

// --- Swimmingpool (SWIMSPEED_BUILDING) ---
static constexpr int AI_NICOWAR_SWIMMINGPOOL_ORDER_WORKERS    = 6;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_WOOD_PREF        = 4;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_WATER_MIN_DIST   = 6;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_WHEAT_PREF       = 1;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_STONE_MIN        = 2;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_BUILDING_PREF    = 2;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_SAND_MIN         = 2;
static constexpr int AI_NICOWAR_SWIMMINGPOOL_CONSTRUCTION_MIN = 4;

// --- School (SCIENCE_BUILDING) ---
static constexpr int AI_NICOWAR_SCHOOL_ORDER_WORKERS    = 5;
static constexpr int AI_NICOWAR_SCHOOL_BUILDING_PREF    = 2;
static constexpr int AI_NICOWAR_SCHOOL_WATER_MIN_DIST   = 6;
static constexpr int AI_NICOWAR_SCHOOL_CONSTRUCTION_MIN = 4;
static constexpr int AI_NICOWAR_SCHOOL_ENEMY_MAX_DIST   = 3;

// --- Barracks (ATTACK_BUILDING) ---
static constexpr int AI_NICOWAR_BARRACKS_ORDER_WORKERS    = 6;
static constexpr int AI_NICOWAR_BARRACKS_WATER_MIN_DIST   = 6;
static constexpr int AI_NICOWAR_BARRACKS_STONE_PREF       = 5;
static constexpr int AI_NICOWAR_BARRACKS_WOOD_PREF        = 2;
static constexpr int AI_NICOWAR_BARRACKS_BUILDING_PREF    = 2;
static constexpr int AI_NICOWAR_BARRACKS_CONSTRUCTION_MIN = 2;

// Halve the queued barracks-demand by free-warrior count: barracks demand
// is min(strategy.war_prep_barracks, isFree[WARRIOR] / N). Splits the
// "/2" so the divisor is named.
static constexpr int AI_NICOWAR_BARRACKS_FREE_WARRIOR_DIVISOR = 2;

// --- Hospital (HEAL_BUILDING) ---
static constexpr int AI_NICOWAR_HOSPITAL_ORDER_WORKERS    = 2;
static constexpr int AI_NICOWAR_HOSPITAL_WOOD_PREF        = 2;
static constexpr int AI_NICOWAR_HOSPITAL_WATER_MIN_DIST   = 6;
static constexpr int AI_NICOWAR_HOSPITAL_BUILDING_PREF    = 3;
static constexpr int AI_NICOWAR_HOSPITAL_CONSTRUCTION_MIN = 2;

// ---------------------------------------------------------------------------
// Explorer cap (manage_swarm in Buildings.cpp):
//   needed_explorers = min(strategy.base_number_of_explorers,
//                          totalUnit / DIVISOR + MIN);
// "Min one explorer, plus one per N pop."
// ---------------------------------------------------------------------------
static constexpr int AI_NICOWAR_EXPLORER_POP_DIVISOR = 10;
static constexpr int AI_NICOWAR_EXPLORER_MIN         = 1;

// ---------------------------------------------------------------------------
// Upgrade selection (Upgrade.cpp).
// ---------------------------------------------------------------------------
// Need at least this many level-2 OR level-3 schools before allowing any
// further school upgrades during upgrading_phase_2.
static constexpr int AI_NICOWAR_LVL2_SCHOOL_THRESHOLD = 2;

// Sentinel returned by choose_building_upgrade_type / _level1 / _level2 when
// no eligible building type is available for upgrading. Distinct from
// AI_NICOWAR_NO_TARGET (target-team sentinel) and from the choose-building-
// to-attack "no building" sentinel in Attack.cpp.
static constexpr int AI_NICOWAR_NO_BUILDING_TYPE = -1;

// ---------------------------------------------------------------------------
// Attack control (Attack.cpp).
// ---------------------------------------------------------------------------
// The Echo gradient layer reports -2 for "unreachable" cells. Used to skip
// enemy buildings we can't path to. Distinct from Echo's wildcard `-1` args
// to enemy_building_iterator and from AI_NICOWAR_NO_TARGET, even though all
// three are negative integers in adjacent code.
static constexpr int AI_NICOWAR_GRADIENT_UNREACHABLE = -2;

// ChangeFlagMinimumLevel(N, war_flag): only warriors level-N or higher
// participate in war-flag attacks.
static constexpr int AI_NICOWAR_WAR_FLAG_MIN_LEVEL = 2;

// Pseudo-INT_MAX initial value for the "closest reachable cell" distance
// scan in dig_out_enemy(). Kept as a plain literal at the call site rather
// than INT_MAX because the loop computes `dist < closest_distance` with
// gradient values that are bounded well below 10000.
static constexpr int AI_NICOWAR_DIG_OUT_INIT_DIST = 10000;

// Initial value of the per-step "ticks since last clearing flag" counter.
// Set to AI_NICOWAR_DIG_FLAG_INTERVAL so the very first iteration of the
// dig-out loop places a clearing flag at the start of the path.
static constexpr int AI_NICOWAR_DIG_FLAG_INIT_COUNTER = 3;

// Tolerance in the dig-out pathfind: at each step we accept a neighbor cell
// whose gradient height is at most `current + N`. Keeps the path moving
// toward the goal while allowing minor backtracks.
static constexpr int AI_NICOWAR_PATHFIND_TOLERANCE = 2;

// Place a clearing flag every N steps along the dig-out path
// (`if(flag_dist_count > N) ...`).
static constexpr int AI_NICOWAR_DIG_FLAG_INTERVAL = 3;

// Workers assigned to each clearing flag spawned during a dig-out.
static constexpr int AI_NICOWAR_DIG_CLEARING_WORKERS = 10;

// Radius assigned (via ChangeFlagSize) to each clearing flag during dig-out.
static constexpr int AI_NICOWAR_DIG_FLAG_SIZE = 3;

// ---------------------------------------------------------------------------
// Defense flag positioning (Flags.cpp::compute_defense_flag_positioning).
// ---------------------------------------------------------------------------
// Detection radius used to flag-paint cells around each unit/building under
// attack. Defines the candidate-flag-area; multiple under-attack entities
// within RADIUS contribute additively to a square's score.
static constexpr int AI_NICOWAR_DEFENSE_FLAG_RADIUS = 4;

// When iterating over the (px, py) area to clear scored entities and count
// nearby enemy warriors, we extend the loop by this margin so that buildings
// (which contribute their score at an offset position determined by
// type->decLeft / decTop) are still found just outside RADIUS.
static constexpr int AI_NICOWAR_DEFENSE_BUILDING_OFFSET_MARGIN = 3;

// Cap on workers assigned to a single defense flag (`std::min(20, enemy_count)`).
static constexpr int AI_NICOWAR_MAX_DEFENSE_FLAG_WORKERS = 20;

// Squared distance limit (in tiles^2) for moving an existing defense flag
// to a new candidate position rather than destroying and re-creating it.
// At source: `if(min_dist < (8*8))`. Kept as `8 * 8` at the call site so
// the "8 tiles, squared" intent stays visible.
static constexpr int AI_NICOWAR_DEFENSE_FLAG_MAX_MOVE_TILES = 8;

// Half-extent (in tiles) of the per-cell scan used by the second-pass
// "destroy unmoved defense flags whose enemy_count is now zero" loop.
// At source: `for(int px = -3; px <= 3; ++px)`.
static constexpr int AI_NICOWAR_DEFENSE_REASSIGN_RADIUS = 3;

// Radius assigned (via ChangeFlagSize) to each freshly-created defense flag.
static constexpr int AI_NICOWAR_DEFENSE_FLAG_SIZE = 4;

// ---------------------------------------------------------------------------
// Explorer-attack flag positioning (compute_explorer_flag_attack_positioning).
// Groups enemy units into clusters; each cluster radiates outward from a
// seed unit, picking up neighbors within EXPLORER_GROUP_SEARCH_RADIUS as
// long as they stay within the centered cohesion ball EXPLORER_GROUP_COHESION
// (warpDistSquare from cluster centroid).
// ---------------------------------------------------------------------------
// Half-extent in tiles for the (dx, dy) neighbor-scan around each unit.
// Source uses `for(int dx = -4; dx<=4; ++dx)` etc.
static constexpr int AI_NICOWAR_EXPLORER_GROUP_SEARCH_RADIUS = 4;

// Cluster cohesion in tiles: candidates are accepted only if their
// warpDistSquare from the running cluster centroid is < N*N. Kept as the
// raw tile count so the call site retains the `(N * N)` literal.
static constexpr int AI_NICOWAR_EXPLORER_GROUP_COHESION_TILES = 6;

// Radius assigned (via ChangeFlagSize) to each explorer-attack flag.
static constexpr int AI_NICOWAR_EXPLORER_ATTACK_FLAG_SIZE = 6;

// Min explorer level required to participate in an explorer-attack flag.
// [POSSIBLE BUG / preserved] Skill levels run 0..3; setting min level to 4
// either locks the flag entirely or is silently capped at MAX_LEVEL=3 by the
// engine. See bugs_surfaced_during_magic_number_audit.md M8 -- the literal
// is preserved verbatim, only named.
static constexpr int AI_NICOWAR_EXPLORER_ATTACK_MIN_LEVEL = 4;

// ---------------------------------------------------------------------------
// Farming (Farming.cpp::update_farming, ::update_fruit_flags).
// ---------------------------------------------------------------------------
// Maximum distance (in water-gradient steps) from water at which the AI
// will permit a wood-farm spot.
static constexpr int AI_NICOWAR_FARM_WOOD_WATER_DIST = 6;

// Maximum distance (in water-gradient steps) from water at which the AI
// will permit a wheat-farm spot.
static constexpr int AI_NICOWAR_FARM_WHEAT_WATER_DIST = 10;

// Stride of the checkerboard farming pattern: permanent farm spots are at
// every (x % N == 1 && y % N == 1); horizontal expansion at (0, 1) and
// vertical expansion at (1, 0). Single shared stride.
static constexpr int AI_NICOWAR_FARM_PATTERN_STRIDE = 2;

// Workers ordered onto each fruit-tree exploration flag (cherry, orange,
// prune). One BuildingOrder per fruit type.
static constexpr int AI_NICOWAR_FRUIT_FLAG_WORKERS = 2;

// MinimizedDistance(gi_building, N) on each fruit flag -- prefer fruit
// trees closer to our settlement.
static constexpr int AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF = 1;

// MaximumDistance(gi_<fruit>, N) on each fruit flag -- the flag must sit
// directly on top of fruit (distance 0).
static constexpr int AI_NICOWAR_FRUIT_FLAG_ON_FRUIT_DIST = 0;

// Radius assigned (via ChangeFlagSize) to each fruit-tree exploration flag.
static constexpr int AI_NICOWAR_FRUIT_FLAG_SIZE = 4;
