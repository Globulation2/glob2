// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

// AI Echo per-slice tuning constants — Phase 3b (RTI scheduler / tuning).
//
// These name the magic numbers used by AIEcho::ReachToInfinity (the simple
// economic test AI in glob2/src/ai/echo/ReachToInfinity.cpp) and a handful
// of supporting classes (Gradient, Management, Construction, Conditions,
// Echo). The Phase 3b high-value pass already named the *sentinel* values
// in echo/Echo.h — this header covers the remaining *tuning* values.
//
// Pure rename pass: every literal value is preserved byte-for-byte.

namespace AIEcho
{
	// ---- RTI master scheduler ------------------------------------------------
	// ReachToInfinity::tick rotates through five "primary builders" on a
	// 2000-tick master cycle. The offsets are coupled — change BIG_CYCLE_TICKS
	// and you must shift the offsets too, or staggering breaks.

	/// Master cycle period for the RTI scheduler (~80s at 25 ticks/s).
	/// (ReachToInfinity.cpp: 375, 445, 500, 543, 591.)
	static constexpr int AI_ECHO_RTI_BIG_CYCLE_TICKS = 2000;

	/// Phase offsets within the master cycle. Swarm fires at offset 0, racetrack
	/// at 500, swimming pool at 1000, school at 1500.
	static constexpr int AI_ECHO_RTI_SWARM_OFFSET_TICKS = 0;
	static constexpr int AI_ECHO_RTI_RACETRACK_OFFSET_TICKS = 500;
	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_OFFSET_TICKS = 1000;
	static constexpr int AI_ECHO_RTI_SCHOOL_OFFSET_TICKS = 1500;

	/// Inn build attempt cadence (~8s); inhibited on master-cycle boundary.
	/// (ReachToInfinity.cpp:375.)
	static constexpr int AI_ECHO_RTI_INN_INTERVAL_TICKS = 200;

	/// Cadence for the enemy-swarm exploration-flag pass (~5s).
	/// (ReachToInfinity.cpp:341.)
	static constexpr int AI_ECHO_RTI_ENEMY_SCAN_INTERVAL_TICKS = 120;

	/// Cadence for the fruit-tree exploration-flag pass (~4s).
	/// (ReachToInfinity.cpp:236.)
	static constexpr int AI_ECHO_RTI_FRUIT_FLAG_INTERVAL_TICKS = 100;

	/// Cadence for both upgrade scheduler scopes — L1->L2 and L2->L3
	/// (~12s). (ReachToInfinity.cpp:630, 691.)
	static constexpr int AI_ECHO_RTI_UPGRADE_INTERVAL_TICKS = 300;

	/// Cadence for the destroy-failing-buildings scan (~20s).
	/// (ReachToInfinity.cpp:761.)
	static constexpr int AI_ECHO_RTI_DELETE_SCAN_INTERVAL_TICKS = 500;

	/// Cadence for the forbid-farming-area scan (~10s).
	/// (ReachToInfinity.cpp:804.)
	static constexpr int AI_ECHO_RTI_FARMING_INTERVAL_TICKS = 250;


	// ---- Inn (FOOD_BUILDING) sizing ------------------------------------------
	// The inn-population check sums "level1*POP_L1 + level2*POP_L2 + level3*POP_L3"
	// against the total unit count to decide when another inn is warranted.
	// (ReachToInfinity.cpp:392.)

	/// Population that a level-1 inn supports.
	static constexpr int AI_ECHO_RTI_INN_POP_PER_L1 = 8;
	/// Population that a level-2 inn supports.
	static constexpr int AI_ECHO_RTI_INN_POP_PER_L2 = 12;
	/// Population that a level-3 inn supports.
	static constexpr int AI_ECHO_RTI_INN_POP_PER_L3 = 16;


	// ---- Stale-inn / stale-swarm destroy thresholds --------------------------
	// (ReachToInfinity.cpp:771, 773, 791, 793.)

