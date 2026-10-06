#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AITelemetryFields.h"
#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
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
int strategicStage(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& building)
{
    return std::clamp(world.catalog->at(building.type).lineagePosition-1,0,NB_UNIT_LEVELS-1);
}
}

std::shared_ptr<Order>AICastor::controlSwarms()
{
	telemetry.count(AITrace::AI2::AICastor_controlSwarms_calls);
	Sint32 warriorGoal=observation->rules.peaceful ? 0 : warLevel;
	
	int unitSum[NB_UNIT_TYPE];
	for (int i=0; i<NB_UNIT_TYPE; i++)
		unitSum[i]=0;
	const auto& myUnits=observedTeam->myUnits;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		const AIEngine::UnitView *u=myUnits[i];
		if (u)
			unitSum[u->type]++;
	}
	int foodSum=0;
	const auto& myBuildings=observedTeam->myBuildings;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView *b=myBuildings[i];
		if (b && requestedWorkers(*b) && queries->kind(*b).resolvedType.canFeedUnit)
			foodSum+=queries->kind(*b).resolvedType.maxUnitInside;
	}
	
	int unitSumAll=unitSum[0]+unitSum[1]+unitSum[2];

	foodWarning=((unitSumAll+AI_CASTOR_FOODWARN_OFFSET)>=(foodSum<<1));
	foodLock=((unitSumAll+AI_CASTOR_FOODLOCK_OFFSET)>=(foodSum<<1));
	// No hunger removes feeding pressure, but swarms still need wheat to produce.
	if (observation->rules.hungerDisabled)
	{ foodLock=false; foodWarning=false; }
	foodLockStats[foodLock]++;

	foodSurplus=observation->rules.hungerDisabled || (unitSumAll+AI_CASTOR_FOODSURPLUS_OFFSET<foodSum);

	starvingWarning=(((unitSumAll>>AI_CASTOR_STARVING_RATIO_SHIFT)+AI_CASTOR_STARVING_OFFSET)<observedTeam->view->starving);
	if (observation->rules.hungerDisabled) starvingWarning=false;
	starvingWarningStats[starvingWarning]++;

	bool realFoodLock;

	if (warriorGoal>1)
		realFoodLock=((unitSumAll)>=(foodSum*AI_CASTOR_REAL_FOODLOCK_MULT_WAR));
	else
		realFoodLock=((unitSumAll)>=(foodSum*AI_CASTOR_REAL_FOODLOCK_MULT_PEACE));

	if (!observation->rules.hungerDisabled && (timer>AI_CASTOR_FOODLOCK_GRACE_TICKS) && (realFoodLock || starvingWarning || starvingWarningStats[1]>starvingWarningStats[0]))
	{
		// Stop making any units!
		const auto& myBuildings=observedTeam->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			const AIEngine::BuildingView *b=myBuildings[bi];
			if (b && std::any_of(std::begin(queries->kind(*b).resolvedType.semantics.production.recipes), std::end(queries->kind(*b).resolvedType.semantics.production.recipes), [](const auto& r) { return r.enabled; }))
				for (int ri=0; ri<NB_UNIT_TYPE; ri++)
					if (requestedRatio(*b,ri)!=0)
					{
						// Keep production intent private until the engine applies it.
						Sint32 newRatio[NB_UNIT_TYPE];
						for (int rj=0; rj<NB_UNIT_TYPE; rj++)
						{
							newRatio[rj]=0;
						}
						return telemetry.returnedOrder(
							AITrace::AI2::AICastor_controlSwarms_result,
							requestRatios(*b, newRatio));
					}
		}

		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlSwarms_result,
									   shared_ptr<Order>());
	}
	
	size_t size=observation->width*observation->height;
	int discovered=0;
	int seeable=0;


	Uint32 me=observedTeam->view->mask;
	for (size_t i=0; i<size; i++)
	{
		if (((observation->tiles[i].discovered) & me)!=0)
			discovered++;
		if (((observation->tiles[i].visible) & me)!=0)
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

    if(auto order=queries->missingProductionOrder({workerGoal,explorerGoal,warriorGoal},4,4)) return order;

	for (int bi=0; bi<Building::MAX_COUNT; bi++)
	{
		const AIEngine::BuildingView *b=myBuildings[bi];
		if (b && std::any_of(std::begin(queries->kind(*b).resolvedType.semantics.production.recipes), std::end(queries->kind(*b).resolvedType.semantics.production.recipes), [](const auto& r) { return r.enabled; }))
		{
   Sint32 desired[NB_UNIT_TYPE] = {workerGoal,explorerGoal,warriorGoal};
   bool differs = false;
   for (int unit=0; unit<NB_UNIT_TYPE; ++unit) {
    if (!queries->kind(*b).resolvedType.semantics.production.recipes[unit].enabled) desired[unit]=0;
    differs |= requestedRatio(*b,unit) != desired[unit];
   }
   if (differs) return telemetry.returnedOrder(AITrace::AI2::AICastor_controlSwarms_result,
    requestRatios(*b,desired));
		}
	}

	return telemetry.returnedOrder(AITrace::AI2::AICastor_controlSwarms_result,
								   shared_ptr<Order>());
}

