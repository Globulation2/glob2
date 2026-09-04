// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

std::shared_ptr<Order>AICastor::controlSwarms()
{
	Sint32 warriorGoal=warLevel;
	
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
	foodLockStats[foodLock]++;

	foodSurplus=(unitSumAll+AI_CASTOR_FOODSURPLUS_OFFSET<foodSum);

	starvingWarning=(((unitSumAll>>AI_CASTOR_STARVING_RATIO_SHIFT)+AI_CASTOR_STARVING_OFFSET)<team->stats.getStarvingUnits());
	starvingWarningStats[starvingWarning]++;

	bool realFoodLock;

	if (warriorGoal>1)
		realFoodLock=((unitSumAll)>=(foodSum*AI_CASTOR_REAL_FOODLOCK_MULT_WAR));
	else
		realFoodLock=((unitSumAll)>=(foodSum*AI_CASTOR_REAL_FOODLOCK_MULT_PEACE));

	if ((timer>AI_CASTOR_FOODLOCK_GRACE_TICKS) && (realFoodLock || starvingWarning || starvingWarningStats[1]>starvingWarningStats[0]))
	{
		// Stop making any units!
		Building **myBuildings=team->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			Building *b=myBuildings[bi];
			if (b && b->type->unitProductionTime)
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
						return shared_ptr<Order>(new OrderModifySwarm(b->gid, newRatio));
					}
		}
		
		return shared_ptr<Order>();
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

	for (int bi=0; bi<Building::MAX_COUNT; bi++)
	{
		Building *b=myBuildings[bi];
		if (b && b->type->unitProductionTime)
		{
			if (b->ratio[EXPLORER]!=explorerGoal
				|| b->ratio[WORKER]!=workerGoal
				|| b->ratio[WARRIOR]!=warriorGoal)
			{
				b->ratio[EXPLORER]=explorerGoal;
				b->ratio[WORKER]=workerGoal;
				b->ratio[WARRIOR]=warriorGoal;
				// Stack buffer for the order payload — see also the foodlock
				// branch above. AI must not touch BuildingGuiState.
				Sint32 newRatio[NB_UNIT_TYPE];
				newRatio[EXPLORER]=explorerGoal;
				newRatio[WORKER]=workerGoal;
				newRatio[WARRIOR]=warriorGoal;
				b->update();
				return shared_ptr<Order>(new OrderModifySwarm(b->gid, newRatio));
			}
		}
	}
	
	return shared_ptr<Order>();
}

std::shared_ptr<Order>AICastor::expandFood()
{
	if (foodSurplus
		|| (!foodWarning && !enoughFreeWorkers())
		|| buildingSum[IntBuildingType::FOOD_BUILDING][1]>buildingSum[IntBuildingType::FOOD_BUILDING][0]+1)
		return shared_ptr<Order>();
	
	Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("inn", 0, true);
	int bw=globalContainer->buildingsTypes.get(typeNum)->width;
	int bh=globalContainer->buildingsTypes.get(typeNum)->height;
	assert(bw==bh);
	
	computeCanSwim();
	computeObstacleBuildingMap();
	computeSpaceForBuildingMap(bw);
	computeBuildingNeighbourMap(bw, bh);
	computeObstacleUnitMap();
	computeWheatGrowthMap();
	computeObstacleUnitMap();
	computeWorkPowerMap();
	computeWorkRangeMap();
	computeWorkAbilityMap();
	
	return findGoodBuilding(typeNum, true, false, false);
}

std::shared_ptr<Order>AICastor::controlFood()
{
	//int w=map->w;
	//int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	//size_t size=w*h;
	
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
		return shared_ptr<Order>();
	if (b->type->shortTypeNum!=IntBuildingType::FOOD_BUILDING && b->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING)
		return shared_ptr<Order>();
	
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
	
	if (worstCare>AI_CASTOR_WHEATCARE_STOP_THRESHOLD)
	{
		if (b->maxUnitWorking!=0)
		{
			b->maxUnitWorking=0;
			b->update();
			if (verbose)
				printf("controlFood(), worstCare=%d\n", worstCare);
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, 0));
		}
	}
	else if (worstCare>AI_CASTOR_WHEATCARE_LIMIT_THRESHOLD)
	{
		if (b->maxUnitWorking>1)
		{
			b->maxUnitWorking=1;
			b->update();
			if (verbose)
				printf("controlFood(), beta, worstCare=%d\n", worstCare);
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, 1));
		}
	}
	else
	{
		if (b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING)
		{
			Sint32 workers;
			if (foodWarning && b->type->isBuildingSite)
				workers=AI_CASTOR_FOODWARN_INN_SITE_WORKERS+b->type->level; //TODO: random 2 or 3
			else
				workers=AI_CASTOR_INN_WORKERS_BASE+b->type->level;
			b->maxUnitWorking=workers;
			b->update();
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, workers));
		}
		else if (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING)
		{
			Sint32 workers;
			if (foodWarning)
				workers=AI_CASTOR_SWARM_WORKERS_FOODWARN;
			else
				workers=AI_CASTOR_SWARM_WORKERS_NORMAL;
			b->maxUnitWorking=workers;
			b->update();
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, workers));
		}
		else
			assert(false);
	}
	return shared_ptr<Order>();
}

