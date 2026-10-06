#include "Material.h"
#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AITelemetryFields.h"
#include "AICastor.h"
#include "Game.h"
#include <algorithm>
#include <span>
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;
namespace {
// Preserve this simple strategy's bounded progression preferences while
// following the catalog's actual transition lineage, independent of labels.
int strategicStage(const Game& game,const Building& building)
{
    return std::clamp(game.buildingCapabilities().lineagePosition(building.typeNum)-1,0,NB_UNIT_LEVELS-1);
}
}

std::shared_ptr<Order>AICastor::controlSwarms()
{
	telemetry.count(AITrace::AI2::AICastor_controlSwarms_calls);
	Sint32 warriorGoal=game->gameHeader.isPeacefulModeEnabled() ? 0 : warLevel;
	
	int unitSum[NB_UNIT_TYPE];
	for (int i=0; i<NB_UNIT_TYPE; i++)
		unitSum[i]=0;
	Unit **myUnits=team->myUnits;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u)
			unitSum[u->typeNum]++;
	}
	int foodSum=0;
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if (b && b->maxUnitWorking && b->type->canFeedUnit)
			foodSum+=b->type->maxUnitInside;
	}
	
	int unitSumAll=unitSum[0]+unitSum[1]+unitSum[2];

	foodWarning=((unitSumAll+AI_CASTOR_FOODWARN_OFFSET)>=(foodSum<<1));
	foodLock=((unitSumAll+AI_CASTOR_FOODLOCK_OFFSET)>=(foodSum<<1));
	// No hunger removes feeding pressure, but swarms still need wheat to produce.
	if (game->gameHeader.isHungerDisabled())
	{ foodLock=false; foodWarning=false; }
	foodLockStats[foodLock]++;

	foodSurplus=game->gameHeader.isHungerDisabled() || (unitSumAll+AI_CASTOR_FOODSURPLUS_OFFSET<foodSum);

	starvingWarning=(((unitSumAll>>AI_CASTOR_STARVING_RATIO_SHIFT)+AI_CASTOR_STARVING_OFFSET)<team->stats.getStarvingUnits());
	if (game->gameHeader.isHungerDisabled()) starvingWarning=false;
	starvingWarningStats[starvingWarning]++;

	bool realFoodLock;

	if (warriorGoal>1)
		realFoodLock=((unitSumAll)>=(foodSum*AI_CASTOR_REAL_FOODLOCK_MULT_WAR));
	else
		realFoodLock=((unitSumAll)>=(foodSum*AI_CASTOR_REAL_FOODLOCK_MULT_PEACE));

	if (!game->gameHeader.isHungerDisabled() && (timer>AI_CASTOR_FOODLOCK_GRACE_TICKS) && (realFoodLock || starvingWarning || starvingWarningStats[1]>starvingWarningStats[0]))
	{
		// Stop making any units!
		Building **myBuildings=team->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			Building *b=myBuildings[bi];
			if (b && std::any_of(std::begin(b->type->semantics.production.recipes), std::end(b->type->semantics.production.recipes), [](const auto& r) { return r.enabled; }))
				for (int ri=0; ri<NB_UNIT_TYPE; ri++)
					if (b->ratio[ri]!=0)
					{
						// Zero out the authoritative ratios; build a stack
						// buffer for the order payload. The per-viewer GUI
						// shadow that used to live as b->ratioLocal is now
						// in BuildingGuiState and off-limits to AI code.
						Sint32 newRatio[NB_UNIT_TYPE];
						for (int rj=0; rj<NB_UNIT_TYPE; rj++)
						{
							b->ratio[rj]=0;
							newRatio[rj]=0;
						}
						b->update();
						return telemetry.returnedOrder(
							AITrace::AI2::AICastor_controlSwarms_result,
							shared_ptr<Order>(new OrderModifySwarm(b->gid, newRatio)));
					}
		}

		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlSwarms_result,
									   shared_ptr<Order>());
	}
	
	size_t size=map->w*map->h;
	int discovered=0;
	int seeable=0;
	Uint32 *mapDiscovered=&(map->mapDiscovered[0]);
	Uint32 *fogOfWar=&map->fogOfWar[0];
	Uint32 me=team->me;
	for (size_t i=0; i<size; i++)
	{
		if (((mapDiscovered[i]) & me)!=0)
			discovered++;
		if (((fogOfWar[i]) & me)!=0)
			seeable++;
	}
	Sint32 explorerGoal;
	if (unitSum[WORKER]<AI_CASTOR_EXPLORER_MIN_WORKERS)
		explorerGoal=0;
	else if (unitSum[EXPLORER]==0)
		explorerGoal=AI_CASTOR_EXPLORER_GOAL_HIGH;
	else if (unitSum[EXPLORER]<AI_CASTOR_EXPLORER_COUNT_TARGET && (unitSum[EXPLORER]<<AI_CASTOR_EXPLORER_RATIO_SHIFT_EARLY)<unitSum[WORKER] && (discovered+seeable<((int)size<<AI_CASTOR_DISCOVERY_RATIO_SHIFT)))
		explorerGoal=AI_CASTOR_EXPLORER_GOAL_HIGH;
	else if ((unitSum[EXPLORER]<<AI_CASTOR_EXPLORER_RATIO_SHIFT_LATE)<unitSum[WORKER])
		explorerGoal=AI_CASTOR_EXPLORER_GOAL_LOW;
	else
		explorerGoal=0;

	Sint32 workerGoal;
	if (overWorkers)
		workerGoal=AI_CASTOR_WORKER_GOAL_LOW;
	else
		workerGoal=AI_CASTOR_WORKER_GOAL_HIGH;

    if(auto order=AIPlanning::missingProductionOrder(*game,*team,{workerGoal,explorerGoal,warriorGoal},4,4)) return order;

	for (int bi=0; bi<Building::MAX_COUNT; bi++)
	{
		Building *b=myBuildings[bi];
		if (b && std::any_of(std::begin(b->type->semantics.production.recipes), std::end(b->type->semantics.production.recipes), [](const auto& r) { return r.enabled; }))
		{
   Sint32 desired[NB_UNIT_TYPE] = {workerGoal,explorerGoal,warriorGoal};
   bool differs = false;
   for (int unit=0; unit<NB_UNIT_TYPE; ++unit) {
    if (!b->type->semantics.production.recipes[unit].enabled) desired[unit]=0;
    differs |= b->ratio[unit] != desired[unit];
   }
   if (differs) return telemetry.returnedOrder(AITrace::AI2::AICastor_controlSwarms_result,
    std::make_shared<OrderModifySwarm>(b->gid,desired));
		}
	}

	return telemetry.returnedOrder(AITrace::AI2::AICastor_controlSwarms_result,
								   shared_ptr<Order>());
}

