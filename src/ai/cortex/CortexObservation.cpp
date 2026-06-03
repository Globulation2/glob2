// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexObservation.h"
#include "CortexPlacement.h"

#include "Player.h"
#include "Game.h"
#include "team/Team.h"
#include "TeamStat.h"
#include "unit/UnitConsts.h"
#include "IntBuildingType.h"
#include "building/Building.h"
#include "BuildingType.h"
#include "map/Map.h"
#include "Ressource.h"

// The frozen header (CortexTypes.h) deliberately carries no heavy engine
// includes, so its CORTEX_* size constants are hand-mirrored copies of the
// engine constants. These static_asserts are the tripwire that catches any
// silent drift between the two — if the engine grows a unit level, building
// type, long-level bucket, or team slot, the build fails here instead of
// producing a truncated/over-read observation at runtime.
static_assert(Cortex::CORTEX_UNIT_LEVELS == NB_UNIT_LEVELS,
	"CORTEX_UNIT_LEVELS must match UnitConsts.h NB_UNIT_LEVELS");
static_assert(Cortex::CORTEX_UNIT_TYPES == NB_UNIT_TYPE,
	"CORTEX_UNIT_TYPES must match UnitConsts.h NB_UNIT_TYPE");
static_assert(Cortex::CORTEX_BUILDING_TYPES == IntBuildingType::NB_BUILDING,
	"CORTEX_BUILDING_TYPES must match IntBuildingType::NB_BUILDING");
static_assert(Cortex::CORTEX_BUILDING_LONG_LEVELS == NB_BUILDING_LONG_LEVELS,
	"CORTEX_BUILDING_LONG_LEVELS must match TeamStat.h NB_BUILDING_LONG_LEVELS");
static_assert(Cortex::MAX_ENEMY_SLOTS >= Team::MAX_COUNT,
	"MAX_ENEMY_SLOTS must cover the engine team array bound Team::MAX_COUNT");
static_assert(Cortex::CORTEX_BUILD_SWARM == IntBuildingType::SWARM_BUILDING,
	"CORTEX_BUILD_SWARM must match IntBuildingType::SWARM_BUILDING");
static_assert(Cortex::CORTEX_BUILD_FOOD == IntBuildingType::FOOD_BUILDING,
	"CORTEX_BUILD_FOOD must match IntBuildingType::FOOD_BUILDING");
static_assert(Cortex::CORTEX_BUILD_ATTACK == IntBuildingType::ATTACK_BUILDING,
	"CORTEX_BUILD_ATTACK must match IntBuildingType::ATTACK_BUILDING");
static_assert(Cortex::CORTEX_BUILD_SCIENCE == IntBuildingType::SCIENCE_BUILDING,
	"CORTEX_BUILD_SCIENCE must match IntBuildingType::SCIENCE_BUILDING");

