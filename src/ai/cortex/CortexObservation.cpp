// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexObservation.h"
#include "CortexPlacement.h"

#include "Player.h"
#include "Game.h"
#include "team/Team.h"
#include "TeamStat.h"
#include "unit/UnitConsts.h"
#include "unit/Unit.h"
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

		// --- upgrade-decision signals (Phase-2 v4) ---
		// maxBuildLevel is the highest BUILD level among our workers and is the
		// engine's own gate on whether a finished building may be upgraded: a
		// building at type->level L is upgradable only when maxBuildLevel > L.
		// C++: Team::maxBuildLevel(), team/TeamRouting.cpp:245-259.
		// Cached once here; the per-building Upgradable predicate below reuses it
		// rather than re-scanning every worker per building.
		const int maxBuildLevel = team->maxBuildLevel();
		obs.maxBuildLevel = maxBuildLevel;

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
		//   warFlagsActive = count of our own live WAR_FLAG virtual buildings.
		//                    Virtual flags are registered in team->myBuildings too
		//                    (Game::addBuilding sets myBuildings[id]=b regardless of
		//                    isVirtual, Game_editor.cpp:261), so they show up in this
		//                    same index scan — no separate virtualBuildings pass.
		//                    Reading our OWN state is not a fog-of-war cheat.
		//   upgradableCount = per IntBuildingType, the count of FINISHED instances
		//                    that pass the full engine "Upgradable" predicate right
		//                    now. The predicate mirrors Echo's
		//                    (ai/echo/Conditions.cpp:112-129) and the GUI enable-gate
		//                    (gui/GameGUIInput.cpp:421-427): the building must be
		//                    ALIVE, not a site, at full HP, not already
		//                    upgrading/repairing, have a next level, clear the
		//                    maxBuildLevel gate, and its larger next-level footprint
		//                    must fit. Lets the policy ask "can I upgrade this type?"
		//                    without re-deriving the engine's spatial/hp predicates.
		obs.feedCapacity    = 0;
		obs.swarmsProducing = 0;
		obs.warFlagsActive  = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL)
				continue;
			BuildingType* bt = b->type;
			if (b->maxUnitWorking && bt->canFeedUnit)
				obs.feedCapacity += bt->maxUnitInside;
			if (bt->shortTypeNum == IntBuildingType::SWARM_BUILDING
			 && b->buildingState == Building::ALIVE
			 && !bt->isBuildingSite   // exclude swarm sites / swarms under upgrade
			 && (b->ratio[0] | b->ratio[1] | b->ratio[2]))
				obs.swarmsProducing++;
			// C++: IntBuildingType::WAR_FLAG == 9, building/IntBuildingType.h:26
			if (bt->shortTypeNum == IntBuildingType::WAR_FLAG
			 && b->buildingState == Building::ALIVE)
				obs.warFlagsActive++;

			// Upgradable predicate. All clauses must hold; ordered cheap-first so
			// the spatial isHardSpaceForBuildingSite query runs only on candidates
			// that already clear the trivial gates.
			// C++: Building::buildingState/ALIVE building/Building.h:497,98
			// C++: BuildingType::isBuildingSite game/entities/BuildingType.h:92
			// C++: BuildingType::nextLevel game/entities/BuildingType.h:107,
			//      BUILDING_LEVEL_NONE == -1 building/Building.h:32
			// C++: Building::hp building/Building.h:542, BuildingType::hpMax
			//      game/entities/BuildingType.h:85 (else launchConstruction REPAIRs)
			// C++: Building::constructionResultState/NO_CONSTRUCTION
			//      building/Building.h:498,108 (not already upgrading/repairing)
			// C++: maxBuildLevel > type->level gate gui/GameGUIInput.cpp:426,
			//      BuildingType::level game/entities/BuildingType.h:90
			// C++: Building::isHardSpaceForBuildingSite(UPGRADE) building/Update.cpp:410,
			//      Building::UPGRADE building/Building.h:110 (larger footprint fits)
			if (b->buildingState == Building::ALIVE
			 && !bt->isBuildingSite
			 && bt->nextLevel != BUILDING_LEVEL_NONE
			 && b->hp == bt->hpMax
			 && b->constructionResultState == Building::NO_CONSTRUCTION
			 && maxBuildLevel > bt->level
			 && b->isHardSpaceForBuildingSite(Building::UPGRADE))
			{
				const int idx = bt->shortTypeNum;
				if (idx >= 0 && idx < CORTEX_BUILDING_TYPES)
					obs.upgradableCount[idx]++;
			}
		}

		// training / upgrade level buckets (one slice per array)
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
		{
			obs.buildLevel[lvl]               = stat->upgradeState[BUILD][lvl];
			obs.attackSpeedLevel[lvl]         = stat->upgradeState[ATTACK_SPEED][lvl];
			// C++: ATTACK_STRENGTH == 9, unit/UnitConsts.h:22
			obs.attackStrengthLevel[lvl]      = stat->upgradeState[ATTACK_STRENGTH][lvl];
			// SWIM is 1-based in storage (index 0 == cannot swim); copied verbatim.
			obs.workerSwimLevel[lvl]          = stat->upgradeStatePerType[WORKER][SWIM][lvl];
			obs.explorerMagicGroundLevel[lvl] = stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][lvl];
		}

		// full per-type, per-long-level building histogram (verbatim mirror;
		// the long-level encoding is decoded by the cortex* helpers, not here).
		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
			for (int l = 0; l < CORTEX_BUILDING_LONG_LEVELS; l++)
				obs.buildingCountPerLevel[t][l] = stat->numberBuildingPerTypePerLevel[t][l];

		// --- defense triggers: our own entities currently taking fire ---
		// Reading our OWN units/buildings is not a fog cheat. underAttackTimer is
		// the engine's "this entity was shot recently" countdown; nonzero => under
		// attack right now. The building scan also picks defenseTarget: the friendly
		// building taking the MOST fire (highest underAttackTimer), the spot to
		// recall the army to. Iterate by index, never a std::set.
		obs.buildingsUnderAttack = 0;
		obs.unitsUnderAttack     = 0;
		Uint8 worstUnderAttack   = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			// C++: Building::underAttackTimer (Uint8), building/Building.h:526
			if (b->underAttackTimer > 0)
			{
				obs.buildingsUnderAttack++;
				// strict > so the first-seen worst wins ties (deterministic).
				if (b->underAttackTimer > worstUnderAttack)
				{
					worstUnderAttack = b->underAttackTimer;
					obs.defenseTarget.valid = 1;
					// C++: Building::posX/posY, building/Building.h:523
					obs.defenseTarget.x     = b->posX;
					obs.defenseTarget.y     = b->posY;
					obs.defenseTarget.score = b->underAttackTimer;
				}
			}
		}
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			Unit* u = team->myUnits[i];
			if (u == NULL)
				continue;
			// C++: Unit::underAttackTimer (Uint8), unit/Unit.h:241
			if (u->underAttackTimer > 0)
				obs.unitsUnderAttack++;
		}

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

			// OFFENSE targets: discovered enemy buildings, nearest-first. Filled
			// ONLY from buildings we have legitimately seen (Building::seenByMask),
			// never from unfogged truth — implemented (with the same visibility
			// gating discipline as the enemy-intel pass below) by placeFlagTargets.
			placeFlagTargets(game, team, obs.flagTargets);
		}

		// --- opponents ---
		// Fairness: the engine grants AIs unfogged access to enemy state, so we
		// must NOT copy enemy ground truth here — that would be a fog-of-war cheat
		// baked into the observation surface (see AIImplementation.h:45-48: the
		// engine does NOT fog AI reads, so gating is OUR job, and
		// docs/AI/cortex/README.md). We expose only which enemy teams exist and
		// are alive (public, shown in the UI) plus VISIBILITY-GATED intel: each
		// enemy entity is counted only if we can legitimately see it. We iterate
		// the enemy's OWN entity arrays by index (never a std::set) and gate each
		// entry — we never scan unfogged truth.
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

				// totalBuilding: enemy buildings we have DISCOVERED. seenByMask is
				// the engine's own per-team "this team has seen this building"
				// record (in the sync checksum), so it is the correct non-cheating
				// signal. team->me is our vision bit (1<<teamNumber).
				// C++: Building::seenByMask (Uint32), building/Building.h:560
				es.totalBuilding = 0;
				for (int j = 0; j < Building::MAX_COUNT; j++)
				{
					Building* b = other->myBuildings[j];
					if (b == NULL || b->buildingState == Building::DEAD)
						continue;
					if ((b->seenByMask & team->me) != 0)
						es.totalBuilding++;
				}

				// totalUnit: enemy units whose tile is CURRENTLY in our fog-of-war
				// view. We iterate the enemy's own unit array and gate each unit on
				// FOW — we do NOT scan the whole map.
				// C++: Map::isFOWDiscovered(int x,int y,int visionMask), map/Map.h:202
				es.totalUnit = 0;
				for (int j = 0; j < Unit::MAX_COUNT; j++)
				{
					Unit* u = other->myUnits[j];
					if (u == NULL)
						continue;
					// C++: Unit::posX/posY, unit/Unit.h:220
					if (game->map.isFOWDiscovered(u->posX, u->posY, team->me))
						es.totalUnit++;
				}

				es.prestige = 0; // prestige is not a visible signal; left unfilled
				                 // to avoid a fog-of-war cheat.
				slot++;
			}
			obs.enemyCount = slot;
		}

		obs.valid = 1;
		return obs;
	}
}