std::shared_ptr<Order>AICastor::expandFood()
{
	// Feeding capacity cannot constrain production when units never need meals.
	if (observation->rules.hungerDisabled) return {};
	telemetry.count(AITrace::AI2::AICastor_expandFood_calls);
	if (foodSurplus
		|| (!foodWarning && !enoughFreeWorkers())
		|| buildingSum[AICastor::FeedUnits][1]>buildingSum[AICastor::FeedUnits][0]+1)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_expandFood_result,
									   shared_ptr<Order>());

	Sint32 typeNum=selectBuilding(FeedUnits);
 if (typeNum < 0) return {};
	int bw=(&queries->kind(typeNum).resolvedType)->width;
	int bh=(&queries->kind(typeNum).resolvedType)->height;

	
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
	if (observation->rules.hungerDisabled) return {};
	telemetry.count(AITrace::AI2::AICastor_controlFood_calls);
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	
	int bi=(controlFoodTimer++)&(Building::MAX_COUNT-1);
	const auto& myBuildings=observedTeam->myBuildings;
	const AIEngine::BuildingView *b=myBuildings[bi];
	for (int i=0; i<AI_CASTOR_CONTROL_FOOD_RETRIES; i++)
		if (b==NULL)
		{
			bi=(controlFoodTimer++)&(Building::MAX_COUNT-1);
			b=myBuildings[bi];
		}
	if (b==NULL)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
									   shared_ptr<Order>());
	if (!provides(*b, AICastor::FeedUnits) && !(queries->rawIntentMask(queries->kind(*b).resolvedType.isBuildingSite ? queries->kind(*b).resolvedType.nextLevel : b->type)&7u))
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
									   shared_ptr<Order>());

 const auto& semantics = queries->kind(*b).resolvedType.semantics;
 bool usesWheat = semantics.feeding.enabled && semantics.feeding.cost[WHEAT] > 0;
 bool otherService = semantics.healing.enabled || queries->kind(*b).resolvedType.shootingRange > 0;
 for (const auto& training : semantics.training) otherService |= training.enabled;
 for (int resource=0; resource<MAX_NB_RESOURCES; ++resource) {
  if (semantics.feeding.enabled && resource != WHEAT && semantics.feeding.cost[resource] > 0) otherService=true;
  for (const auto& recipe : semantics.production.recipes) if (recipe.enabled) {
   usesWheat |= recipe.cost[WHEAT] > 0;
   otherService |= resource != WHEAT && recipe.cost[resource] > 0;
  }
 }
 if (!usesWheat || otherService) return {};

	int bx=b->x;
	int by=b->y;
	int bw=queries->kind(*b).resolvedType.width;
	int bh=queries->kind(*b).resolvedType.height;
	
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
	if (!observation->rules.resourceGrowthDisabled && worstCare>AI_CASTOR_WHEATCARE_STOP_THRESHOLD)
	{
		if (requestedWorkers(*b)!=0)
		{
			if (verbose)
				bufferedDiagnostics.push_back({"", "", "controlFood(), worstCare=" + std::to_string(worstCare) + "\n"});
			return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
										   requestWorkers(*b, 0));
		}
	}
	else if (!observation->rules.resourceGrowthDisabled && worstCare>AI_CASTOR_WHEATCARE_LIMIT_THRESHOLD)
	{
		if (requestedWorkers(*b)>1)
		{
			if (verbose)
				bufferedDiagnostics.push_back({"", "", "controlFood(), beta, worstCare=" + std::to_string(worstCare) + "\n"});
			return telemetry.returnedOrder(AITrace::AI2::AICastor_controlFood_result,
										   requestWorkers(*b, 1));
		}
	}
	else
	{
		if (provides(*b, AICastor::FeedUnits))
		{
			Sint32 workers;
			if (foodWarning && queries->kind(*b).resolvedType.isBuildingSite)
				workers=AI_CASTOR_FOODWARN_INN_SITE_WORKERS+strategicStage(*observation,*b); //TODO: random 2 or 3
			else
				workers=AI_CASTOR_INN_WORKERS_BASE+strategicStage(*observation,*b);
			workers=desiredWorkers(*b,workers);
			return telemetry.returnedOrder(
				AITrace::AI2::AICastor_controlFood_result,
				requestWorkers(*b, workers));
		}
		else if (queries->rawIntentMask(queries->kind(*b).resolvedType.isBuildingSite ? queries->kind(*b).resolvedType.nextLevel : b->type)&7u)
		{
			Sint32 workers;
			if (foodWarning)
				workers=AI_CASTOR_SWARM_WORKERS_FOODWARN;
			else
				workers=AI_CASTOR_SWARM_WORKERS_NORMAL;
			workers=desiredWorkers(*b,workers);
			return telemetry.returnedOrder(
				AITrace::AI2::AICastor_controlFood_result,
				requestWorkers(*b, workers));
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
	const auto& myBuildings=observedTeam->myBuildings;
	const AIEngine::BuildingView *b=myBuildings[bi];
	if (b==NULL)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	const bool repairing=b->hp<b->maxHp && queries->kind(*b).resolvedType.semantics.repairable;
	if (queries->kind(*b).resolvedType.isBuildingSite || (!repairing && (observation->rules.upgradesDisabled || !b->upgradeAvailable))) return {};
	if (requestedWorkers(*b)<1 && queries->kind(*b).resolvedType.semantics.assignmentLimit>0)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   requestWorkers(*b, 1));
	int numberOfFreeWorkers = observedTeam->view->statistics.isFree[WORKER];
	const int transition=repairing ? queries->kind(*b).resolvedType.prevLevel : queries->kind(*b).resolvedType.nextLevel;
	const int qualification=transition>=0 ? (&queries->kind(transition).resolvedType)->semantics.requiredWorkerLevel
		: queries->kind(*b).resolvedType.semantics.requiredWorkerLevel;
	int numberOfAbleWorkers=0;
	for(int level=qualification;level<NB_UNIT_LEVELS;++level)
		numberOfAbleWorkers+=observedTeam->view->statistics.workersByConstructionLevel[level];
	if (numberOfAbleWorkers <= AI_CASTOR_UPGRADE_MIN_ABLE_WORKERS
		|| numberOfFreeWorkers <= AI_CASTOR_UPGRADE_MIN_FREE_WORKERS
		|| numberOfAbleWorkers <= (numberOfFreeWorkers/AI_CASTOR_UPGRADE_ABLE_FREE_RATIO_DIV))
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	// Is it any repair:
	if (!queries->kind(*b).resolvedType.isBuildingSite && queries->kind(*b).resolvedType.semantics.repairable)
	{
		if (provides(*b, DefendWithProjectiles))
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->maxHp*AI_CASTOR_REPAIR_HP_RATIO_DEFENCE_NUM)
				return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
											   queries->constructionOrder(*b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												   AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
		else if (queries->kind(*b).resolvedType.maxUnitInside)
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->maxHp*AI_CASTOR_REPAIR_HP_RATIO_INSIDE_NUM)
				return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
											   queries->constructionOrder(*b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												   AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
		else
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->maxHp*AI_CASTOR_REPAIR_HP_RATIO_OTHER_NUM)
				return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
											   queries->constructionOrder(*b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												   AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
	}
	// Repairs above remain useful even when upgrades are disabled.
	if (observation->rules.upgradesDisabled || !b->upgradeAvailable) return {};
	// Do we want to upgrade it:
	// We compute the number of buildings satifying the strategy:
	int demand = -1;
 for (int candidate=0; candidate<NB_HARD_BUILDING; ++candidate)
  if (provides(*b,candidate) && strategy.build[candidate].baseUpgrade > 0) { demand=candidate; break; }
 if (demand<0)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlUpgrades_result,
									   shared_ptr<Order>());
	int level=strategicStage(*observation,*b);
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
		int buildBase=observedTeam->view->workersLevel[0];
		int buildSum=0;
		for (int i=0; i<NB_UNIT_LEVELS; i++)
			buildSum+=observedTeam->view->workersLevel[i];
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
		queries->constructionOrder(*b, AI_CASTOR_CONSTRUCTION_ORDER_UNITS,
												AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
}




