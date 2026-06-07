// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

// AICortex inline accessor/helper functions: the building-histogram accessors
// over CortexObservation and the make*() factory helpers for CortexObservation
// and CortexAction. Split out of CortexTypes.h (the umbrella public header) so
// each header stays under 500 lines. These reference the POD struct definitions
// and the *_VERSION constants, so this header is included at the BOTTOM of
// CortexTypes.h, AFTER the structs it depends on are defined. Including it pulls
// in CortexTypes.h directly (the include guard makes the umbrella's bottom
// include of it a no-op, while a standalone include of CortexQuery.h still sees
// the structs).

#include "CortexTypes.h"

namespace Cortex
{
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

	/// Highest FINISHED building level (0,1,2) of `type`, or -1 if none finished.
	/// Reads the odd long-level slots (finished) high-to-low.
	inline Sint32 cortexMaxFinishedLevel(const CortexObservation& obs, int type)
	{
		for (int level = 2; level >= 0; level--)
			if (obs.buildingCountPerLevel[type][(level << 1) + 1] > 0)
				return level;
		return -1;
	}

	/// Count of `type` buildings currently mid-UPGRADE: a construction site of an
	/// already-raised level (1 or 2). A fresh level-0 site (even slot 0) is a NEW
	/// build, not an upgrade, so it is excluded. Lets the policy avoid stacking a
	/// second upgrade on a type that is already upgrading.
	inline Sint32 cortexBuildingsUpgrading(const CortexObservation& obs, int type)
	{
		return obs.buildingCountPerLevel[type][2]  // site of level 1 (0->1 upgrade)
		     + obs.buildingCountPerLevel[type][4]; // site of level 2 (1->2 upgrade)
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
			obs.walkLevel[i] = 0;
			obs.attackSpeedLevel[i] = 0;
			obs.attackStrengthLevel[i] = 0;
			obs.workerSwimLevel[i] = 0;
			obs.explorerMagicGroundLevel[i] = 0;
			obs.totalNeededPerLevel[i] = 0;
		}

		obs.maxBuildLevel = 0;

		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
		{
			obs.upgradableCount[t] = 0;
			for (int l = 0; l < CORTEX_BUILDING_LONG_LEVELS; l++)
				obs.buildingCountPerLevel[t][l] = 0;
			for (int c = 0; c < CORTEX_BUILD_CANDIDATES; c++)
			{
				obs.buildCandidates[t][c].valid = 0;
				obs.buildCandidates[t][c].x = 0;
				obs.buildCandidates[t][c].y = 0;
				obs.buildCandidates[t][c].score = 0;
				obs.buildCandidates[t][c].wheatDist = -1;
			}
		}

		for (int i = 0; i < CORTEX_FLAG_TARGETS; i++)
		{
			obs.flagTargets[i].valid = 0;
			obs.flagTargets[i].x = 0;
			obs.flagTargets[i].y = 0;
			obs.flagTargets[i].score = 0;
			obs.flagTargets[i].wheatDist = -1;
		}
		obs.defenseTarget.valid = 0;
		obs.defenseTarget.x = 0;
		obs.defenseTarget.y = 0;
		obs.defenseTarget.score = 0;
		obs.defenseTarget.wheatDist = -1;
		obs.warFlagsActive = 0;
		obs.enemyUnitsNearFlag = 0;
		obs.unitsUnderAttack = 0;
		obs.buildingsUnderAttack = 0;

		// Neutral defaults; AICortex overwrites these with its live RAM-only
		// hysteresis state after observe() returns, before policy.decide() (the
		// wheatOpenMargin pattern: observe leaves a placeholder, AICortex injects).
		obs.flagPosture = CORTEX_POSTURE_NONE;
		obs.offenseHoldUntil = 0;

		obs.wheatOpenMargin = 0;
		obs.wheatProtectAddCount = 0;
		obs.wheatProtectDelCount = 0;
		obs.swarmsProducingExplorer = 0;
		obs.swarmsProducingWarrior = 0;
		obs.swarmsProducingWorker = 0;