	/// Min resource-tracker age (~60s) before an inn becomes a destroy candidate.
	static constexpr int AI_ECHO_RTI_INN_DELETE_AGE_TICKS = 1500;
	/// Per-level food threshold; inn destroyed if total_level < THIS * level.
	static constexpr int AI_ECHO_RTI_INN_DELETE_FOOD_PER_LEVEL = 24;
	/// Min resource-tracker age (~100s) before a swarm becomes a destroy candidate.
	static constexpr int AI_ECHO_RTI_SWARM_DELETE_AGE_TICKS = 2500;
	/// Total-corn threshold below which a swarm is destroyed.
	static constexpr int AI_ECHO_RTI_SWARM_DELETE_FOOD = 18;


	// ---- Exploration-flag radii / explorer-count gates -----------------------

	/// Radius of fruit-tree exploration flags (cherry/orange/prune sites).
	/// (ReachToInfinity.cpp:265, 295, 325.)
	static constexpr int AI_ECHO_RTI_FRUIT_FLAG_RADIUS = 4;
	/// Radius of an exploration flag placed on an enemy swarm.
	/// (ReachToInfinity.cpp:358.)
	static constexpr int AI_ECHO_RTI_ENEMY_FLAG_RADIUS = 12;
	/// Min explorer count before the AI sets fruit-tree flags.
	/// (ReachToInfinity.cpp:240.)
	static constexpr int AI_ECHO_RTI_FRUIT_FLAG_EXPLORER_MIN = 6;
	/// Min explorer count before the AI flags enemy swarms.
	/// (ReachToInfinity.cpp:343.)
	static constexpr int AI_ECHO_RTI_ENEMY_FLAG_EXPLORER_MIN = 3;


	// ---- Initial / steady-state swarm setup ---------------------------------

	/// Workers assigned to the very first existing swarm at game start.
	/// (ReachToInfinity.cpp:97.)
	static constexpr int AI_ECHO_RTI_INITIAL_SWARM_WORKERS = 5;
	/// Resource-tracker history length (in tracker samples; tracker samples
	/// every 10 ticks, so 12 = ~120 ticks of history). Used in 6 sites.
	/// (ReachToInfinity.cpp:104, 109, 198, 438, 492, 892.)
	static constexpr int AI_ECHO_RTI_TRACKER_LENGTH = 12;
	/// Workers assigned to a freshly-ordered swarm site.
	/// (ReachToInfinity.cpp:455.)
	static constexpr int AI_ECHO_RTI_SWARM_WORKERS_NEW = 3;
	/// Workers reassigned once a swarm finishes construction.
	/// (ReachToInfinity.cpp:482.)
	static constexpr int AI_ECHO_RTI_SWARM_WORKERS_FINISHED = 5;

	/// Initial / steady-state swarm ratio (worker:explorer:warrior). Used at
	/// game start (ReachToInfinity.cpp:100) and on every new swarm completion
	/// (ReachToInfinity.cpp:487).
	static constexpr int AI_ECHO_RTI_SWARM_RATIO_WORKER = 15;
	static constexpr int AI_ECHO_RTI_SWARM_RATIO_EXPLORER = 1;
	static constexpr int AI_ECHO_RTI_SWARM_RATIO_WARRIOR = 0;


	// ---- Swarm cadence: early vs late population thresholds ------------------
	// "if (number<=EARLY_LIMIT && totalUnit/EARLY_RATIO >= number) || totalUnit/LATE_RATIO >= number"
	// (ReachToInfinity.cpp:450.)

	static constexpr int AI_ECHO_RTI_SWARM_EARLY_LIMIT = 3;
	static constexpr int AI_ECHO_RTI_SWARM_EARLY_RATIO = 20;
	static constexpr int AI_ECHO_RTI_SWARM_LATE_RATIO = 50;


	// ---- Inn placement constraint weights / distances ------------------------
	// Used by the standard inn order (ReachToInfinity.cpp:401, 403, 410, 416,
	// 426) and the "construct inn" message handler (ReachToInfinity.cpp:855,
	// 857, 864, 870, 880).

	/// Constraint weight: minimize distance to wheat (inn placement).
	static constexpr int AI_ECHO_RTI_INN_WHEAT_WEIGHT = 4;
	/// Constraint cap: inn must be within this many tiles of wheat.
	static constexpr int AI_ECHO_RTI_INN_WHEAT_MAX_DIST = 10;
	/// Constraint weight: prefer clustering with friendly buildings.
	static constexpr int AI_ECHO_RTI_BUILD_CLUSTER_WEIGHT = 2;
	/// Min-distance from friendly construction sites for an inn order.
	static constexpr int AI_ECHO_RTI_INN_CONSTRUCTION_MIN_DIST = 3;
	/// Constraint weight: light pull toward fruit (inn placement).
	static constexpr int AI_ECHO_RTI_INN_FRUIT_WEIGHT = 1;


