// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>

// AICortex shared data types: the Observation and Action structs that sit
// between the three layers (observation -> policy -> action). See
// docs/AI/cortex/README.md for the design rationale.
//
// Both structs are deliberately POD (no pointers, no std::string, no virtuals)
// so that a future ML policy can serialize an Observation straight to a tensor
// and an Action straight from a discrete distribution. Every field is
// fixed-shape and bounded; per-enemy data lives in a fixed-size array padded
// with the "no enemy in this slot" sentinel. Bump the *_VERSION constant on any
// layout change so trained models can bind to a known shape.
//
// CONTRACT NOTE (the frozen Phase-1 economy layout): CortexObservation is a
// *superset* of every fact Nicowar's check_phases() decides on
// (glob2/src/ai/nicowar/Phases.cpp), so the policy never has to reach past this
// struct into Game*. The field comments name the exact TeamStat / Game source
// each value mirrors. The cortex-local CORTEX_* size constants mirror the engine
// constants (NB_UNIT_LEVELS, IntBuildingType::NB_BUILDING, NB_BUILDING_LONG_LEVELS,
// Team count); CortexObservation.cpp static_asserts that they stay in sync so
// this header itself stays free of heavy engine includes.

namespace Cortex
{
	/// Layout version of CortexObservation. Bump on any field add/remove/resize.
	/// v1 was the first economy-phase layout. v2 (2026-06-02) dropped the dead
	/// totalFood/totalFoodCapacity mirrors (TeamStat never writes them — always 0)
	/// and added feedCapacity + swarmsProducing for the production-throttle.
	static const Uint32 OBSERVATION_VERSION = 2;
	/// Layout version of CortexAction. Bump on any field add/remove/resize.
	/// v2 (2026-06-02) added ACTION_SET_PRODUCTION + productionRatio[].
	static const Uint32 ACTION_VERSION = 2;

	/// Fixed upper bound on enemy team slots in an Observation. 32 ==
	/// Team::MAX_COUNT_ON_DISK; it is a safe over-bound on the live team ceiling
	/// (Team::MAX_COUNT == 12), so every possible enemy team always has a slot.
	/// CortexObservation.cpp static_asserts MAX_ENEMY_SLOTS >= Team::MAX_COUNT.
	/// Unused slots are flagged inactive rather than omitted (fixed shape).
	static const int MAX_ENEMY_SLOTS = 32;

	/// Mirrors UnitConsts.h NB_UNIT_LEVELS (per-skill upgrade level buckets, 0..3).
	static const int CORTEX_UNIT_LEVELS = 4;
	/// Mirrors UnitConsts.h NB_UNIT_TYPE (WORKER, EXPLORER, WARRIOR). Indexes the
	/// swarm production-ratio triple. CortexObservation.cpp static_asserts it.
	static const int CORTEX_UNIT_TYPES = 3;
	/// Self-imposed upper bound on a single swarm production-ratio entry. Mirrors
	/// the GUI's MAX_RATIO_RANGE (gui/GameGUI.h) convention — the engine itself
	/// caps nothing (OrderModifySwarm writes ratios verbatim), so this is Cortex's
	/// own bound, not an engine constant; no static_assert needed.
	static const int CORTEX_MAX_RATIO = 16;
	/// Mirrors IntBuildingType::NB_BUILDING (number of distinct building types).
	static const int CORTEX_BUILDING_TYPES = 13;
	/// Mirrors TeamStat.h NB_BUILDING_LONG_LEVELS. The "long level" packs both a
	/// building's level and whether it is still a site into one index via
	/// longLevel = (level << 1) + 1 - isBuildingSite, range 0..5. So odd indices
	/// (1,3,5) are FINISHED buildings at level 0,1,2 and even indices (0,2,4) are
	/// their construction SITES. Use the cortex* helpers below rather than
	/// reading raw slots, so the encoding lives in exactly one place.
	static const int CORTEX_BUILDING_LONG_LEVELS = 6;