		obs.swarmCount = 0;
		obs.innCount = 0;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SWARMS; i++)
		{
			obs.trackedSwarms[i].valid = 0;
			obs.trackedSwarms[i].gid = -1;
			obs.trackedSwarms[i].corn = 0;
			obs.trackedSwarms[i].maxCorn = 0;
			obs.trackedSwarms[i].maxUnitWorking = 0;
			obs.trackedSwarms[i].unitsInside = 0;
			obs.trackedSwarms[i].maxUnitInside = 0;
			obs.trackedSwarms[i].nearestWheatDist = -1;
			obs.trackedSwarms[i].harvestableWheatNearby = -1;
			obs.trackedSwarms[i].restockTripsNeeded = -1;
			obs.trackedSwarms[i].priority = 0;
		}
		for (int i = 0; i < CORTEX_MAX_TRACKED_INNS; i++)
		{
			obs.trackedInns[i].valid = 0;
			obs.trackedInns[i].gid = -1;
			obs.trackedInns[i].corn = 0;
			obs.trackedInns[i].maxCorn = 0;
			obs.trackedInns[i].maxUnitWorking = 0;
			obs.trackedInns[i].unitsInside = 0;
			obs.trackedInns[i].maxUnitInside = 0;
			obs.trackedInns[i].nearestWheatDist = -1;
			obs.trackedInns[i].harvestableWheatNearby = -1;
			obs.trackedInns[i].restockTripsNeeded = -1;
			obs.trackedInns[i].priority = 0;
		}
		obs.siteCount = 0;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SITES; i++)
		{
			obs.trackedSites[i].valid = 0;
			obs.trackedSites[i].gid = -1;
			obs.trackedSites[i].maxUnitWorking = 0;
			obs.trackedSites[i].deliveriesLeft = 0;
		}

		obs.fruitOnMap = 0;
		obs.totalPrestige = 0;

		obs.algaeDiscovered = 0;
		obs.swimLandReach = 0;
		obs.swimWaterReach = 0;
		obs.algaeReachable = 0;

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
		action.flagRadius = -1;
		action.unitCount = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SWARMS; i++)
			action.swarmWorkers[i] = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_INNS; i++)
			action.innWorkers[i] = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SITES; i++)
			action.siteWorkers[i] = -1;
		action.priorityTarget = CORTEX_PRIORITY_NONE;
		action.priorityRest   = CORTEX_PRIORITY_NONE;
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

	/// Offense: recall/commit our single war flag onto flagTargets[locationSlot].
	/// radius/unitCount are clamped to [1,CORTEX_MAX_FLAG_RADIUS]/[0,CORTEX_MAX_FLAG_UNITS]
	/// by the action layer.
	inline CortexAction makeWarFlagAction(int locationSlot, int flagRadius, int unitCount)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_PLACE_WAR_FLAG;
		action.locationSlot = locationSlot;
		action.flagRadius = flagRadius;
		action.unitCount = unitCount;
		return action;
	}

	/// Defense: recall our single war flag onto obs.defenseTarget.
	inline CortexAction makeDefenseFlagAction(int flagRadius, int unitCount)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_PLACE_DEFENSE_FLAG;
		action.flagRadius = flagRadius;
		action.unitCount = unitCount;
		return action;
	}

	/// Remove our war flag if one exists (no offense/defense wanted right now).
	inline CortexAction makeClearFlagsAction()
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_CLEAR_FLAGS;
		return action;
	}

	/// Upgrade one finished instance of `buildingType` to its next level. The
	/// action layer picks the specific eligible (bottleneck) instance and the
	/// engine-default worker counts; the policy only names the type to upgrade.
	inline CortexAction makeUpgradeAction(int buildingType)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_UPGRADE_BUILDING;
		action.buildingType = buildingType;
		return action;
	}

	/// Construct an empty worker-tuning action: kind set, every per-building target
	/// initialised to -1 (leave unchanged). The caller fills swarmWorkers[i] /
	/// innWorkers[i] (indexed in lockstep with obs.trackedSwarms[]/trackedInns[])
	/// for the buildings it wants to retarget. The action layer dedups, so a target
	/// equal to the building's current maxUnitWorking emits no order.
	inline CortexAction makeTuneWorkersAction()
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_TUNE_WORKERS;
		return action;
	}

	/// Set the FIRST swarm's engine priority to `first` and every other swarm's to
	/// `rest` (each -1/0/+1). The action layer dedups against each swarm's current
	/// Building::priority, so re-issuing the same targets every cycle emits no order.
	/// Steady state keeps the primary swarm at CORTEX_PRIORITY_HIGH (first) so it wins
	/// worker/hauler contention while later swarms stay CORTEX_PRIORITY_NORMAL (rest);
	/// the panic defense raises ALL swarms to HIGH (first == rest == HIGH).
	inline CortexAction makeSetPriorityAction(int first, int rest)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_SET_PRIORITY;
		action.priorityTarget = first;
		action.priorityRest   = rest;
		return action;
	}
}