	// ---- Swarm placement constraint weights / distances ----------------------
	// (ReachToInfinity.cpp:461, 468, 474.)

	/// Constraint weight: lighter cluster pull for swarms (vs inns).
	static constexpr int AI_ECHO_RTI_SWARM_CLUSTER_WEIGHT = 1;


	// ---- Racetrack (WALKSPEED_BUILDING) placement ----------------------------
	// (ReachToInfinity.cpp:505, 508, 514, 520, 522, 529, 535.)

	/// Workers assigned to a racetrack construction site.
	static constexpr int AI_ECHO_RTI_RACETRACK_WORKERS = 6;
	/// Constraint weight: minimize distance to wood.
	static constexpr int AI_ECHO_RTI_RACETRACK_WOOD_WEIGHT = 4;
	/// Constraint weight: minimize distance to stone.
	static constexpr int AI_ECHO_RTI_RACETRACK_STONE_WEIGHT = 1;
	/// Min-distance from stone (room to upgrade).
	static constexpr int AI_ECHO_RTI_RACETRACK_STONE_MIN_DIST = 2;
	/// Min-distance from friendly construction sites (room for racetrack upgrade).
	static constexpr int AI_ECHO_RTI_RACETRACK_CONSTR_MIN_DIST = 4;


	// ---- Swimming pool (SWIMSPEED_BUILDING) placement ------------------------
	// (ReachToInfinity.cpp:548, 551, 557, 563, 569, 576, 582.)

	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_WORKERS = 6;
	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_WOOD_WEIGHT = 4;
	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_WHEAT_WEIGHT = 1;
	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_STONE_MIN_DIST = 2;
	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_CONSTR_MIN_DIST = 4;


	// ---- School (SCIENCE_BUILDING) placement --------------------------------
	// (ReachToInfinity.cpp:596, 599, 606, 612, 621.)

	static constexpr int AI_ECHO_RTI_SCHOOL_WORKERS = 5;
	/// Min-distance from friendly construction sites (room to upgrade school).
	static constexpr int AI_ECHO_RTI_SCHOOL_CONSTR_MIN_DIST = 4;
	/// Constraint weight: maximize distance from enemy buildings.
	static constexpr int AI_ECHO_RTI_SCHOOL_ENEMY_DIST_WEIGHT = 3;


	// ---- Secondary-building (racetrack/swimmingpool/school) population gating
	// "if (totalUnit/SECONDARY_BLDG_RATIO) >= number && number < MAX_*"
	// (ReachToInfinity.cpp:505, 548, 596.)

	/// Population per allowed secondary building (one racetrack per 60 units, etc.).
	static constexpr int AI_ECHO_RTI_SECONDARY_BLDG_RATIO = 60;
	/// Hard cap on number of racetracks.
	static constexpr int AI_ECHO_RTI_RACETRACK_MAX = 3;
	/// Hard cap on number of swimming pools.
	static constexpr int AI_ECHO_RTI_SWIMMINGPOOL_MAX = 3;
	/// Hard cap on number of schools.
	static constexpr int AI_ECHO_RTI_SCHOOL_MAX = 4;


	// ---- Upgrade scheduler (level 1->2 and 2->3) ----------------------------
	// (ReachToInfinity.cpp:644, 712 — concurrent fraction; 649, 717 — school
	// gate; 662, 730 — workers during; 676 — L2 finished; 744 — L3 finished;
	// 633, 694 — target-level args.)