	// Cortex-local mirrors of the IntBuildingType::Number values the policy
	// names directly. The policy layer must not include engine headers, so it
	// refers to building types through these; CortexObservation.cpp
	// static_asserts each against IntBuildingType so they cannot drift.
	static const int CORTEX_BUILD_SWARM   = 0; ///< IntBuildingType::SWARM_BUILDING
	static const int CORTEX_BUILD_FOOD    = 1; ///< IntBuildingType::FOOD_BUILDING (inn)
	static const int CORTEX_BUILD_ATTACK  = 5; ///< IntBuildingType::ATTACK_BUILDING (barracks)
	static const int CORTEX_BUILD_SCIENCE = 6; ///< IntBuildingType::SCIENCE_BUILDING (school)

	/// Number of candidate build locations the placement helper surfaces per
	/// building type into the observation. The policy chooses a building type
	/// plus one of these K slots, keeping the action space a fixed-size discrete
	/// distribution (ML rule: no unbounded "(x, y)" — see README action rules).
	static const int CORTEX_BUILD_CANDIDATES = 4;

	/// One enemy team projected into the observation. POD, bounded.
	struct EnemySlot
	{
		Sint32 active;        ///< 0 = no enemy in this slot (sentinel), 1 = present and alive.
		Sint32 teamNumber;    ///< Engine team id, or -1 when inactive.
		Sint32 totalUnit;     ///< Visible unit count (caller must gate on fog-of-war).
		Sint32 totalBuilding; ///< Visible finished-building count.
		Sint32 prestige;      ///< Enemy prestige.
	};

	/// One candidate location for placing a building, produced by the placement
	/// helper (Cortex::placeCandidates) and surfaced in the observation. POD.
	struct BuildCandidate
	{
		Sint32 valid; ///< 0 = empty slot (no candidate here), 1 = usable location.
		Sint32 x;     ///< Map tile x of the building's top-left corner. Valid only if valid==1.
		Sint32 y;     ///< Map tile y.
		Sint32 score; ///< Relative placement score; higher is better. Ranking only, not normalized.
	};

	/// The full feature vector handed to the policy layer. Built by
	/// Cortex::observe(); read by CortexPolicy::decide(). The policy must read
	/// ONLY this struct — never Game* directly (see README anti-pattern).
	struct CortexObservation
	{
		Uint32 version;       ///< == OBSERVATION_VERSION; lets the policy reject stale layouts.
		Sint32 valid;         ///< 0 when no observation has been taken yet.

		Sint32 tick;          ///< game->stepCounter at observation time.

		// --- own economy: population (TeamStat unless noted) ---
		Sint32 totalUnit;     ///< stat->totalUnit.
		Sint32 workers;       ///< stat->numberUnitPerType[WORKER].
		Sint32 explorers;     ///< stat->numberUnitPerType[EXPLORER].
		Sint32 warriors;      ///< stat->numberUnitPerType[WARRIOR].
		Sint32 freeWorkers;   ///< stat->isFree[WORKER] (workers not assigned a job).
		Sint32 totalFree;     ///< stat->totalFree (idle units of all types).
		Sint32 totalNeeded;   ///< stat->totalNeeded (jobs requested across all buildings).

		// --- own economy: food / health pressure ---
		Sint32 totalBuilding;     ///< stat->totalBuilding (finished buildings only).
		Sint32 feedCapacity;      ///< Units the colony's inns can feed: sum of type->maxUnitInside over finished buildings with maxUnitWorking && type->canFeedUnit (mirrors AICastor's foodSum). The live food-supply signal — totalFood/totalFoodCapacity were dropped (TeamStat never populates them).
		Sint32 starvingUnits;     ///< team->stats.getStarvingUnits() == stat->needFoodCritical (hungry AND losing HP).
		Sint32 needFood;          ///< stat->needFood (hungry, not being fed, HP still full — early warning).
		Sint32 needFoodCritical;  ///< stat->needFoodCritical (== starvingUnits).
		Sint32 needFoodNoInns;    ///< stat->needFoodNoInns (hungry, not upgrading).
		Sint32 needHeal;          ///< stat->needHeal.

		// --- own economy: prestige ---
		Sint32 prestige;          ///< team->prestige.

