// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexObservation.h"
#include "CortexPlacement.h"
#include "CortexPlacementGeo.h"

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
	void observeBuildings(CortexObservation& obs, Team* team, Game* game,
		int maxBuildLevel, Uint16 offenseFlagGid, bool& warFlagFound,
		Sint32& warFlagX, Sint32& warFlagY, Sint32& warFlagRange)
	{
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
			//
			// An inn with no harvestable wheat in reach cannot be restocked with corn,
			// so it keeps nobody fed — counting its slot throughput here would inflate
			// feedCapacity and suppress the second-inn build gate (Priority 2), leaving
			// the colony short of real feeding capacity. Gate the contribution on the
			// SAME wheat test inn placement uses (CortexPlacement.cpp: at least
			// CORTEX_WHEAT_MIN_TILES wheat tiles within CORTEX_WHEAT_MIN_TILES_RADIUS of
			// the footprint), measured as the SURVIVING (open-parity) corn the protection
			// checkerboard leaves harvestable — NOT the live non-forbidden count. The live
			// count reads ~zero on a fully-checkerboarded field even when its open half
			// feeds fine, so it conflated "field drained" (genuinely can't feed) with
			// "field checkerboarded" (still feeds from the open half) and collapsed
			// feedCapacity to 0 mid-game, making the inn-build gate fire forever (the Muka
			// inn-spam spiral). The surviving count drops to 0 only on real depletion.
			// game==NULL (no-map test path): can't measure, so fall back to counting it.
			if (b->maxUnitWorking && bt->canFeedUnit)
			{
				const bool innHasWheat = (game == NULL)
					|| Cortex::countSurvivingCornWithin(game->map,
					                                    b->posX, b->posY,
					                                    bt->width, bt->height,
					                                    CORTEX_WHEAT_MIN_TILES_RADIUS)
					   >= CORTEX_WHEAT_MIN_TILES;
				if (innHasWheat)
					obs.feedCapacity += Cortex::cortexInnUnitSupport(
						bt->maxUnitInside, bt->timeToFeedUnit);
			}
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
					// Harvestable-wheat count in the swarm's catchment — the input to the
					// wheat-starved worker throttle (CortexPolicy Priority 1.5). Counts
					// non-forbidden CORN within CORTEX_SWARM_WHEAT_STARVED_RADIUS of the
					// footprint, so it tracks the field draining/being checkerboarded over
					// time, not just the spot the swarm was built on.
					t.harvestableWheatNearby = (game != NULL)
						? Cortex::countHarvestableCornWithin(game->map, team->me,
						                                     b->posX, b->posY,
						                                     bt->width, bt->height,
						                                     CORTEX_SWARM_WHEAT_STARVED_RADIUS)
						: -1;
					t.restockTripsNeeded = -1; // inn-only hauler-ceiling signal; unused for swarms.
					// C++: Building::priority (-1/0/+1), building/Building.h:516
					t.priority        = b->priority;
					t.ticksSinceFinished = -1; // swarms do not use the inn tune-cooldown.
					t.diagBlindCornNearby = -1; // inn-only diagnostic; unused for swarms.
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
					// DIAGNOSTIC (Phase-1 feedCap root-cause): the EXACT quantity the
					// feedCapacity gate tests for this inn — count of non-forbidden CORN
					// tiles within CORTEX_WHEAT_MIN_TILES_RADIUS of the footprint. An inn
					// contributes to feedCapacity iff this is >= CORTEX_WHEAT_MIN_TILES.
					// Paired with nearestWheatDist (forbidden-BLIND) this discriminates
					// (b) corn-present-but-forbidden from (c) corn-depleted/absent. No
					// policy reads inn harvestableWheatNearby (verified swarm-only), so
					// this is purely a trace signal. -1 when game absent (no map).
					t.harvestableWheatNearby = (game != NULL)
						? Cortex::countHarvestableCornWithin(game->map, team->me,
						                                     b->posX, b->posY,
						                                     bt->width, bt->height,
						                                     CORTEX_WHEAT_MIN_TILES_RADIUS)
						: -1;
					// Forbidden-BLIND corn count over the SAME box: (blind - harvestable)
					// is the forbidden-but-present corn. blind>=MIN & harvestable<MIN =>
					// checkerboard-forbidding (b); blind<MIN => field depleted/absent (c).
					t.diagBlindCornNearby = (game != NULL)
						? Cortex::countCornWithin(game->map, b->posX, b->posY,
						                          bt->width, bt->height,
						                          CORTEX_WHEAT_MIN_TILES_RADIUS)
						: -1;
					// Restock demand (the inn-hauler ceiling, CortexPolicy Priority 1.5):
					// the inn's CORN deficit expressed in HAULER TRIPS. Corn is the feed
					// resource that limits how many units the inn sustains, so the hauler
					// count tracks how empty the corn buffer is; fruit is happiness garnish
					// and does not drive feeding, so it is deliberately excluded. One trip
					// delivers multiplierRessource[CORN] units, so divide the deficit by it.
					//
					// We do NOT gate on Map::ressourceAvailable here: it reads the team
					// resource gradient at (posX, posY), but updateRessourcesGradient marks
					// every building-occupied tile GRADIENT_FORBIDDEN (MapGradientGlobal.cpp
					// :141), so probing the inn's OWN footprint corner always returned false
					// and zeroed the deficit — pinning every inn to one hauler. "No corn in
					// reach" is instead handled coarsely in the policy via nearestWheatDist
					// (CORTEX_INN_WHEAT_STARVED_RADIUS). C++: maxRessource/multiplierRessource
					// game/entities/BuildingType.h:76,78.
					if (game != NULL)
					{
						const int cornDeficit = bt->maxRessource[CORN] - b->ressources[CORN];
						if (cornDeficit > 0)
						{
							const int mult = (bt->multiplierRessource[CORN] > 0)
								? bt->multiplierRessource[CORN] : 1;
							t.restockTripsNeeded = cornDeficit / mult;
						}
						else
							t.restockTripsNeeded = 0;
					}
					else
						t.restockTripsNeeded = -1;
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
				// Remember the OFFENSE flag's footprint (gid match) so the enemy-unit
				// pass can detect stragglers still loitering inside its attraction radius
				// AND the own-units pass can count our warriors present at the front.
				// Cortex runs two flags now (offense + defense); capturing by gid keeps
				// these front-line signals tied to the offense push the retire/retreat
				// decisions reason about, rather than whichever flag was scanned last.
				// C++: Building::gid building/Building.h:514; posX/posY building/Building.h:523;
				//      unitStayRange building/Building.h:530
				if (offenseFlagGid != NOGBID && b->gid == offenseFlagGid)
				{
					warFlagFound = true;
					warFlagX     = b->posX;
					warFlagY     = b->posY;
					warFlagRange = b->unitStayRange;
				}
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
					s.priority       = b->priority; // C++: building/Building.h:516
					obs.siteCount++;
				}
			}
		}
	}
}
