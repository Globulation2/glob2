// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexObservation.h"
#include "CortexPlacement.h"
#include "CortexWheat.h"
#include "CortexWater.h"

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
static_assert(Cortex::CORTEX_MAX_BUILDING_WORKERS == MAX_BUILDING_WORKER_REQUEST,
	"CORTEX_MAX_BUILDING_WORKERS must match the engine worker-request ceiling "
	"asserted in Game::executeModifyBuilding");
static_assert(Cortex::CORTEX_BUILD_SWARM == IntBuildingType::SWARM_BUILDING,
	"CORTEX_BUILD_SWARM must match IntBuildingType::SWARM_BUILDING");
static_assert(Cortex::CORTEX_BUILD_FOOD == IntBuildingType::FOOD_BUILDING,
	"CORTEX_BUILD_FOOD must match IntBuildingType::FOOD_BUILDING");
static_assert(Cortex::CORTEX_BUILD_ATTACK == IntBuildingType::ATTACK_BUILDING,
	"CORTEX_BUILD_ATTACK must match IntBuildingType::ATTACK_BUILDING");
static_assert(Cortex::CORTEX_BUILD_SCIENCE == IntBuildingType::SCIENCE_BUILDING,
	"CORTEX_BUILD_SCIENCE must match IntBuildingType::SCIENCE_BUILDING");
static_assert(Cortex::CORTEX_BUILD_WALKSPEED == IntBuildingType::WALKSPEED_BUILDING,
	"CORTEX_BUILD_WALKSPEED must match IntBuildingType::WALKSPEED_BUILDING");
static_assert(Cortex::CORTEX_BUILD_SWIMSPEED == IntBuildingType::SWIMSPEED_BUILDING,
	"CORTEX_BUILD_SWIMSPEED must match IntBuildingType::SWIMSPEED_BUILDING");