		// --- production state ---
		// Count of FINISHED swarms whose total ratio (WORKER+EXPLORER+WARRIOR) is
		// nonzero, i.e. currently producing units. Lets the pure policy tell
		// whether a halt/resume ACTION_SET_PRODUCTION is actually needed without
		// reading raw per-swarm ratios — so it doesn't re-emit the order every cycle.
		Sint32 swarmsProducing;

		// --- training / upgrade levels, indexed by unit level 0..CORTEX_UNIT_LEVELS-1 ---
		// Each array mirrors one TeamStat upgrade slice that Nicowar's phases read.
		Sint32 buildLevel[CORTEX_UNIT_LEVELS];               ///< stat->upgradeState[BUILD][lvl] (any unit type).
		Sint32 attackSpeedLevel[CORTEX_UNIT_LEVELS];         ///< stat->upgradeState[ATTACK_SPEED][lvl].
		Sint32 workerSwimLevel[CORTEX_UNIT_LEVELS];          ///< stat->upgradeStatePerType[WORKER][SWIM][lvl]; index 0 == cannot swim.
		Sint32 explorerMagicGroundLevel[CORTEX_UNIT_LEVELS]; ///< stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][lvl].

		// --- buildings: full per-type, per-long-level histogram ---
		// Direct mirror of stat->numberBuildingPerTypePerLevel. Read it through
		// the cortex* helpers (finished vs site, by level) below.
		Sint32 buildingCountPerLevel[CORTEX_BUILDING_TYPES][CORTEX_BUILDING_LONG_LEVELS];

		// --- candidate build locations, per building type ---
		// Filled by the placement helper for the building types the AI may build;
		// other types' slots stay valid==0. ACTION_BUILD.locationSlot indexes the
		// second dimension for ACTION_BUILD.buildingType.
		BuildCandidate buildCandidates[CORTEX_BUILDING_TYPES][CORTEX_BUILD_CANDIDATES];

		// --- map / global facts ---
		Sint32 fruitOnMap;    ///< 1 if any fruit resource exists on the map (Map query).
		Sint32 totalPrestige; ///< game->totalPrestige (all teams; for the explorer-defence heuristic).

		// --- opponents ---
		Sint32 enemyCount;    ///< Number of active slots below.
		EnemySlot enemies[MAX_ENEMY_SLOTS];
	};

	/// Discrete, bounded intents the policy can choose. Hierarchical: a real
	/// decision is (kind, plus a few bounded parameters), never an unbounded
	/// "(x, y)". Phase 1 wires NoOp and Build; later phases add kinds (and a
	/// version bump).
	enum CortexActionKind
	{
		ACTION_NOOP = 0,      ///< Do nothing this decision cycle.
		ACTION_BUILD,         ///< Place buildingType at buildCandidates[buildingType][locationSlot].
		ACTION_SET_PRODUCTION,///< Set every finished swarm's production ratio to productionRatio[].

		ACTION_KIND_COUNT
	};

	/// The policy's output: an action intent, not an engine Order. The action
	/// layer (AICortex::translateAction) turns it into one or more Orders. POD,
	/// versioned, bounded.
	struct CortexAction
	{
		Uint32 version;      ///< == ACTION_VERSION.
		Sint32 kind;         ///< A CortexActionKind value.
		Sint32 buildingType; ///< For ACTION_BUILD: an IntBuildingType::Number in [0, CORTEX_BUILDING_TYPES). Else -1.
		Sint32 locationSlot; ///< For ACTION_BUILD: index in [0, CORTEX_BUILD_CANDIDATES). Else -1.
		Sint32 productionRatio[CORTEX_UNIT_TYPES]; ///< For ACTION_SET_PRODUCTION: target swarm ratio [WORKER,EXPLORER,WARRIOR], each 0..CORTEX_MAX_RATIO ({0,0,0} = halt). Else all 0.
	};

	// --- building-histogram accessors -------------------------------------
	// The long-level encoding (see CORTEX_BUILDING_LONG_LEVELS) lives only here.