namespace Cortex
{
	CortexObservation observe(Player* player)
	{
		CortexObservation obs = makeEmptyObservation();

		if (player == NULL || player->team == NULL)
			return obs; // valid stays 0 — caller treats as "no observation".

		Team* team = player->team;
		Game* game = team->game;

		obs.tick = (game != NULL) ? static_cast<Sint32>(game->stepCounter) : 0;

		// --- own economy: read straight from the latest team stat snapshot ---
		const TeamStat* stat = team->stats.getLatestStat();

		// population
		obs.totalUnit         = stat->totalUnit;
		obs.workers           = stat->numberUnitPerType[WORKER];
		obs.explorers         = stat->numberUnitPerType[EXPLORER];
		obs.warriors          = stat->numberUnitPerType[WARRIOR];
		obs.freeWorkers       = stat->isFree[WORKER];
		obs.totalFree         = stat->totalFree;
		obs.totalNeeded       = stat->totalNeeded;

		// food / health pressure
		obs.totalBuilding     = stat->totalBuilding;
		obs.starvingUnits     = team->stats.getStarvingUnits();
		obs.needFood          = stat->needFood;
		obs.needFoodCritical  = stat->needFoodCritical;
		obs.needFoodNoInns    = stat->needFoodNoInns;
		obs.needHeal          = stat->needHeal;

		// prestige
		obs.prestige          = team->prestige;

		// production / food-supply: one live pass over the colony's buildings.
		// The TeamStat snapshot carries neither signal, so both are computed
		// here directly from team->myBuildings (iterated by index, never a set).
		//   feedCapacity   = units the colony's inns can feed: sum of
		//                    type->maxUnitInside over working, feeding buildings.
		//                    Mirrors AICastor's foodSum (ai/castor/Control.cpp:36-43);
		//                    the live food-supply signal that replaces the dropped
		//                    totalFood/totalFoodCapacity (TeamStat never wrote them).
		//   swarmsProducing = count of FINISHED swarms (buildingState==ALIVE, not a
		//                    site/dead) whose production ratio is nonzero, i.e.
		//                    actually producing units right now.
		obs.feedCapacity    = 0;
		obs.swarmsProducing = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL)
				continue;
			if (b->maxUnitWorking && b->type->canFeedUnit)
				obs.feedCapacity += b->type->maxUnitInside;
			if (b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING
			 && b->buildingState == Building::ALIVE
			 && !b->type->isBuildingSite   // exclude swarm sites / swarms under upgrade
			 && (b->ratio[0] | b->ratio[1] | b->ratio[2]))
				obs.swarmsProducing++;
		}

		// training / upgrade level buckets (one slice per array)
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
		{
			obs.buildLevel[lvl]               = stat->upgradeState[BUILD][lvl];
			obs.attackSpeedLevel[lvl]         = stat->upgradeState[ATTACK_SPEED][lvl];
			// SWIM is 1-based in storage (index 0 == cannot swim); copied verbatim.
			obs.workerSwimLevel[lvl]          = stat->upgradeStatePerType[WORKER][SWIM][lvl];
			obs.explorerMagicGroundLevel[lvl] = stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][lvl];
		}

		// full per-type, per-long-level building histogram (verbatim mirror;
		// the long-level encoding is decoded by the cortex* helpers, not here).
		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
			for (int l = 0; l < CORTEX_BUILDING_LONG_LEVELS; l++)
				obs.buildingCountPerLevel[t][l] = stat->numberBuildingPerTypePerLevel[t][l];

		// --- map / global facts ---
		if (game != NULL)
		{
			obs.totalPrestige = game->totalPrestige;

			// fruitOnMap: replicate Echo::check_fruit() directly off the Map
			// (AIEcho/MapInfo::is_ressource -> Map::isRessourceTakeable) so the
			// direct binding carries no Echo dependency. Any takeable fruit
			// (CHERRY/ORANGE/PRUNE) anywhere on the map flips this on.
			Map& map = game->map;
			const int w = map.getW();
			const int h = map.getH();
			for (int x = 0; x < w && obs.fruitOnMap == 0; x++)
				for (int y = 0; y < h; y++)
					if (map.isRessourceTakeable(x, y, CHERRY)
					 || map.isRessourceTakeable(x, y, ORANGE)
					 || map.isRessourceTakeable(x, y, PRUNE))
					{
						obs.fruitOnMap = 1;
						break;
					}

			// candidate build locations for the building types the economy
			// phase reasons about. Other types keep valid==0 from the empty
			// observation. placeCandidates writes exactly CORTEX_BUILD_CANDIDATES
			// slots (zero-filling unused trailing ones).
			placeCandidates(game, team, IntBuildingType::FOOD_BUILDING,    0, obs.buildCandidates[IntBuildingType::FOOD_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SWARM_BUILDING,   0, obs.buildCandidates[IntBuildingType::SWARM_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SCIENCE_BUILDING, 0, obs.buildCandidates[IntBuildingType::SCIENCE_BUILDING]);
			placeCandidates(game, team, IntBuildingType::ATTACK_BUILDING,  0, obs.buildCandidates[IntBuildingType::ATTACK_BUILDING]);
		}

		// --- opponents ---
		// Fairness: the engine grants AIs unfogged access to enemy state, so we
		// must NOT copy enemy unit/building/prestige counts here — that would be
		// a fog-of-war cheat baked into the observation surface (see
		// AIImplementation.h and docs/AI/cortex/README.md). For now we expose
		// only which enemy teams exist and are alive (public, shown in the UI);
		// the intel fields stay zeroed until the scouting/visibility-gating pass
		// lands and can fill them from what we've actually seen.
		if (game != NULL)
		{
			int slot = 0;
			for (int i = 0; i < game->teamsCount() && slot < MAX_ENEMY_SLOTS; i++)
			{
				Team* other = game->teams[i];
				if (other == NULL)
					continue;
				const bool isEnemy = (team->enemies & other->me) != 0;
				if (!isEnemy || !other->isAlive)
					continue;

				EnemySlot& es = obs.enemies[slot];
				es.active = 1;
				es.teamNumber = other->teamNumber;
				es.totalUnit = 0;     // fog-gated; filled by later scouting pass.
				es.totalBuilding = 0; // fog-gated; filled by later scouting pass.
				es.prestige = 0;      // fog-gated; filled by later scouting pass.
				slot++;
			}
			obs.enemyCount = slot;
		}

		obs.valid = 1;
		return obs;
	}
}