namespace Cortex
{
	CortexObservation observe(Player* player, int openMargin)
	{
		CortexObservation obs = makeEmptyObservation();

		// Echo the seeded open margin N regardless; even an early-return (no team)
		// observation carries it, and decide() ignores invalid observations anyway.
		obs.wheatOpenMargin = openMargin;

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
		obs.feedCapacity            = 0;
		obs.swarmsProducing         = 0;
		obs.swarmsProducingExplorer = 0;
		obs.swarmsProducingWarrior  = 0;
		obs.swarmsProducingWorker   = 0;
		obs.warFlagsActive          = 0;
		obs.enemyUnitsNearFlag      = 0;
		obs.swarmCount              = 0;
		obs.innCount                = 0;
		// Captured from our live war flag (if any) so the opponents pass below can
		// count enemy stragglers still inside its stay-range. Cortex only ever runs a
		// single flag; if more than one were live, the last seen wins (harmless).
		bool   warFlagFound = false;
		Sint32 warFlagX     = 0;
		Sint32 warFlagY     = 0;
		Sint32 warFlagRange = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL)
				continue;
			BuildingType* bt = b->type;
			// Feed capacity = the population this inn actually keeps fed, NOT its
			// maxUnitInside (the simultaneous-eaters count). One inn cycles many more
			// units than fit inside at once because each visit is brief — see
			// cortexInnUnitSupport / CORTEX_UNIT_WORK_TICKS_PER_FEED. Using the raw
			// slot count made the second-inn gate (Priority 2) fire at ~4 population.
			if (b->maxUnitWorking && bt->canFeedUnit)
				obs.feedCapacity += Cortex::cortexInnUnitSupport(
					bt->maxUnitInside, bt->timeToFeedUnit);
			if (bt->shortTypeNum == IntBuildingType::SWARM_BUILDING
			 && b->buildingState == Building::ALIVE
			 && !bt->isBuildingSite)  // exclude swarm sites / swarms under upgrade
			{
				if (b->ratio[0] | b->ratio[1] | b->ratio[2])
					obs.swarmsProducing++;
				// EXPLORER == unit-type index 1; lets the policy revert the one-shot
				// early-explorer mix once an explorer is actually being produced.
				if (b->ratio[EXPLORER] > 0)
					obs.swarmsProducingExplorer++;
				// WORKER == unit-type index 0; the worker-surplus throttle reads this
				// to tell whether the swarm is currently minting workers, so it can
				// stop (idle labour piling up) and resume (labour scarce) cleanly.
				if (b->ratio[WORKER] > 0)
					obs.swarmsProducingWorker++;
				// 100%-warrior swarm: WARRIOR ratio set, WORKER+EXPLORER both zero.
				// Tells the panic defense the all-warrior flip is complete.
				if (b->ratio[WARRIOR] > 0 && b->ratio[WORKER] == 0 && b->ratio[EXPLORER] == 0)
					obs.swarmsProducingWarrior++;

				// Wheat-economy tracking: record per-swarm supply signals up to the
				// bounded POD array. Buildings beyond CORTEX_MAX_TRACKED_SWARMS are
				// not individually tracked — that's intentional (bounded array).
				// C++: Building::ressources (Sint32*), building/Building.h:538
				// C++: BuildingType::maxRessource[], maxUnitWorking, maxUnitInside
				//      game/entities/BuildingType.h:76,80,79
				// C++: Building::unitsInside (std::list<Unit*>), building/Building.h:510
				// C++: nearestCornDist: Chebyshev to nearest CORN tile, ai/cortex/CortexPlacement
				// NOTE: b->ressources[CORN] is safe — for buildings with local (not
				// global) ressources it points to localRessources; for global-ressource
				// buildings it points to Team::teamRessources. The swarm is always a
				// local-ressource building, so this is the building's own wheat stock.
				if (obs.swarmCount < CORTEX_MAX_TRACKED_SWARMS)
				{
					TrackedBuilding& t = obs.trackedSwarms[obs.swarmCount];
					t.valid           = 1;
					t.gid             = b->gid;
					t.corn            = b->ressources[CORN];
					t.maxCorn         = bt->maxRessource[CORN];
					t.maxUnitWorking  = b->maxUnitWorking;
					t.unitsInside     = static_cast<Sint32>(b->unitsInside.size());
					t.maxUnitInside   = bt->maxUnitInside;
					// Only call nearestCornDist when game is available — the Map
					// reference is owned by Game and the building scan is NOT guarded
					// by (game != NULL). When game is absent, leave -1 (no result).
					t.nearestWheatDist = (game != NULL)
						? Cortex::nearestCornDist(game->map, b->posX, b->posY,
						                          CORTEX_WHEAT_SCAN_CAP)
						: -1;
					// C++: Building::priority (-1/0/+1), building/Building.h:516
					t.priority        = b->priority;
					t.ticksSinceFinished = -1; // swarms do not use the inn tune-cooldown.
					obs.swarmCount++;
				}
			}
			// Finished inns: food-supply per-building signals for the wheat-economy
			// policy (corn stock, capacity, worker slots, wheat proximity).
			// FOOD_BUILDING == IntBuildingType::FOOD_BUILDING; guarded by the same
			// ALIVE/!isBuildingSite predicate used for swarmsProducing above.
			// Buildings beyond CORTEX_MAX_TRACKED_INNS are silently not tracked.
			// C++: Building::ressources[CORN], building/Building.h:538
			// C++: BuildingType::maxRessource[CORN], maxUnitInside, maxUnitWorking
			//      game/entities/BuildingType.h:76,79,80
			// C++: Building::unitsInside (std::list<Unit*>), building/Building.h:510
			if (bt->shortTypeNum == IntBuildingType::FOOD_BUILDING
			 && b->buildingState == Building::ALIVE
			 && !bt->isBuildingSite)  // exclude inn sites / inns under upgrade
			{
				if (obs.innCount < CORTEX_MAX_TRACKED_INNS)
				{
					TrackedBuilding& t = obs.trackedInns[obs.innCount];
					t.valid           = 1;
					t.gid             = b->gid;
					t.corn            = b->ressources[CORN];
					t.maxCorn         = bt->maxRessource[CORN];
					t.maxUnitWorking  = b->maxUnitWorking;
					t.unitsInside     = static_cast<Sint32>(b->unitsInside.size());
					t.maxUnitInside   = bt->maxUnitInside;
					t.nearestWheatDist = (game != NULL)
						? Cortex::nearestCornDist(game->map, b->posX, b->posY,
						                          CORTEX_WHEAT_SCAN_CAP)
						: -1;
					t.priority        = b->priority; // C++: building/Building.h:516
					t.ticksSinceFinished = -1; // stamped post-observe by AICortex.
					obs.innCount++;
				}
			}
			// C++: IntBuildingType::WAR_FLAG == 9, building/IntBuildingType.h:26
			if (bt->shortTypeNum == IntBuildingType::WAR_FLAG
			 && b->buildingState == Building::ALIVE)
			{
				obs.warFlagsActive++;
				// Remember the flag's footprint so the enemy-unit pass can detect
				// stragglers still loitering inside its attraction radius.
				// C++: Building::posX/posY, building/Building.h:523;
				//      Building::unitStayRange, building/Building.h:530
				warFlagFound = true;
				warFlagX     = b->posX;
				warFlagY     = b->posY;
				warFlagRange = b->unitStayRange;
			}

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