	/// Finished buildings of `type` at internal level >= minLevel (0-based).
	inline Sint32 cortexFinishedBuildingsMinLevel(const CortexObservation& obs, int type, int minLevel)
	{
		Sint32 count = 0;
		for (int level = minLevel; level <= 2; level++) // three building levels: 0,1,2
			count += obs.buildingCountPerLevel[type][(level << 1) + 1]; // odd long-level == finished
		return count;
	}

	/// All finished buildings of `type`, any level.
	inline Sint32 cortexFinishedBuildings(const CortexObservation& obs, int type)
	{
		return cortexFinishedBuildingsMinLevel(obs, type, 0);
	}

	/// Construction sites of `type`, any level (even long-levels).
	inline Sint32 cortexBuildingSites(const CortexObservation& obs, int type)
	{
		return obs.buildingCountPerLevel[type][0]
		     + obs.buildingCountPerLevel[type][2]
		     + obs.buildingCountPerLevel[type][4];
	}

	/// Construct an empty/no-op observation with the current version stamped.
	inline CortexObservation makeEmptyObservation()
	{
		CortexObservation obs;
		obs.version = OBSERVATION_VERSION;
		obs.valid = 0;
		obs.tick = 0;

		obs.totalUnit = 0;
		obs.workers = 0;
		obs.explorers = 0;
		obs.warriors = 0;
		obs.freeWorkers = 0;
		obs.totalFree = 0;
		obs.totalNeeded = 0;

		obs.totalBuilding = 0;
		obs.feedCapacity = 0;
		obs.starvingUnits = 0;
		obs.needFood = 0;
		obs.needFoodCritical = 0;
		obs.needFoodNoInns = 0;
		obs.needHeal = 0;

		obs.prestige = 0;
		obs.swarmsProducing = 0;

		for (int i = 0; i < CORTEX_UNIT_LEVELS; i++)
		{
			obs.buildLevel[i] = 0;
			obs.attackSpeedLevel[i] = 0;
			obs.workerSwimLevel[i] = 0;
			obs.explorerMagicGroundLevel[i] = 0;
		}

		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
		{
			for (int l = 0; l < CORTEX_BUILDING_LONG_LEVELS; l++)
				obs.buildingCountPerLevel[t][l] = 0;
			for (int c = 0; c < CORTEX_BUILD_CANDIDATES; c++)
			{
				obs.buildCandidates[t][c].valid = 0;
				obs.buildCandidates[t][c].x = 0;
				obs.buildCandidates[t][c].y = 0;
				obs.buildCandidates[t][c].score = 0;
			}
		}

		obs.fruitOnMap = 0;
		obs.totalPrestige = 0;

		obs.enemyCount = 0;
		for (int i = 0; i < MAX_ENEMY_SLOTS; i++)
		{
			obs.enemies[i].active = 0;
			obs.enemies[i].teamNumber = -1;
			obs.enemies[i].totalUnit = 0;
			obs.enemies[i].totalBuilding = 0;
			obs.enemies[i].prestige = 0;
		}
		return obs;
	}

	/// Construct a no-op action with the current version stamped.
	inline CortexAction makeNoOpAction()
	{
		CortexAction action;
		action.version = ACTION_VERSION;
		action.kind = ACTION_NOOP;
		action.buildingType = -1;
		action.locationSlot = -1;
		for (int i = 0; i < CORTEX_UNIT_TYPES; i++)
			action.productionRatio[i] = 0;
		return action;
	}

	/// Construct a build action targeting one candidate slot.
	inline CortexAction makeBuildAction(int buildingType, int locationSlot)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_BUILD;
		action.buildingType = buildingType;
		action.locationSlot = locationSlot;
		return action;
	}

	/// Construct a set-production action: target ratio applied to all swarms.
	/// {0,0,0} halts production. Callers should keep each entry in [0, CORTEX_MAX_RATIO].
	inline CortexAction makeSetProductionAction(int workerRatio, int explorerRatio, int warriorRatio)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_SET_PRODUCTION;
		action.productionRatio[0] = workerRatio;
		action.productionRatio[1] = explorerRatio;
		action.productionRatio[2] = warriorRatio;
		return action;
	}
}