std::shared_ptr<Order>AICastor::controlUpgrades()
{
	//printf("controlUpgrades(), controlUpgradeTimer=%d, controlUpgradeDelay=%d, buildsAmount=%d\n",
	//	controlUpgradeTimer, controlUpgradeDelay, buildsAmount);
	if (controlUpgradeDelay!=0)
	{
		controlUpgradeDelay--;
		return shared_ptr<Order>();
	}
	if (buildsAmount<1 || !enoughFreeWorkers())
		return shared_ptr<Order>();
	int bi=((controlUpgradeTimer++)&(Building::MAX_COUNT-1));
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[bi];
	if (b==NULL)
		return shared_ptr<Order>();
	if (b->type->isVirtual)
		return shared_ptr<Order>();
	if (b->maxUnitWorking<1)
		return shared_ptr<Order>(new OrderModifyBuilding(b->gid, 1));
	int numberOfFreeWorkers = team->stats.getLatestStat()->isFree[WORKER];
	int numberOfAbleWorkers = team->stats.getLatestStat()->upgradeState[BUILD][b->type->level];
	if (numberOfAbleWorkers <= AI_CASTOR_UPGRADE_MIN_ABLE_WORKERS
		|| numberOfFreeWorkers <= AI_CASTOR_UPGRADE_MIN_FREE_WORKERS
		|| numberOfAbleWorkers <= (numberOfFreeWorkers/AI_CASTOR_UPGRADE_ABLE_FREE_RATIO_DIV))
		return shared_ptr<Order>();
	// Is it any repair:
	if (!b->type->isBuildingSite)
	{
		if (b->type->type == "defencetower")
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->type->hpMax*AI_CASTOR_REPAIR_HP_RATIO_DEFENCE_NUM)
				return shared_ptr<Order>(new OrderConstruction(b->gid, AI_CASTOR_CONSTRUCTION_ORDER_UNITS, AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
		else if (b->type->maxUnitInside)
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->type->hpMax*AI_CASTOR_REPAIR_HP_RATIO_INSIDE_NUM)
				return shared_ptr<Order>(new OrderConstruction(b->gid, AI_CASTOR_CONSTRUCTION_ORDER_UNITS, AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
		else
		{
			if (b->hp*AI_CASTOR_REPAIR_HP_RATIO_DIV<b->type->hpMax*AI_CASTOR_REPAIR_HP_RATIO_OTHER_NUM)
				return shared_ptr<Order>(new OrderConstruction(b->gid, AI_CASTOR_CONSTRUCTION_ORDER_UNITS, AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
		}
	}
	// Do we want to upgrade it:
	// We compute the number of buildings satifying the strategy:
	int shortTypeNum=b->type->shortTypeNum;
	if (shortTypeNum>=NB_HARD_BUILDING)
		return shared_ptr<Order>();
	int level=b->type->level;
	int upgradeLevelGoal=((buildsAmount+AI_CASTOR_UPGRADE_LEVEL_FORMULA_BIAS)>>AI_CASTOR_UPGRADE_LEVEL_FORMULA_SHIFT);
	if (upgradeLevelGoal>AI_CASTOR_UPGRADE_LEVEL_MAX)
		upgradeLevelGoal=AI_CASTOR_UPGRADE_LEVEL_MAX;
	if (level>=upgradeLevelGoal)
		return shared_ptr<Order>();
	int sumOver=0;
	for (int li=(level+1); li<NB_UNIT_LEVELS; li++)
		for (int si=0; si<2; si++)
			sumOver+=buildingLevels[shortTypeNum][si][li];
	
	int upgradeAmountGoal=strategy.build[shortTypeNum].baseUpgrade;
	for (int ai=1; ai<=upgradeLevelGoal; ai++)
		upgradeAmountGoal+=strategy.build[shortTypeNum].newUpgrade;

	if (sumOver>=upgradeAmountGoal)
		return shared_ptr<Order>();
	
	if (shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
	{
		int buildBase=team->stats.getWorkersLevel(0);
		int buildSum=0;
		for (int i=0; i<NB_UNIT_LEVELS; i++)
			buildSum+=team->stats.getWorkersLevel(i);
		if (buildBase>buildSum)
			return shared_ptr<Order>();
		int sumEqual=0;
		for (int li=level; li<NB_UNIT_LEVELS; li++)
			sumEqual+=buildingLevels[shortTypeNum][0][li];
		if (sumEqual<AI_CASTOR_SCIENCE_UPGRADE_MIN_COUNT)
		{
			return shared_ptr<Order>();
		}
	}
	controlUpgradeDelay=AI_CASTOR_UPGRADE_DELAY_TICKS;
	return shared_ptr<Order>(new OrderConstruction(b->gid, AI_CASTOR_CONSTRUCTION_ORDER_UNITS, AI_CASTOR_CONSTRUCTION_ORDER_UNITS));
}




std::shared_ptr<Order>AICastor::controlStrikes()
{
	controlStrikesTimer=timer+AI_CASTOR_CONTROL_STRIKES_INTERVAL;

	if (!onStrike)
		return shared_ptr<Order>();

	int warriors=team->stats.getTotalUnits(WARRIOR);
	int warFlagsGoal=(warriors+AI_CASTOR_WARFLAG_FORMULA_BIAS)/AI_CASTOR_WARRIORS_PER_WARFLAG;
	int warFlagsReal=buildingSum[IntBuildingType::WAR_FLAG][0];

	if (!strikeTeamSelected)
	{
		int bestLevel=AI_CASTOR_LEVEL_NONE;
		for (int ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
		{
			Team *enemyTeam=game->teams[ti];
			Uint32 me=team->me;
			if ((team->enemies&enemyTeam->me)==0)
				continue;
			Building **enemyBuildings=enemyTeam->myBuildings;
			for (int bi=0; bi<Building::MAX_COUNT; bi++)
			{
				Building *b=enemyBuildings[bi];
				if (b==NULL || ((b->seenByMask&me)==0) || b->locked[canSwim])
					continue;
				int level=b->type->level;
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
			if ((team->enemies&enemyTeam->me)==0)
				continue;
			Building **enemyBuildings=enemyTeam->myBuildings;
			for (int bi=0; bi<Building::MAX_COUNT; bi++)
			{
				Building *b=enemyBuildings[bi];
				if (b==NULL || ((b->seenByMask&me)==0) || b->locked[canSwim] || b->type->level<bestLevel)
					continue;
				int shortTypeNum=b->type->shortTypeNum;
				if (shortTypeNum==IntBuildingType::ATTACK_BUILDING
					|| shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
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
	
	//int w=map->w;
	//int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
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
		Sint32 level=b->type->level;
		Uint32 score=(AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS+workRange)*(AI_CASTOR_STRIKE_BUILDING_SCORE_BIAS+level);
		if (b->type->isBuildingSite)
			score=(score>>AI_CASTOR_STRIKE_BUILDING_SITE_SHIFT);
		int shortTypeNum=b->type->shortTypeNum;
		if (shortTypeNum==IntBuildingType::ATTACK_BUILDING
			||shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
			score=(score<<AI_CASTOR_STRIKE_HIGH_VALUE_SHIFT);
		if (bestScore<score)
		{
			bestScore=score;
			bestBuilding=b;
		}
	}
	
	std::list<Building *> *virtualBuildings=&team->virtualBuildings;
	if (bestBuilding!=NULL)
	{
		Sint32 x=bestBuilding->posX+1;
		Sint32 y=bestBuilding->posY+1;

		if (warFlagsReal<warFlagsGoal)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
			return shared_ptr<Order>(new OrderCreate(team->teamNumber, x, y, typeNum, 1, 1));
		}
		else
		{
			Sint32 maxSqDist=0;
			Building *maxFlag=NULL;
			for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if ((*it)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
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
			if (maxSqDist>AI_CASTOR_FLAG_MOVE_SQ_DIST && maxFlag!=NULL)
			{
				return shared_ptr<Order>(new OrderMoveFlag(maxFlag->gid, x, y, true));
			}
			for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if ((*it)->type->shortTypeNum==IntBuildingType::WAR_FLAG
					&& (*it)->maxUnitWorking<AI_CASTOR_WARFLAG_WORKER_GOAL)
				{
					return shared_ptr<Order>(new OrderModifyBuilding((*it)->gid, AI_CASTOR_WARFLAG_WORKER_GOAL));
				}
		}
	}
	else
	{
		for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
			if ((*it)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				return shared_ptr<Order>(new OrderDelete((*it)->gid));
			}
		strikeTeamSelected=false;
		onStrike=false;
	}
	
	return shared_ptr<Order>();
}