			// Construction sites (new builds AND in-progress upgrades): record the
			// resource hauler-trips still needed so the policy can pour idle workers
			// into them. A delivery adds multiplierRessource[r] to ressources[r]
			// (building/Misc.cpp:178), so the trips left for resource r are
			// ceil((maxRessource[r] - ressources[r]) / multiplierRessource[r]); the
			// sum over the basic resource types bounds how many workers can usefully
			// build it. b->ressources is the site's own (local) build stock.
			// C++: BuildingType::isBuildingSite game/entities/BuildingType.h:92,
			//      maxRessource/multiplierRessource :76,78; Building::ressources :538.
			if (bt->isBuildingSite && b->buildingState == Building::ALIVE
			 && obs.siteCount < CORTEX_MAX_TRACKED_SITES)
			{
				int deliveriesLeft = 0;
				for (int r = 0; r < MAX_RESSOURCES; r++)
				{
					const int mult = bt->multiplierRessource[r];
					if (mult <= 0)
						continue;
					const int rem = bt->maxRessource[r] - b->ressources[r];
					if (rem > 0)
						deliveriesLeft += (rem + mult - 1) / mult; // ceil to whole trips.
				}
				if (deliveriesLeft > 0)
				{
					TrackedSite& s = obs.trackedSites[obs.siteCount];
					s.valid          = 1;
					s.gid            = b->gid;
					s.maxUnitWorking = b->maxUnitWorking;
					s.deliveriesLeft = deliveriesLeft;
					obs.siteCount++;
				}
			}
		}

		// training / upgrade level buckets (one slice per array)
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
		{
			obs.buildLevel[lvl]               = stat->upgradeState[BUILD][lvl];
			// WALK == 3 (unit/UnitConsts.h:13). Any-type row == workers+warriors:
			// explorers have performance[WALK]==0 at every level (game/entities/Race.cpp),
			// so they never enter this bucket. Racetrack expand-vs-upgrade gate.
			obs.walkLevel[lvl]                = stat->upgradeState[WALK][lvl];
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
			placeCandidates(game, team, IntBuildingType::HEAL_BUILDING,    0, obs.buildCandidates[IntBuildingType::HEAL_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SCIENCE_BUILDING, 0, obs.buildCandidates[IntBuildingType::SCIENCE_BUILDING]);
			placeCandidates(game, team, IntBuildingType::WALKSPEED_BUILDING, 0, obs.buildCandidates[IntBuildingType::WALKSPEED_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SWIMSPEED_BUILDING, 0, obs.buildCandidates[IntBuildingType::SWIMSPEED_BUILDING]);
			placeCandidates(game, team, IntBuildingType::ATTACK_BUILDING,  0, obs.buildCandidates[IntBuildingType::ATTACK_BUILDING]);

			// OFFENSE targets: discovered enemy buildings, nearest-first. Filled
			// ONLY from buildings we have legitimately seen (Building::seenByMask),
			// never from unfogged truth — implemented (with the same visibility
			// gating discipline as the enemy-intel pass below) by placeFlagTargets.
			placeFlagTargets(game, team, obs.flagTargets);

			// Swim/pool decision signals (algae in reach + land-vs-swim reach). The
			// reach flood-fill is the only non-trivial cost in the observation, so
			// skip it once a pool already exists or is building — the policy gates the
			// pool build on the pool count and would not build a second one anyway, so
			// the signals are only ever read while there is no pool. The building
			// histogram is populated above, so the pool count is available here.
			if (cortexFinishedBuildings(obs, CORTEX_BUILD_SWIMSPEED) == 0
			 && cortexBuildingSites(obs, CORTEX_BUILD_SWIMSPEED) == 0)
			{
				const Cortex::SwimAssessment sw = Cortex::assessSwim(player);
				obs.algaeDiscovered = sw.algaeDiscovered;
				obs.swimLandReach   = sw.landReach;
				obs.swimWaterReach  = sw.waterReach;
			}
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
					if (!game->map.isFOWDiscovered(u->posX, u->posY, team->me))
						continue;
					es.totalUnit++;
					// Straggler grace: visible enemy still inside our flag's stay-range.
					// Same warp-safe Chebyshev metric placeFlagTargets/ensureWarFlagAt use.
					if (warFlagFound
					 && game->map.warpDistMax(u->posX, u->posY, warFlagX, warFlagY) <= warFlagRange)
						obs.enemyUnitsNearFlag++;
				}

				es.prestige = 0; // prestige is not a visible signal; left unfilled
				                 // to avoid a fog-of-war cheat.
				slot++;
			}
			obs.enemyCount = slot;
		}

		// --- wheat sustainability: counts-only reconcile over the colony region ---
		// The full per-tile masks are rebuilt in the action layer (which has the
		// Map to paint into); the observation carries only the cheap diff counts so
		// the pure policy can tell whether ACTION_PROTECT_WHEAT has real work to do.
		{
			const Cortex::WheatReconcile wr =
				Cortex::reconcileWheatForbidden(player, openMargin, /*buildMasks=*/false);
			obs.wheatProtectAddCount = wr.addCount;
			obs.wheatProtectDelCount = wr.delCount;
		}

		obs.valid = 1;
		return obs;
	}
}