std::shared_ptr<Order>AICastor::controlStrikes()
{
	// Combat cannot damage opponents here; military work must not reserve economic labour.
	if (observation->rules.peaceful) return {};
	telemetry.count(AITrace::AI2::AICastor_controlStrikes_calls);
	controlStrikesTimer=timer+AI_CASTOR_CONTROL_STRIKES_INTERVAL;

	if (!onStrike)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
									   shared_ptr<Order>());

	int warriors=observedTeam->view->statistics.numberUnitPerType[WARRIOR];
	int warFlagsGoal=(warriors+AI_CASTOR_WARFLAG_FORMULA_BIAS)/AI_CASTOR_WARRIORS_PER_WARFLAG;
	int warFlagsReal=buildingSum[AICastor::AttractWarriors][0];

	if (!strikeTeamSelected)
	{
		int bestLevel=AI_CASTOR_LEVEL_NONE;
		for (int ti=0; ti<observation->teams.size(); ti++)
		{
			TeamObservation *enemyTeam=teamAt(ti);
			Uint32 me=observedTeam->view->mask;
			if ((observedTeam->view->enemies&enemyTeam->view->mask)==0)
				continue;
			const auto& enemyBuildings=enemyTeam->myBuildings;
			for (int bi=0; bi<Building::MAX_COUNT; bi++)
			{
				const AIEngine::BuildingView *b=enemyBuildings[bi];
				if (b==NULL || ((b->seenBy&me)==0) || b->locked[canSwim])
					continue;
				int level=strategicStage(*observation,*b);
				if (bestLevel<level)
					bestLevel=level;
			}
		}
		int bestTeam=0;
		int bestScore=AI_CASTOR_SCORE_NONE;
		for (int ti=0; ti<observation->teams.size(); ti++)
		{
			int score=0;
			TeamObservation *enemyTeam=teamAt(ti);
			Uint32 me=observedTeam->view->mask;
			if ((observedTeam->view->enemies&enemyTeam->view->mask)==0)
				continue;
			const auto& enemyBuildings=enemyTeam->myBuildings;
			for (int bi=0; bi<Building::MAX_COUNT; bi++)
			{
				const AIEngine::BuildingView *b=enemyBuildings[bi];
				if (b==NULL || ((b->seenBy&me)==0) || b->locked[canSwim] || strategicStage(*observation,*b)<bestLevel)
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
	
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	
	Uint32 bestScore=0;
	const AIEngine::BuildingView *bestBuilding=NULL;
	TeamObservation *enemyTeam=teamAt(strikeTeam);
	Uint32 me=observedTeam->view->mask;
	const auto& enemyBuildings=enemyTeam->myBuildings;
	for (int bi=0; bi<Building::MAX_COUNT; bi++)
	{
		const AIEngine::BuildingView *b=enemyBuildings[bi];
		if (b==NULL || ((b->seenBy&me)==0) || b->locked[canSwim])
			continue;
		int x=b->x;
		int y=b->y;
		size_t index=(x&wMask)+((y&hMask)<<wDec);
		Uint8 workRange=workRangeMap[index];
		Sint32 level=strategicStage(*observation,*b);
		Uint32 score=(AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS+workRange)*(AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS+level);
		if (queries->kind(*b).resolvedType.isBuildingSite)
			score=(score>>AI_CASTOR_STRIKE_BUILDING_SITE_SHIFT);
		if (provides(*b, TrainAttack) || provides(*b, TrainConstruction))
			score=(score<<AI_CASTOR_STRIKE_HIGH_VALUE_SHIFT);
		if (bestScore<score)
		{
			bestScore=score;
			bestBuilding=b;
		}
	}
	
	std::list<const AIEngine::BuildingView *> rallyBuildings;
 for (auto* candidate : observedTeam->myBuildings)
  if (candidate && provides(*candidate,AttractWarriors)) rallyBuildings.push_back(candidate);
 auto* virtualBuildings=&rallyBuildings;
	if (bestBuilding!=NULL)
	{
		Sint32 x=bestBuilding->x+1;
		Sint32 y=bestBuilding->y+1;

		if (warFlagsReal<warFlagsGoal)
		{
			Sint32 typeNum=selectBuilding(AttractWarriors);
   if (typeNum < 0) return {};
   bool place=false;
   for (int radius=0; radius<=8 && !place; ++radius)
    for (int dx=-radius; dx<=radius && !place; ++dx)
     for (int dy=-radius; dy<=radius; ++dy)
      if (queries->checkRoomForBuilding(x+dx,y+dy,typeNum,teamNumber))
       { x+=dx; y+=dy; place=true; break; }
   if (!place) return {};
			return telemetry.returnedOrder(
				AITrace::AI2::AICastor_controlStrikes_result,
				queries->createOrder(teamNumber, x, y, typeNum, 1, 1));
		}
		else
		{
			Sint32 maxSqDist=0;
			const AIEngine::BuildingView *maxFlag=NULL;
			for (std::list<const AIEngine::BuildingView *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if (provides(**it, AICastor::AttractWarriors))
				{
					Sint32 dx=x-(*it)->x;
					Sint32 dy=y-(*it)->y;
					Sint32 sqDist=dx*dx+dy*dy;
					if (maxSqDist<sqDist)
					{
						maxSqDist=sqDist;
						maxFlag=*it;
					}
				}
			if (maxSqDist>AI_CASTOR_FLAG_MOVE_SQ_DIST && maxFlag!=NULL && queries->kind(*maxFlag).resolvedType.semantics.relocatable)
			{
				return telemetry.returnedOrder(
					AITrace::AI2::AICastor_controlStrikes_result,
					shared_ptr<Order>(new OrderMoveFlag(maxFlag->identity.gid, x, y, true)));
			}
			for (std::list<const AIEngine::BuildingView *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if (provides(**it, AICastor::AttractWarriors)
					&& requestedWorkers(**it)<std::min(AI_CASTOR_WARFLAG_WORKER_GOAL,queries->kind(*(*it)).resolvedType.semantics.assignmentLimit))
				{
					return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
												   requestWorkers(**it, std::min(AI_CASTOR_WARFLAG_WORKER_GOAL,queries->kind(*(*it)).resolvedType.semantics.assignmentLimit)));
				}
		}
	}
	else
	{
		for (std::list<const AIEngine::BuildingView *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
			if (provides(**it, AICastor::AttractWarriors)
    && queries->kind(*(*it)).resolvedType.semantics.instantPlacement && !queries->kind(*(*it)).resolvedType.semantics.occupiesGround
    && !queries->kind(*(*it)).resolvedType.semantics.feeding.enabled && !queries->kind(*(*it)).resolvedType.semantics.healing.enabled
    && queries->kind(*(*it)).resolvedType.shootingRange == 0
    && !queries->kind(*(*it)).resolvedType.semantics.market.interTeamFruitExchange
    && !queries->kind(*(*it)).resolvedType.semantics.market.suppliesStock
    && std::none_of(queries->kind(*(*it)).resolvedType.semantics.production.recipes.begin(),queries->kind(*(*it)).resolvedType.semantics.production.recipes.end(),[](const auto& r){return r.enabled;})
    && std::none_of(queries->kind(*(*it)).resolvedType.semantics.training.begin(),queries->kind(*(*it)).resolvedType.semantics.training.end(),[](const auto& r){return r.enabled;}))
   {
    return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
											   shared_ptr<Order>(new OrderDelete((*it)->identity.gid)));
			}
		strikeTeamSelected=false;
		onStrike=false;
	}

	return telemetry.returnedOrder(AITrace::AI2::AICastor_controlStrikes_result,
								   shared_ptr<Order>());
}