std::shared_ptr<Order>AICastor::expandFood()
{
	// Feeding capacity cannot constrain production when units never need meals.
	if (game->gameHeader.isHungerDisabled()) return {};
	telemetry.count(AITrace::AI2::AICastor_expandFood_calls);
	if (foodSurplus
		|| (!foodWarning && !enoughFreeWorkers())
		|| buildingSum[AICastor::FeedUnits][1]>buildingSum[AICastor::FeedUnits][0]+1)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_expandFood_result,
									   shared_ptr<Order>());

	Sint32 typeNum=selectBuilding(FeedUnits);
 if (typeNum < 0) return {};
	int bw=game->buildingsTypes.get(typeNum)->width;
	int bh=game->buildingsTypes.get(typeNum)->height;

	
	computeCanSwim();
	computeObstacleBuildingMap();
	computeSpaceForBuildingMap(std::max(bw,bh));
	computeBuildingNeighbourMap(bw, bh);
	computeObstacleUnitMap();
	computeWheatGrowthMap();
	computeObstacleUnitMap();
	computeWorkPowerMap();
	computeWorkRangeMap();
	computeWorkAbilityMap();

	return telemetry.returnedOrder(AITrace::AI2::AICastor_expandFood_result,
								   findGoodBuilding(typeNum, true, false, false));
}