	/// Concurrent upgrades capped to ~1/THIS of all level-N buildings.
	static constexpr int AI_ECHO_RTI_CONCURRENT_UPGRADE_FRACTION = 15;
	/// Below this many schools, upgrade pass excludes schools (avoid bricking
	/// the upgrade pipeline).
	static constexpr int AI_ECHO_RTI_SCHOOL_THRESHOLD_FOR_UPGRADE = 2;
	/// Workers assigned to a building while it is being upgraded.
	static constexpr int AI_ECHO_RTI_UPGRADE_WORKERS_DURING = 8;
	/// Workers reassigned to a finished L2 inn.
	static constexpr int AI_ECHO_RTI_INN_L2_WORKERS_FINISHED = 3;
	/// Workers reassigned to a finished L3 inn.
	static constexpr int AI_ECHO_RTI_INN_L3_WORKERS_FINISHED = 6;
	/// User-facing 1-based target level for the L1->L2 upgrade pass.
	static constexpr int AI_ECHO_RTI_UPGRADE_TARGET_LEVEL_2 = 2;
	/// User-facing 1-based target level for the L2->L3 upgrade pass.
	static constexpr int AI_ECHO_RTI_UPGRADE_TARGET_LEVEL_3 = 3;


	// ---- Farming-area pattern -----------------------------------------------
	// The forbidden-farming-area scan applies a brush only on a checker
	// pattern (every 4th tile) to leave aisles between rows.
	// (ReachToInfinity.cpp:816, 830.)

	/// Stride modulus for the farming pattern: brush only on (x % STRIDE == 1
	/// && y % STRIDE == 1) tiles.
	static constexpr int AI_ECHO_RTI_FARMING_PATTERN_STRIDE = 2;
	/// Max distance from water for forbidden-farming-area placement.
	static constexpr int AI_ECHO_RTI_FARMING_WATER_MAX_DIST = 10;


	// ---- GradientManager / pending-building timing ---------------------------
	// (Gradient.cpp:265, 297, 308, 326, 329; Construction.cpp:802.)

	/// Maximum age (ticks) before a referenced gradient is force-recalculated.
	/// (~6s at 25 ticks/s; doc-string at Gradients.h:299-302.)
	static constexpr int AI_ECHO_GRADIENT_STALE_TICKS = 150;
	/// Pre-stale value seeded for newly-queued gradients so they age past the
	/// QUEUE_MIN_AGE gate immediately and recompute on the next update tick.
	static constexpr int AI_ECHO_GRADIENT_INITIAL_AGE_TICKS = 200;
	/// Min age (ticks) before a queued gradient is actually recomputed.
	static constexpr int AI_ECHO_GRADIENT_QUEUE_MIN_AGE_TICKS = 50;
	/// Tick timeout — drop a pending building if the engine hasn't placed it
	/// within this many ticks (~12s at 25 ticks/s).
	/// (Construction.cpp:802.)
	static constexpr int AI_ECHO_PENDING_BUILDING_TIMEOUT_TICKS = 300;


	// ---- Resource tracker sampling ------------------------------------------

	/// Sampling cadence for RessourceTracker — samples building resources
	/// every THIS many ticks. (Management.cpp:346.)
	static constexpr int AI_ECHO_TRACKER_SAMPLE_INTERVAL_TICKS = 10;


	// ---- Save signature -----------------------------------------------------

	/// Length (bytes) of the literal "EchoSig" save-stream signature (does NOT
	/// include a NUL terminator). (Echo.cpp:34, 42, 43.)
	static constexpr int AI_ECHO_SIGNATURE_LENGTH = 7;


	// ---- Misc level / priority codecs ---------------------------------------

	/// Engine BuildingType::level cap — engine level 2 is the third (max)
	/// level, so a building cannot be upgraded above this.
	/// (Conditions.cpp:561, Upgradable::passes.)
	static constexpr int AI_ECHO_MAX_BUILDING_LEVEL_INDEX = 2;

	/// On-disk and on-Order encoding of AdjustPriority::BuildingPriority.
	/// Stored as a Sint32 (-1/0/1) inside MAdjustPriority orders. Distinct
	/// from the AI_ECHO_TRIBOOL_* domain — those encode boost::logic::tribool
	/// in BuildingRegister/ChangeAlliances save streams.
	/// (Management.cpp:733-738, 769-773, 786-791.)
	static constexpr int AI_ECHO_PRIORITY_LOW = -1;
	static constexpr int AI_ECHO_PRIORITY_MEDIUM = 0;
	static constexpr int AI_ECHO_PRIORITY_HIGH = 1;
};