std::shared_ptr<Order>AICastor::controlFood()
{
	// Feeding capacity cannot constrain production when units never need meals.
	if (game->gameHeader.isHungerDisabled()) return {};
	telemetry.count(AITrace::AI2::AICastor_controlFood_calls);
	int wMask=map->wMask;
	int hMask=map->hMask;
	int wDec=map->wDec;
	
	int bi=(controlFoodTimer++)&(Building::MAX_COUNT-1);
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[bi];
	for (int i=0; i<AI_CASTOR_CONTROL_FOOD_RETRIES; i++)
		if (b==NULL)
		{
			bi=(controlFoodTimer++)&(Building::MAX_COUNT-1);
			b=myBuildings[bi];
		}
	if (b==NULL)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
									   shared_ptr<Order>());
	if (!provides(*b, AICastor::FeedUnits) && !(game->buildingCapabilities().intentMask(b->type->isBuildingSite ? b->type->nextLevel : b->typeNum)&7u))
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
									   shared_ptr<Order>());

 const auto& semantics = b->type->semantics;
 bool usesWheat = semantics.feeding.enabled && semantics.feeding.cost[materialIndex(MaterialId::Food)] > 0;
 bool otherService = semantics.healing.enabled || b->type->shootingRange > 0;
 for (const auto& training : semantics.training) otherService |= training.enabled;
 for (int resource=0; resource<MaterialSlotCount; ++resource) {
  if (semantics.feeding.enabled && resource != materialIndex(MaterialId::Food) && semantics.feeding.cost[resource] > 0) otherService=true;
  for (const auto& recipe : semantics.production.recipes) if (recipe.enabled) {
   usesWheat |= recipe.cost[materialIndex(MaterialId::Food)] > 0;
   otherService |= resource != materialIndex(MaterialId::Food) && recipe.cost[resource] > 0;
  }
 }
 if (!usesWheat || otherService) return {};

	int bx=b->posX;
	int by=b->posY;
	int bw=b->type->width;
	int bh=b->type->height;
	
	Uint8 worstCare=0;
	for (int xi=bx-1; xi<bx+bw; xi++)
	{
		Uint8 wheatCare;
		wheatCare=wheatCareMap[0][(xi&wMask)+(((by-1)&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
		wheatCare=wheatCareMap[0][(xi&wMask)+(((by+bh)&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
	}
	for (int yi=by; yi<=by+bh; yi++)
	{
		Uint8 wheatCare;
		wheatCare=wheatCareMap[0][((bx-1)&wMask)+((yi&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
		wheatCare=wheatCareMap[0][((bx+bw)&wMask)+((yi&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
	}
	
	// Sparse wheat normally needs a recovery pause. With no regrowth, waiting
	// cannot improve this catchment; keep harvesting its remaining finite stock.
	if (!game->gameHeader.isResourceGrowthDisabled() && worstCare>AI_CASTOR_WHEATCARE_STOP_THRESHOLD)
	{
		if (b->maxUnitWorking!=0)
		{
			b->maxUnitWorking=0;
			b->update();
			if (verbose)
				printf("controlFood(), worstCare=%d\n", worstCare);
			return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
										   shared_ptr<Order>(new OrderModifyBuilding(b->gid, 0)));
		}
	}
	else if (!game->gameHeader.isResourceGrowthDisabled() && worstCare>AI_CASTOR_WHEATCARE_LIMIT_THRESHOLD)
	{
		if (b->maxUnitWorking>1)
		{
			b->maxUnitWorking=1;
			b->update();
			if (verbose)
				printf("controlFood(), beta, worstCare=%d\n", worstCare);
			return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
										   shared_ptr<Order>(new OrderModifyBuilding(b->gid, 1)));
		}
	}
	else
	{
		if (provides(*b, AICastor::FeedUnits))
		{
			Sint32 workers;
			if (foodWarning && b->type->isBuildingSite)
				workers=AI_CASTOR_FOODWARN_INN_SITE_WORKERS+strategicStage(*game,*b); //TODO: random 2 or 3
			else
				workers=AI_CASTOR_INN_WORKERS_BASE+strategicStage(*game,*b);
			workers=desiredWorkers(*b,workers);
			b->maxUnitWorking=workers;
			b->update();
			return telemetry.returnedOrder(
				AITrace::AI2::AICastor_controlFood_result,
				shared_ptr<Order>(new OrderModifyBuilding(b->gid, workers)));
		}
		else if (game->buildingCapabilities().intentMask(b->type->isBuildingSite ? b->type->nextLevel : b->typeNum)&7u)
		{
			Sint32 workers;
			if (foodWarning)
				workers=AI_CASTOR_SWARM_WORKERS_FOODWARN;
			else
				workers=AI_CASTOR_SWARM_WORKERS_NORMAL;
			workers=desiredWorkers(*b,workers);
			b->maxUnitWorking=workers;
			b->update();
			return telemetry.returnedOrder(
				AITrace::AI2::AICastor_controlFood_result,
				shared_ptr<Order>(new OrderModifyBuilding(b->gid, workers)));
		}
		else
			assert(false);
	}
	return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result, shared_ptr<Order>());
}

std::shared_ptr<Order>AICastor::controlUpgrades()
{
	telemetry.count(AITrace::AI2::AICastor_controlUpgrades_calls);
	if (controlUpgradeDelay!=0)
	{
		controlUpgradeDelay--;
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	}
	if (buildsAmount<1 || !enoughFreeWorkers())
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	int bi=((controlUpgradeTimer++)&(Building::MAX_COUNT-1));
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[bi];
	if (b==NULL)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	const bool repairing=b->hp<b->getEffectiveMaxHp() && b->type->semantics.repairable;
	if (b->type->isBuildingSite || (!repairing && (game->gameHeader.isUnitUpgradesDisabled() || !b->isUpgradeAvailable()))) return {};
	if (b->maxUnitWorking<1 && b->type->semantics.assignmentLimit>0)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>(new OrderModifyBuilding(b->gid, 1)));
	int numberOfFreeWorkers = team->stats.getLatestStat()->isFree[WORKER];
	const int transition=repairing ? b->type->prevLevel : b->type->nextLevel;
	const int qualification=transition>=0 ? game->buildingsTypes.get(transition)->semantics.requiredWorkerLevel
		: b->type->semantics.requiredWorkerLevel;
	int numberOfAbleWorkers=0;
	for(int level=qualification;level<NB_UNIT_LEVELS;++level)
		numberOfAbleWorkers+=team->stats.getLatestStat()->workersByConstructionLevel[level];
	if (numberOfAbleWorkers <= AI_CASTOR_UPGRADE_MIN_ABLE_WORKERS
		|| numberOfFreeWorkers <= AI_CASTOR_UPGRADE_MIN_FREE_WORKERS
		|| numberOfAbleWorkers <= (numberOfFreeWorkers/AI_CASTOR_UPGRADE_ABLE_FREE_RATIO_DIV))
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	// Is it any repair:
	if (!b->type->isBuildingSite && b->type->semantics.repairable)
	{
		if (provides(*b, DefendWithProjectiles))
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->getEffectiveMaxHp()*AI_CASTOR_REPAIR_HP_RATIO_DEFENCE_NUM)
				return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
											   AIRules::constructionOrder(*game, *b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												   AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
		else if (b->type->maxUnitInside)
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->getEffectiveMaxHp()*AI_CASTOR_REPAIR_HP_RATIO_INSIDE_NUM)
				return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
											   AIRules::constructionOrder(*game, *b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												   AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
		else
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->getEffectiveMaxHp()*AI_CASTOR_REPAIR_HP_RATIO_OTHER_NUM)
				return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
											   AIRules::constructionOrder(*game, *b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												   AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
	}
	// Repairs above remain useful even when upgrades are disabled.
	if (game->gameHeader.isUnitUpgradesDisabled() || !b->isUpgradeAvailable()) return {};
	// Do we want to upgrade it:
	// We compute the number of buildings satifying the strategy:
	int demand = -1;
 for (int candidate=0; candidate<NB_HARD_BUILDING; ++candidate)
  if (provides(*b,candidate) && strategy.build[candidate].baseUpgrade > 0) { demand=candidate; break; }
 if (demand<0)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	int level=strategicStage(*game,*b);
	int upgradeLevelGoal=((buildsAmount+AI_CASTOR_UPGRADE_LEVEL_FORMULA_BIAS)>>AI_CASTOR_UPGRADE_LEVEL_FORMULA_SHIFT);
	if (upgradeLevelGoal>AI_CASTOR_UPGRADE_LEVEL_MAX)
		upgradeLevelGoal=AI_CASTOR_UPGRADE_LEVEL_MAX;
	if (level>=upgradeLevelGoal)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	int sumOver=0;
	for (int li=(level+1); li<NB_UNIT_LEVELS; li++)
		for (int si=0; si<2; si++)
			sumOver+=buildingLevels[demand][si][li];
	
	int upgradeAmountGoal=strategy.build[demand].baseUpgrade;
	for (int ai=1; ai<=upgradeLevelGoal; ai++)
		upgradeAmountGoal+=strategy.build[demand].newUpgrade;

	if (sumOver>=upgradeAmountGoal)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());

	if (demand==AICastor::TrainConstruction)
	{
		int buildBase=team->stats.getWorkersLevel(0);
		int buildSum=0;
		for (int i=0; i<NB_UNIT_LEVELS; i++)
			buildSum+=team->stats.getWorkersLevel(i);
		if (buildBase>buildSum)
			return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
										   shared_ptr<Order>());
		int sumEqual=0;
		for (int li=level; li<NB_UNIT_LEVELS; li++)
			sumEqual+=buildingLevels[demand][0][li];
		if (sumEqual<AI_CASTOR_SCIENCE_UPGRADE_MIN_COUNT)
		{
			return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
										   shared_ptr<Order>());
		}
	}
	controlUpgradeDelay=AI_CASTOR_UPGRADE_DELAY_TICKS;
	return telemetry.returnedOrder(
		AITrace::AI2::AICastor_controlUpgrades_result,
		AIRules::constructionOrder(*game, *b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
}




std::shared_ptr<Order>AICastor::controlStrikes()
{
	// Combat cannot damage opponents here; military work must not reserve economic labour.
	if (game->gameHeader.isPeacefulModeEnabled()) return {};
	telemetry.count(AITrace::AI2::AICastor_controlStrikes_calls);
	controlStrikesTimer=timer+AI_CASTOR_CONTROL_STRIKES_INTERVAL;

	if (!onStrike)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
									   shared_ptr<Order>());

	int warriors=team->stats.getTotalUnits(WARRIOR);
	int warFlagsGoal=(warriors+AI_CASTOR_WARFLAG_FORMULA_BIAS)/AI_CASTOR_WARRIORS_PER_WARFLAG;
	int warFlagsReal=buildingSum[AICastor::AttractWarriors][0];

	if (!strikeTeamSelected)
	{
		int bestLevel=AI_CASTOR_LEVEL_NONE;
		for (int ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
		{
			Team *enemyTeam=game->teams[ti];
			Uint32 me=team->me;
			if ((team->attackableTeams()&enemyTeam->me)==0)
				continue;
			Building **enemyBuildings=enemyTeam->myBuildings;
			for (int bi=0; bi<Building::MAX_COUNT; bi++)
			{
				Building *b=enemyBuildings[bi];
				if (b==NULL || ((b->seenByMask&me)==0) || b->locked[canSwim])
					continue;
				int level=strategicStage(*game,*b);
				if (bestLevel<level)
					bestLevel=level;
			}
		}
		int bestTeam=0;
		int bestScore=AI_CASTOR_SCORE_NONE;
		for (int ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
		{
			int score=0;
			Team *enemyTeam=game->teams[ti];
			Uint32 me=team->me;
			if ((team->attackableTeams()&enemyTeam->me)==0)
				continue;
			Building **enemyBuildings=enemyTeam->myBuildings;
			for (int bi=0; bi<Building::MAX_COUNT; bi++)
			{
				Building *b=enemyBuildings[bi];
				if (b==NULL || ((b->seenByMask&me)==0) || b->locked[canSwim] || strategicStage(*game,*b)<bestLevel)
					continue;
				if (provides(*b, TrainAttack) || provides(*b, TrainConstruction))
					score+=AI_CASTOR_STRIKE_TEAM_SCORE_HIGH;
				else
					score+=AI_CASTOR_STRIKE_TEAM_SCORE_LOW;
			}
			if (bestScore<score)
			{
				bestScore=score;
				bestTeam=ti;
			}
		}
		strikeTeam=bestTeam;
		strikeTeamSelected=true;
	}

	// We choose the best buildings to attack:
	
	int wMask=map->wMask;
	int hMask=map->hMask;
	int wDec=map->wDec;
	
	Uint32 bestScore=0;
	Building *bestBuilding=NULL;
	Team *enemyTeam=game->teams[strikeTeam];
	Uint32 me=team->me;
	Building **enemyBuildings=enemyTeam->myBuildings;
	for (int bi=0; bi<Building::MAX_COUNT; bi++)
	{
		Building *b=enemyBuildings[bi];
		if (b==NULL || ((b->seenByMask&me)==0) || b->locked[canSwim])
			continue;
		int x=b->posX;
		int y=b->posY;
		size_t index=(x&wMask)+((y&hMask)<<wDec);
		Uint8 workRange=workRangeMap[index];
		Sint32 level=strategicStage(*game,*b);
		Uint32 score=(AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS+workRange)*(AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS+level);
		if (b->type->isBuildingSite)
			score=(score>>AI_CASTOR_STRIKE_BUILDING_SITE_SHIFT);
		if (provides(*b, TrainAttack) || provides(*b, TrainConstruction))
			score=(score<<AI_CASTOR_STRIKE_HIGH_VALUE_SHIFT);
		if (bestScore<score)
		{
			bestScore=score;
			bestBuilding=b;
		}
	}
	
	std::list<Building *> rallyBuildings;
 for (auto* candidate : std::span<Building*>(team->myBuildings,Building::MAX_COUNT))
  if (candidate && provides(*candidate,AttractWarriors)) rallyBuildings.push_back(candidate);
 auto* virtualBuildings=&rallyBuildings;
	if (bestBuilding!=NULL)
	{
		Sint32 x=bestBuilding->posX+1;
		Sint32 y=bestBuilding->posY+1;

		if (warFlagsReal<warFlagsGoal)
		{
			Sint32 typeNum=selectBuilding(AttractWarriors);
   if (typeNum < 0) return {};
   bool place=false;
   for (int radius=0; radius<=8 && !place; ++radius)
    for (int dx=-radius; dx<=radius && !place; ++dx)
     for (int dy=-radius; dy<=radius; ++dy)
      if (game->checkRoomForBuilding(x+dx,y+dy,game->buildingsTypes.get(typeNum),team->teamNumber))
       { x+=dx; y+=dy; place=true; break; }
   if (!place) return {};
			return telemetry.returnedOrder(
				AITrace::AI2::AICastor_controlStrikes_result,
				AIRules::createOrder(*game, team->teamNumber, x, y, typeNum, 1, 1));
		}
		else
		{
			Sint32 maxSqDist=0;
			Building *maxFlag=NULL;
			for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if (provides(**it, AICastor::AttractWarriors))
				{
					Sint32 dx=x-(*it)->posX;
					Sint32 dy=y-(*it)->posY;
					Sint32 sqDist=dx*dx+dy*dy;
					if (maxSqDist<sqDist)
					{
						maxSqDist=sqDist;
						maxFlag=*it;
					}
				}
			if (maxSqDist>AI_CASTOR_FLAG_MOVE_SQ_DIST && maxFlag!=NULL && maxFlag->type->semantics.relocatable)
			{
				return telemetry.returnedOrder(
					AITrace::AI2::AICastor_controlStrikes_result,
					shared_ptr<Order>(new OrderMoveFlag(maxFlag->gid, x, y, true)));
			}
			for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if (provides(**it, AICastor::AttractWarriors)
					&& (*it)->maxUnitWorking<std::min(AI_CASTOR_WARFLAG_WORKER_GOAL,(*it)->type->semantics.assignmentLimit))
				{
					return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
												   shared_ptr<Order>(new OrderModifyBuilding(
													   (*it)->gid, std::min(AI_CASTOR_WARFLAG_WORKER_GOAL,(*it)->type->semantics.assignmentLimit))));
				}
		}
	}
	else
	{
		for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
			if (provides(**it, AICastor::AttractWarriors)
    && (*it)->type->semantics.instantPlacement && !(*it)->type->semantics.occupiesGround
    && !(*it)->type->semantics.feeding.enabled && !(*it)->type->semantics.healing.enabled
    && (*it)->type->shootingRange == 0
    && !(*it)->type->semantics.market.interTeamFruitExchange
    && !(*it)->type->semantics.market.suppliesStock
    && std::none_of((*it)->type->semantics.production.recipes.begin(),(*it)->type->semantics.production.recipes.end(),[](const auto& r){return r.enabled;})
    && std::none_of((*it)->type->semantics.training.begin(),(*it)->type->semantics.training.end(),[](const auto& r){return r.enabled;}))
   {
    return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
											   shared_ptr<Order>(new OrderDelete((*it)->gid)));
			}
		strikeTeamSelected=false;
		onStrike=false;
	}

	return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
								   shared_ptr<Order>());
}



