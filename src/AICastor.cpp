/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>

#include "AICastor.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

// utilities part:

inline static void dxdyfromDirection(int direction, int *dx, int *dy)
{
	const int tab[9][2]={	{ -1, -1},
							{ 0, -1},
							{ 1, -1},
							{ 1, 0},
							{ 1, 1},
							{ 0, 1},
							{ -1, 1},
							{ -1, 0},
							{ 0, 0} };
	assert(direction>=0);
	assert(direction<=8);
	*dx=tab[direction][0];
	*dy=tab[direction][1];
}


// AICastor::Project part:

AICastor::Project::Project(BuildingType::BuildingTypeShortNumber shortTypeNum, const char *suffix)
{
	this->shortTypeNum=shortTypeNum;
	init(suffix);
}
AICastor::Project::Project(BuildingType::BuildingTypeShortNumber shortTypeNum, int amount, Sint32 mainWorkers, const char *suffix)
{
	this->shortTypeNum=shortTypeNum;
	init(suffix);
	this->amount=amount;
	this->mainWorkers=mainWorkers;
}
void AICastor::Project::init(const char *suffix)
{
	amount=1;
	food=(this->shortTypeNum==BuildingType::SWARM_BUILDING
		|| this->shortTypeNum==BuildingType::FOOD_BUILDING);
	
	debugStdName += Toolkit::getStringTable()->getString("[building name]", this->shortTypeNum);
	debugStdName += "-";
	debugStdName += suffix;
	this->debugName=debugStdName.c_str();
	
	printf("new project(%s)\n", debugName);
	
	subPhase=0;;
	
	successWait=0;
	blocking=true;
	critical=false;
	priority=1;
	triesLeft=64;
	
	mainWorkers=-1;
	foodWorkers=-1;
	otherWorkers=-1;
	
	multipleStart=false;
	waitFinished=false;
	finalWorkers=-1;
	
	finished=false;
	
	timer=(Uint32)-1;
}


// AICastor::Strategy part:

AICastor::Strategy::Strategy()
{
	defined=false;
	
	successWait=0;
	
	warLevelTriger=0;
	warTimeTriger=0;
	maxAmountGoal=0;
	
	wheatCareLimit=0;
};

// AICastor main class part:

void AICastor::firstInit()
{
	obstacleUnitMap=NULL;
	obstacleBuildingMap=NULL;
	spaceForBuildingMap=NULL;
	buildingNeighbourMap=NULL;
	twoSpaceNeighbourMap=NULL;
	
	workPowerMap=NULL;
	workRangeMap=NULL;
	workAbilityMap=NULL;
	hydratationMap=NULL;
	wheatGrowthMap=NULL;
	wheatCareMap=NULL;
	
	goodBuildingMap=NULL;
	
	ressourcesCluster=NULL;
}

AICastor::AICastor(Player *player)
{
	logFile=globalContainer->logFileManager->getFile("AICastor.log");
	
	firstInit();
	init(player);
}

AICastor::AICastor(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	logFile=globalContainer->logFileManager->getFile("AICastor.log");
	
	firstInit();
	bool goodLoad=load(stream, player, versionMinor);
	assert(goodLoad);
}

void AICastor::init(Player *player)
{
	assert(player);
	
	// Logical :
	timer=0;
	canSwim=false;
	needSwim=false;
	lastFreeWorkersComputed=(Uint32)-1;
	computeNeedSwimTimer=0;
	controlSwarmsTimer=0;
	expandFoodTimer=0;
	controlFoodTimer=0;
	controlUpgradeTimer=0;
	controlUpgradeDelay=32;
	controlFoodToogle=false;
	
	hydratationMapComputed=false;
	warLevel=0;
	warTimeTrigerLevel=0;
	warLevelTrigerLevel=0;
	warAmountTrigerLevel=0;
	foodLock=false;
	foodLockStats[0]=0;
	foodLockStats[1]=0;
	overWorkers=false;
	buildsAmount=0;
	
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		delete *pi;
	projects.clear();

	// Structural:
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
	
	size_t size=map->w*map->h;
	assert(size>0);
	
	if (obstacleUnitMap!=NULL)
		delete[] obstacleUnitMap;
	obstacleUnitMap=new Uint8[size];
	
	if (obstacleBuildingMap!=NULL)
		delete[] obstacleBuildingMap;
	obstacleBuildingMap=new Uint8[size];
	
	if (spaceForBuildingMap!=NULL)
		delete[] spaceForBuildingMap;
	spaceForBuildingMap=new Uint8[size];
	
	if (buildingNeighbourMap!=NULL)
		delete[] buildingNeighbourMap;
	buildingNeighbourMap=new Uint8[size];
	
	if (twoSpaceNeighbourMap!=NULL)
		delete[] twoSpaceNeighbourMap;
	twoSpaceNeighbourMap=new Uint8[size];
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	workPowerMap=new Uint8[size];
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	workRangeMap=new Uint8[size];
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	workAbilityMap=new Uint8[size];
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	hydratationMap=new Uint8[size];
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	wheatGrowthMap=new Uint8[size];
	
	if (wheatCareMap!=NULL)
		delete[] wheatCareMap;
	wheatCareMap=new Uint8[size];
	
	if (goodBuildingMap!=NULL)
		delete[] goodBuildingMap;
	goodBuildingMap=new Uint8[size];
	
	
	if (ressourcesCluster!=NULL)
		delete[] ressourcesCluster;
	ressourcesCluster=new Uint16[size];
}

AICastor::~AICastor()
{
	if (obstacleUnitMap!=NULL)
		delete[] obstacleUnitMap;
	
	if (obstacleBuildingMap!=NULL)
		delete[] obstacleBuildingMap;
	
	if (spaceForBuildingMap!=NULL)
		delete[] spaceForBuildingMap;
	
	if (buildingNeighbourMap!=NULL)
		delete[] buildingNeighbourMap;
	
	if (twoSpaceNeighbourMap!=NULL)
		delete[] twoSpaceNeighbourMap;
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	
	if (wheatCareMap!=NULL)
		delete[] wheatCareMap;
	
	if (goodBuildingMap!=NULL)
		delete[] goodBuildingMap;
	
	
	if (ressourcesCluster!=NULL)
		delete[] ressourcesCluster;
}

bool AICastor::load(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	printf("AICastor::load\n");
	init(player);
	assert(game);
	
	if (versionMinor<29)
	{
		//TODO:init
		return true;
	}
	
	Sint32 aiFileVersion=SDL_ReadBE32(stream);
	if (aiFileVersion<AI_FILE_MIN_VERSION)
		return false;
	if (aiFileVersion>=1)
		timer=SDL_ReadBE32(stream);
	else
		timer=0;
	
	return true;
}

void AICastor::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, AI_FILE_VERSION);
	SDL_WriteBE32(stream, timer);
}

Order *AICastor::getOrder()
{
	timer++;
	
	if (!hydratationMapComputed)
		computeHydratationMap();
	
	if (!strategy.defined)
		defineStrategy();
	
	//printf("getOrder(), %d projects\n", projects.size());
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->finished)
		{
			//printf("deleting project (%s)\n", (*pi)->debugName);
			delete *pi;
			pi=projects.erase(pi);
		}
	bool blocking=false;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->blocking)
			blocking=true;
	
	computeBuildingSum();
	
	if (!blocking)// No blocking project, we can start a new one:
		addProjects();
	
	Sint32 priority=0x7FFFFFFF;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (priority>(*pi)->priority && (*pi)->critical)
			priority=(*pi)->priority;
	
	if (priority>=0 && timer>controlSwarmsTimer)
	{
		computeWarLevel();
		controlSwarmsTimer=timer+256; // each 10s
		Order *order=controlSwarms();
		if (order)
			return order;
	}
	
	//bool critical=false;
	//for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
	//	if ((*pi)->critical)
	//		critical=true;
	
	int minReal=1024;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->priority<=priority)
		{
			int real=buildingSum[(*pi)->shortTypeNum][0];
			if (minReal>real)
				minReal=real;
		}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->priority<=priority)
		{
			int real=buildingSum[(*pi)->shortTypeNum][0];
			if (real<=minReal)
			{
				Order *order=continueProject(*pi);
				if (order)
					return order;
			}
		}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->priority<=priority)
		{
			int real=buildingSum[(*pi)->shortTypeNum][0];
			if (real>minReal)
			{
				Order *order=continueProject(*pi);
				if (order)
					return order;
			}
		}
	
	if (priority>0 && timer>expandFoodTimer)
	{
		expandFoodTimer=timer+256; // each 10s
		Order *order=expandFood();
		if (order)
			return order;
	}
	
	if (priority>0)
	{
		Order *order=controlFood();
		if (order)
			return order;
	}
	
	if (priority>0)
	{
		Order *order=controlUpgrades();
		if (order)
			return order;
	}
	
	return new NullOrder();
}

void AICastor::defineStrategy()
{
	strategy.defined=true;
	
	for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
		strategy.build[bi].finalWorkers=-1;
	
	strategy.build[BuildingType::SCIENCE_BUILDING].baseOrder=0;
	strategy.build[BuildingType::SWARM_BUILDING].baseOrder=1;
	strategy.build[BuildingType::ATTACK_BUILDING].baseOrder=2;
	strategy.build[BuildingType::DEFENSE_BUILDING].baseOrder=3;
	strategy.build[BuildingType::FOOD_BUILDING].baseOrder=4;
	strategy.build[BuildingType::HEAL_BUILDING].baseOrder=5;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].baseOrder=6;
	strategy.build[BuildingType::WALKSPEED_BUILDING].baseOrder=7;
	
	
	strategy.build[BuildingType::SCIENCE_BUILDING].base=2;
	strategy.build[BuildingType::SWARM_BUILDING].base=2;
	strategy.build[BuildingType::ATTACK_BUILDING].base=2;
	strategy.build[BuildingType::DEFENSE_BUILDING].base=2;
	strategy.build[BuildingType::FOOD_BUILDING].base=4;
	strategy.build[BuildingType::HEAL_BUILDING].base=2;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].base=1;
	strategy.build[BuildingType::WALKSPEED_BUILDING].base=1;
	
	strategy.build[BuildingType::SCIENCE_BUILDING].workers=7;
	strategy.build[BuildingType::SWARM_BUILDING].workers=2;
	strategy.build[BuildingType::ATTACK_BUILDING].workers=4;
	strategy.build[BuildingType::DEFENSE_BUILDING].workers=3;
	strategy.build[BuildingType::FOOD_BUILDING].workers=2;
	strategy.build[BuildingType::HEAL_BUILDING].workers=1;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].workers=4;
	strategy.build[BuildingType::WALKSPEED_BUILDING].workers=4;
	
	strategy.build[BuildingType::SWARM_BUILDING].finalWorkers=2;
	strategy.build[BuildingType::FOOD_BUILDING].finalWorkers=1;
	strategy.build[BuildingType::DEFENSE_BUILDING].finalWorkers=1;
	
	
	strategy.build[BuildingType::SCIENCE_BUILDING].upgradeBase=1;
	strategy.build[BuildingType::SWARM_BUILDING].upgradeBase=0;
	strategy.build[BuildingType::ATTACK_BUILDING].upgradeBase=2;
	strategy.build[BuildingType::DEFENSE_BUILDING].upgradeBase=1;
	strategy.build[BuildingType::FOOD_BUILDING].upgradeBase=2;
	strategy.build[BuildingType::HEAL_BUILDING].upgradeBase=1;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].upgradeBase=0;
	strategy.build[BuildingType::WALKSPEED_BUILDING].upgradeBase=0;
	
	
	
	strategy.build[BuildingType::DEFENSE_BUILDING].newOrder=0;
	strategy.build[BuildingType::SWARM_BUILDING].newOrder=1;
	strategy.build[BuildingType::FOOD_BUILDING].newOrder=2;
	strategy.build[BuildingType::SCIENCE_BUILDING].newOrder=3;
	strategy.build[BuildingType::ATTACK_BUILDING].newOrder=4;
	strategy.build[BuildingType::HEAL_BUILDING].newOrder=5;
	strategy.build[BuildingType::WALKSPEED_BUILDING].newOrder=6;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].newOrder=7;
	
	strategy.build[BuildingType::DEFENSE_BUILDING].news=10;
	strategy.build[BuildingType::SWARM_BUILDING].news=1;
	strategy.build[BuildingType::FOOD_BUILDING].news=7;
	strategy.build[BuildingType::SCIENCE_BUILDING].news=2;
	strategy.build[BuildingType::ATTACK_BUILDING].news=2;
	strategy.build[BuildingType::HEAL_BUILDING].news=5;
	strategy.build[BuildingType::WALKSPEED_BUILDING].news=1;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].news=1;
	
	
	strategy.build[BuildingType::DEFENSE_BUILDING].newUpgrade=10;
	strategy.build[BuildingType::SWARM_BUILDING].newUpgrade=0;
	strategy.build[BuildingType::FOOD_BUILDING].newUpgrade=3;
	strategy.build[BuildingType::SCIENCE_BUILDING].newUpgrade=2;
	strategy.build[BuildingType::ATTACK_BUILDING].newUpgrade=2;
	strategy.build[BuildingType::HEAL_BUILDING].newUpgrade=5;
	strategy.build[BuildingType::WALKSPEED_BUILDING].newUpgrade=0;
	strategy.build[BuildingType::SWIMSPEED_BUILDING].newUpgrade=0;
	
	strategy.successWait=0; // TODO: use a "lowDiscovered" flag instead
	strategy.isFreePart=10; // good in [3..20]
	
	strategy.warLevelTriger=1;
	strategy.warTimeTriger=8192;
	strategy.warAmountTriger=3;
	
	strategy.maxAmountGoal=10;
	
	strategy.wheatCareLimit=4; // be cautious with this. [2..6]
}

Order *AICastor::controlSwarms()
{
	Sint32 warriorGoal=warLevel;
	
	int unitSum[NB_UNIT_TYPE];
	for (int i=0; i<NB_UNIT_TYPE; i++)
		unitSum[i]=0;
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
			unitSum[u->typeNum]++;
	}
	int foodSum=0;
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b && b->maxUnitWorking && b->type->canFeedUnit)
			foodSum+=b->type->maxUnitInside;
	}
	
	int unitSumAll=unitSum[0]+unitSum[1]+unitSum[2];
	
	foodLock=((unitSumAll+3)>=(foodSum<<1));
	foodLockStats[foodLock]++;
	
	bool realFoodLock;
		
	if (warriorGoal>1)
		realFoodLock=((unitSumAll)>=(foodSum*3));
	else
		realFoodLock=((unitSumAll)>=(foodSum*2));
	
	printf("unitSum=[%d, %d, %d], unitSumAll=%d, foodSum=%d, foodLock=%d, realFoodLock=%d, foodLockStats=[%d, %d]\n",
		unitSum[0], unitSum[1], unitSum[2], unitSumAll, foodSum, foodLock, realFoodLock, foodLockStats[0], foodLockStats[1]);
	
	if (realFoodLock)
	{
		// Stop making any units!
		Building **myBuildings=team->myBuildings;
		for (int bi=0; bi<1024; bi++)
		{
			Building *b=myBuildings[bi];
			if (b && b->type->unitProductionTime)
				for (int ri=0; ri<NB_UNIT_TYPE; ri++)
					if (b->ratio[ri]!=0)
					{
						for (int ri=0; ri<NB_UNIT_TYPE; ri++)
						{
							b->ratio[ri]=0;
							b->ratioLocal[ri]=0;
						}
						b->update();
						return new OrderModifySwarm(b->gid, b->ratioLocal);
					}
		}
		
		return NULL;
	}
	
	size_t size=map->w*map->h;
	int discovered=0;
	int seeable=0;
	Uint32 *mapDiscovered=map->mapDiscovered;
	Uint32 *fogOfWar=map->fogOfWar;
	Uint32 me=team->me;
	for (size_t i=0; i<size; i++)
	{
		if (mapDiscovered[i]&me!=0)
			discovered++;
		if (fogOfWar[i]&me!=0)
			seeable++;
	}
	Sint32 explorerGoal;
	if (unitSum[WORKER]<4)
		explorerGoal=0;
	else if (unitSum[EXPLORER]==0)
		explorerGoal=2;
	else if (unitSum[EXPLORER]<3 && (unitSum[EXPLORER]<<2)<unitSum[WORKER] && (discovered+seeable<((int)size<<2)))
		explorerGoal=2;
	else if ((unitSum[EXPLORER]<<4)<unitSum[WORKER])
		explorerGoal=1;
	else
		explorerGoal=0;
	
	Sint32 workerGoal;
	if (overWorkers)
		workerGoal=1;
	else
		workerGoal=4;
	
	printf("discovered=%d, seeable=%d, size=%d, explorerGoal=%d\n",
		discovered, seeable, size, explorerGoal);
	
	for (int bi=0; bi<1024; bi++)
	{
		Building *b=myBuildings[bi];
		if (b && b->type->unitProductionTime)
		{
			if (b->ratio[EXPLORER]!=explorerGoal
				|| b->ratio[WORKER]!=workerGoal
				|| b->ratio[WARRIOR]!=warriorGoal)
			{
				b->ratio[EXPLORER]=explorerGoal;
				b->ratioLocal[EXPLORER]=explorerGoal;
				b->ratio[WORKER]=workerGoal;
				b->ratioLocal[WORKER]=workerGoal;
				b->ratio[WARRIOR]=warriorGoal;
				b->ratioLocal[WARRIOR]=warriorGoal;
				b->update();
				return new OrderModifySwarm(b->gid, b->ratioLocal);
			}
		}
	}
	
	return NULL;
}

Order *AICastor::expandFood()
{
	if (!foodLock && !enoughFreeWorkers())
		return NULL;
	
	Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::FOOD_BUILDING, 0, true);
	int bw=globalContainer->buildingsTypes.get(typeNum)->width;
	int bh=globalContainer->buildingsTypes.get(typeNum)->height;
	assert(bw==bh);
	
	computeCanSwim();
	computeObstacleBuildingMap();
	computeSpaceForBuildingMap(bw);
	computeBuildingNeighbourMap(bw, bh);
	computeWheatGrowthMap(bw, bh);
	computeObstacleUnitMap();
	computeWorkPowerMap();
	computeWorkRangeMap();
	computeWorkAbilityMap();
	
	return findGoodBuilding(typeNum, true, false);
}

Order *AICastor::controlFood()
{
	//int w=map->w;
	//int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	//size_t size=w*h;
	
	int bi=(controlFoodTimer++)&1023;
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[bi];
	if (b==NULL)
		return NULL;
	if (b->type->shortTypeNum!=BuildingType::FOOD_BUILDING && b->type->shortTypeNum!=BuildingType::SWARM_BUILDING)
		return NULL;
	
	int bx=b->posX;
	int by=b->posY;
	int bw=b->type->width;
	int bh=b->type->height;
	
	Uint8 worstCare=0;
	for (int xi=bx-1; xi<bx+bw; xi++)
	{
		Uint8 wheatCare;
		wheatCare=wheatCareMap[(xi&wMask)+(((by-1)&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
		wheatCare=wheatCareMap[(xi&wMask)+(((by+bh)&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
	}
	for (int yi=by; yi<=by+bh; yi++)
	{
		Uint8 wheatCare;
		wheatCare=wheatCareMap[((bx-1)&wMask)+((yi&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
		wheatCare=wheatCareMap[((bx+bw)&wMask)+((yi&hMask)<<wDec)];
		if (worstCare<wheatCare)
			worstCare=wheatCare;
	}
	
	if (worstCare>strategy.wheatCareLimit)
	{
		if (b->maxUnitWorking!=0)
		{
			b->maxUnitWorking=0;
			b->maxUnitWorkingLocal=0;
			b->update();
			return new OrderModifyBuilding(b->gid, 0);
		}
	}
	else
	{
		if (b->type->shortTypeNum==BuildingType::FOOD_BUILDING)
		{
			Sint32 workers;
			if (foodLock && b->type->isBuildingSite)
				workers=2;
			else
				workers=1;
			b->maxUnitWorking=workers;
			b->maxUnitWorkingLocal=workers;
			b->update();
			return new OrderModifyBuilding(b->gid, workers);
		}
		else if (b->type->shortTypeNum==BuildingType::SWARM_BUILDING)
		{
			Sint32 workers;
			if (foodLock)
				workers=1;
			else
				workers=2;
			b->maxUnitWorking=workers;
			b->maxUnitWorkingLocal=workers;
			b->update();
			return new OrderModifyBuilding(b->gid, workers);
		}
		else
			assert(false);
	}
	return NULL;
}

Order *AICastor::controlUpgrades()
{
	if (controlUpgradeDelay!=0)
	{
		controlUpgradeDelay--;
		return NULL;
	}
	if (buildsAmount<1 || !enoughFreeWorkers())
		return NULL;
	int bi=((controlUpgradeTimer++)&1023);
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[bi];
	if (b==NULL)
		return NULL;
	if (b->type->isVirtual)
		return NULL;
	// We compute the number of buildings satifying the strategy:
	int shortTypeNum=b->type->shortTypeNum;
	int level=b->type->level;
	int upgradeLevelGoal=((buildsAmount-1)>>1);
	if (upgradeLevelGoal>3)
		upgradeLevelGoal=3;
	if (level>=upgradeLevelGoal)
		return NULL;
	int sumOver=0;
	for (int li=(level+1); li<4; li++)
		for (int si=0; si<2; si++)
			sumOver+=buildingLevels[shortTypeNum][si][li];
	
	int upgradeAmountGoal;
	upgradeAmountGoal=strategy.build[shortTypeNum].upgradeBase;
	
	for (int ai=1; ai<=upgradeLevelGoal; ai++)
		upgradeAmountGoal+=strategy.build[shortTypeNum].newUpgrade;
	
	printf("controlUpgrades(%d), shortTypeNum=%d, sumOver=%d, upgradeAmountGoal=%d\n",
		bi, shortTypeNum, sumOver, upgradeAmountGoal);
	
	if (sumOver>=upgradeAmountGoal)
		return NULL;
	
	if (shortTypeNum==BuildingType::SCIENCE_BUILDING)
	{
		int buildBase=team->stats.getWorkersLevel(0);
		int buildSum=0;
		for (int i=0; i<4; i++)
			buildSum+=team->stats.getWorkersLevel(i);
		printf(" buildBase=%d, buildSum=%d\n", buildBase, buildSum);
		if (buildBase>buildSum)
			return NULL;
		int sumEqual=0;
		for (int li=level; li<4; li++)
			for (int si=0; si<2; si++)
				sumEqual+=buildingLevels[shortTypeNum][si][li];
		if (sumEqual<2)
		{
			printf(" not another building level %d\n", level);
			return NULL;
		}
	}
	controlUpgradeDelay=32;
	return new OrderConstruction(b->gid);
}

bool AICastor::addProject(Project *project)
{
	if (buildingSum[project->shortTypeNum][0]>=project->amount)
	{
		printf("will not add project (%s x%d) as it already succeded\n", project->debugName, project->amount);
		return false;
	}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (project->shortTypeNum==(*pi)->shortTypeNum)
		{
			if (project->amount<=(*pi)->amount)
			{
				printf("will not add project (%s x%d) as project (%s x%d) has shortTypeNum (%d) too\n",
					project->debugName, project->amount, (*pi)->debugName, (*pi)->amount, project->shortTypeNum);
				(*pi)->timer=timer;
				delete project;
				return false;
			}
			else
			{
				printf("adding project (%s x%d) as project (%s x%d) has shortTypeNum (%d) too will replace it\n",
					project->debugName, project->amount, (*pi)->debugName, (*pi)->amount, project->shortTypeNum);
				delete (*pi);
				projects.erase(pi);
				projects.push_back(project);
				return true;
			}
		}
	projects.push_back(project);
	return true;
}

void AICastor::addProjects()
{
	//printf(" canFeedUnit=%d, swarms=%d, pool=%d+%d, attaque=%d+%d, speed=%d+%d\n",
	//	canFeedUnit, swarms, pool, poolSite, attaque, attaqueSite, speed, speedSite);
	
	if (buildingSum[BuildingType::FOOD_BUILDING][0]==0)
	{
		Project *project=new Project(BuildingType::FOOD_BUILDING, "boot");
		
		project->successWait=strategy.successWait;
		project->critical=true;
		project->priority=0;
		
		project->food=true;
		
		project->mainWorkers=3;
		project->foodWorkers=1;
		project->otherWorkers=0;
		
		project->multipleStart=true;
		project->waitFinished=true;
		project->finalWorkers=1;
		
		if (addProject(project))
			return;
	}
	if (buildingSum[BuildingType::SWARM_BUILDING][0]+buildingSum[BuildingType::SWARM_BUILDING][1]==0)
	{
		Project *project=new Project(BuildingType::SWARM_BUILDING, "boot");
		
		project->successWait=strategy.successWait;
		project->critical=true;
		project->priority=0;
		project->food=true;
		
		project->mainWorkers=10;
		project->foodWorkers=1;
		project->otherWorkers=0;
		
		project->multipleStart=false;
		project->waitFinished=true;
		project->finalWorkers=2;
		
		if (addProject(project))
			return;
	}
	if (buildingSum[BuildingType::SWIMSPEED_BUILDING][0]+buildingSum[BuildingType::SWIMSPEED_BUILDING][1]==0)
	{
		if (timer>computeNeedSwimTimer)
		{
			computeNeedSwimTimer=timer+1024;// every 41s
			computeNeedSwim();
		}
		if (needSwim)
		{
			Project *project=new Project(BuildingType::SWIMSPEED_BUILDING, 1, 7, "boot");
			project->successWait=strategy.successWait;
			project->critical=true;
			project->priority=0;
			if (addProject(project))
				return;
		}
	}
	if (buildingSum[BuildingType::ATTACK_BUILDING][0]+buildingSum[BuildingType::ATTACK_BUILDING][1]==0)
	{
		Project *project=new Project(BuildingType::ATTACK_BUILDING, 1, 7, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		if (addProject(project))
			return;
	}
	/*if (buildingSum[BuildingType::WALKSPEED_BUILDING][0]+buildingSum[BuildingType::WALKSPEED_BUILDING][1]==0)
	{
		Project *project=new Project(BuildingType::WALKSPEED_BUILDING, 1, 7, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		if (addProject(project))
			return;
	}
	if (buildingSum[BuildingType::HEAL_BUILDING][0]+buildingSum[BuildingType::HEAL_BUILDING][1]==0)
	{
		Project *project=new Project(BuildingType::HEAL_BUILDING, 1, 3, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		project->multipleStart=true;
		if (addProject(project))
			return;
	}
	if (buildingSum[BuildingType::SCIENCE_BUILDING][0]+buildingSum[BuildingType::SCIENCE_BUILDING][1]==0)
	{
		Project *project=new Project(BuildingType::SCIENCE_BUILDING, 1, 5, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		if (addProject(project))
			return;
	}*/
	// all critical projects succeded.
	
	// enough workers
	//Strategy::Builds buildsCurrent=strategy.buildsBase;
	buildsAmount=0;
	if (!enoughFreeWorkers())
		return;
	
	for (int bpi=0; bpi<BuildingType::NB_HARD_BUILDING; bpi++)
		for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
			if (bpi==strategy.build[bi].baseOrder)
				if (buildingSum[bi][0]+buildingSum[bi][1]<strategy.build[bi].base)
				{
					if (bi==BuildingType::SWARM_BUILDING && foodLock)
						continue;
					Project *project=new Project((BuildingType::BuildingTypeShortNumber)bi,
						strategy.build[bi].base, strategy.build[bi].workers, "base");
					project->successWait=strategy.successWait;
					project->finalWorkers=strategy.build[bi].finalWorkers;
					if (addProject(project))
						return;
				}
	buildsAmount=1;
	
	for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
	{
		int upgradeSum=0;
		for (int li=1; li<4; li++)
			upgradeSum+=buildingLevels[bi][0][li];
		if (upgradeSum<strategy.build[bi].upgradeBase)
			return;
	}
	buildsAmount=2;
	
	int amountGoal[BuildingType::NB_HARD_BUILDING];
	for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
		amountGoal[bi]=strategy.build[bi].base;
	
	int upgradeGoal[BuildingType::NB_HARD_BUILDING];
	for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
		upgradeGoal[bi]=strategy.build[bi].upgradeBase;
	
	for (Sint32 agi=1; agi<4; agi++)
	{
		buildsAmount=0+(agi<<1);
		if (!enoughFreeWorkers())
			return;
		for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
			amountGoal[bi]+=strategy.build[bi].news;
		
		for (int bpi=0; bpi<BuildingType::NB_HARD_BUILDING; bpi++)
			for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
				if (bi==strategy.build[bpi].newOrder)
					if (buildingSum[bi][0]+buildingSum[bi][1]<amountGoal[bi])
					{
						if (bi==BuildingType::SWARM_BUILDING && (foodLockStats[1]>foodLockStats[0] || foodLock))
							continue;
						Project *project=new Project((BuildingType::BuildingTypeShortNumber)bi,
							amountGoal[bi], strategy.build[bi].workers, "loop");
						project->successWait=strategy.successWait;
						project->finalWorkers=strategy.build[bi].finalWorkers;
						if (addProject(project))
							return;
					}
		buildsAmount=1+(agi<<1);
		
		for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
			upgradeGoal[bi]+=strategy.build[bi].newUpgrade;
		for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
		{
			int upgradeSum=0;
			for (int li=agi; li<4; li++)
				upgradeSum+=buildingLevels[bi][0][li];
			if (upgradeSum<upgradeGoal[bi])
				return;
		}
		
		buildsAmount=2+(agi<<1);
	}
}

Order *AICastor::continueProject(Project *project)
{
	// Phase alpha will make a new Food Building at any price.
	//printf("(%s)(stn=%d, f=%d, w=[%d, %d, %d], ms=%d, wf=%d), sp=%d\n",
	//	project->debugName,
	//	project->shortTypeNum, project->food,
	//	project->mainWorkers, project->foodWorkers, project->otherWorkers,
	//	project->multipleStart, project->waitFinished, project->subPhase);
	
	if (timer<project->timer+32)
		return NULL;
	
	if (foodLock && !project->critical && project->shortTypeNum==BuildingType::SWARM_BUILDING)
	{
		printf("(%s) (give up by foodLock [%d, %d])\n", project->debugName, project->blocking, project->critical);
		project->timer=timer+8192; // 5min27s
		project->blocking=false;
		project->critical=false;
	}
	
	if (project->subPhase==0)
	{
		// boot phase
		project->subPhase=2;
		printf("(%s) (boot) (switching to subphase 2)\n", project->debugName);
	}
	else if (project->subPhase==1)
	{
		if (!project->critical && !enoughFreeWorkers())
			return NULL;
		// find any good building place
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(project->shortTypeNum, 0, true);
		int bw=globalContainer->buildingsTypes.get(typeNum)->width;
		int bh=globalContainer->buildingsTypes.get(typeNum)->height;
		assert(bw==bh);
		
		computeCanSwim();
		computeObstacleBuildingMap();
		computeSpaceForBuildingMap(bw);
		computeBuildingNeighbourMap(bw, bh);
		computeWheatGrowthMap(bw, bh);
		computeObstacleUnitMap();
		computeWorkPowerMap();
		computeWorkRangeMap();
		computeWorkAbilityMap();
		
		Order *gfbm=findGoodBuilding(typeNum, project->food, project->critical);
		project->timer=timer;
		if (gfbm)
		{
			if (project->successWait>0)
			{
				printf("(%s) (successWait [%d])\n", project->debugName, project->successWait);
				project->successWait--;
			}
			else
			{
				project->subPhase=2;
				printf("(%s) (one construction site placed) (switching to next subphase 2)\n", project->debugName);
				return gfbm;
			}
		}
		else if (project->triesLeft>0)
		{
			project->triesLeft--;
		}
		else
		{
			printf("(%s) (give up by failures [%d, %d])\n", project->debugName, project->blocking, project->critical);
			project->timer=timer+8192; // 5min27s
			project->blocking=false;
			project->critical=false;
		}
	}
	else if (project->subPhase==2)
	{
		// do we have enough building sites ?
		
		int real=buildingSum[project->shortTypeNum][0];
		int site=buildingSum[project->shortTypeNum][1];
		int sum=real+site;
		
		if (real>=project->amount)
		{
			project->subPhase=6;
			printf("(%s) ([%d>=%d] building finished) (switching to subphase 6).\n",
				project->debugName, real, project->amount);
			if (!project->waitFinished)
			{
				printf("(%s) (deblocking [%d, %d])\n", project->debugName, project->blocking, project->critical);
				project->blocking=false;
				project->critical=false;
			}
		}
		else if (sum<project->amount)
		{
			project->subPhase=1;
			printf("(%s) (need more construction site [%d+%d<%d]) (switching back to subphase 1)\n",
				project->debugName, real, site, project->amount);
		}
		else
		{
			project->subPhase=3;
			printf("(%s) (enough real building site found [%d+%d>=%d]) (switching to next subphase 3)\n",
				project->debugName, real, site, project->amount);
			if (!project->waitFinished)
			{
				printf("(%s) (deblocking [%d, %d])\n", project->debugName, project->blocking, project->critical);
				project->blocking=false;
				project->critical=false;
			}
		}
	}
	else if (project->subPhase==3)
	{
		// balance workers:
		
		int isFree=getFreeWorkers();
		Sint32 mainWorkers=project->mainWorkers;
		Sint32 finalWorkers=project->finalWorkers;
		if (isFree<=3)
		{
			if (mainWorkers>3)
				mainWorkers=((3+mainWorkers)>>1);
			//if (finalWorkers>3)
			//	finalWorkers=3;
		}
		else
		{
			if (mainWorkers>isFree)
				mainWorkers=((isFree+mainWorkers)>>1);
			//if (finalWorkers>isFree)
			//	finalWorkers=isFree;
		}
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(project->shortTypeNum, 0, false);
		Sint32 siteTypeNum=globalContainer->buildingsTypes.getTypeNum(project->shortTypeNum, 0, true);
		
		Sint32 swarmTypeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::SWARM_BUILDING, 0, false);
		Sint32 swarmSiteTypeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::SWARM_BUILDING, 0, true);
		Sint32 foodTypeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::FOOD_BUILDING, 0, false);
		Sint32 foodSiteTypeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::FOOD_BUILDING, 0, true);
		
		Building **myBuildings=team->myBuildings;
		for (int i=0; i<1024; i++)
		{
			Building *b=myBuildings[i];
			if (b)
			{
				if (b->typeNum==typeNum)
				{
					// a main building site
					if (finalWorkers>=0 && b->maxUnitWorking!=finalWorkers)
					{
						b->maxUnitWorking=finalWorkers;
						b->maxUnitWorkingLocal=finalWorkers;
						b->update();
						project->timer=timer;
						return new OrderModifyBuilding(b->gid, finalWorkers);
					}
				}
				else if (b->typeNum==siteTypeNum)
				{
					// a main building
					if (mainWorkers>=0 && b->maxUnitWorking!=mainWorkers)
					{
						b->maxUnitWorking=mainWorkers;
						b->maxUnitWorkingLocal=mainWorkers;
						b->update();
						project->timer=timer;
						return new OrderModifyBuilding(b->gid, mainWorkers);
					}
				}
				else if (b->typeNum==swarmTypeNum
					|| b->typeNum==swarmSiteTypeNum
					|| b->typeNum==foodTypeNum
					|| b->typeNum==foodSiteTypeNum)
				{
					// food buildings
					if (project->foodWorkers>=0 && b->maxUnitWorking!=project->foodWorkers)
					{
						b->maxUnitWorking=project->foodWorkers;
						b->maxUnitWorkingLocal=project->foodWorkers;
						b->update();
						project->timer=timer;
						return new OrderModifyBuilding(b->gid, project->foodWorkers);
					}
				}
				else if (b->type->maxUnitWorking!=0)
				{
					// others buildings:
					if (project->otherWorkers>=0 && b->maxUnitWorking!=project->otherWorkers)
					{
						b->maxUnitWorking=project->otherWorkers;
						b->maxUnitWorkingLocal=project->otherWorkers;
						b->update();
						project->timer=timer;
						return new OrderModifyBuilding(b->gid, project->otherWorkers);
					}
				}
			}
		}
		
		int real=buildingSum[project->shortTypeNum][0];
		int site=buildingSum[project->shortTypeNum][1];
		int sum=real+site;
		
		//printf("(%s) (all maxUnitWorking set)\n", project->debugName);
		
		if (real>=project->amount)
		{
			project->subPhase=6;
			printf("(%s) (building finished [%d+%d>=%d]) (switching to subphase 6).\n",
				project->debugName, real, site, project->amount);
		}
		else if (sum<project->amount)
		{
			project->subPhase=1;
			printf("(%s) (need more construction site [%d+%d<%d]) (switching back to subphase 1)\n",
				project->debugName, real, site, project->amount);
		}
		else if (project->multipleStart)
		{
			printf("(%s) (want more construction site [%d+%d>=%d])\n",
				project->debugName, real, site, project->amount);
			int isFree=getFreeWorkers();
			if (isFree>0)
			{
				project->subPhase=1;
				printf("(%s) (enough free workers) (switching back to subphase 1)\n", project->debugName);
			}
			else
			{
				project->subPhase=5;
				printf("(%s) (no more free workers) (switching to next subphase 5)\n", project->debugName);
			}
		}
		else
		{
			project->subPhase=5;
			printf("(%s) (enough construction site [%d+%d>=%d]) (switching to next subphase 5)\n",
				project->debugName, real, site, project->amount);
		}
	}
	else if (project->subPhase==5)
	{
		// We simply wait for the food building to be finished,
		// and add free workers if aviable and project.waitFinished:
		
		if (project->waitFinished || overWorkers)
		{
			int isFree=getFreeWorkers();
			if (isFree>2)
			{
				Sint32 siteTypeNum=globalContainer->buildingsTypes.getTypeNum(project->shortTypeNum, 0, true);
				Building **myBuildings=team->myBuildings;
				for (int i=0; i<1024; i++)
				{
					Building *b=myBuildings[i];
					if (b && b->typeNum==siteTypeNum && b->maxUnitWorking<project->mainWorkers)
					{
						//printf("(%s) (incrementing workers) isFree=%d, current=%d\n",
						//	project->debugName, isFree, b->maxUnitWorking);
						b->maxUnitWorking++;
						b->maxUnitWorkingLocal=b->maxUnitWorking;
						b->update();
						project->timer=timer;
						return new OrderModifyBuilding(b->gid, b->maxUnitWorking);
					}
				}
			}
		}
		
		int real=buildingSum[project->shortTypeNum][0];
		int site=buildingSum[project->shortTypeNum][1];
		int sum=real+site;
		
		if (real>=project->amount)
		{
			project->subPhase=6;
			printf("(%s) (building finished [%d+%d>=%d]) (switching to subphase 6).\n",
				project->debugName, real, site, project->amount);
		}
		else if (sum<project->amount)
		{
			project->subPhase=2;
			printf("(%s) (building destroyed! [%d+%d<%d]) (switching to subphase 2).\n",
				project->debugName, real, site, project->amount);
		}
	}
	else if (project->subPhase==6)
	{
		// balance final workers:
		
		if (project->blocking)
		{
			printf("(%s) (deblocking [%d, %d])\n", project->debugName, project->blocking, project->critical);
			project->blocking=false;
			project->critical=false;
		}
		
		if (project->finalWorkers>=0)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(project->shortTypeNum, 0, false);
			
			int isFree=getFreeWorkers();
			Sint32 finalWorkers=project->finalWorkers;
			/*if (isFree<=3)
			{
				if (finalWorkers>3)
					finalWorkers=3;
			}
			else if (finalWorkers>isFree)
				finalWorkers=isFree;*/
			
			Building **myBuildings=team->myBuildings;
			for (int i=0; i<1024; i++)
			{
				Building *b=myBuildings[i];
				if (b && b->typeNum==typeNum && b->maxUnitWorking!=finalWorkers)
				{
					assert(b->type->maxUnitWorking!=0);
					printf("(%s) (set finalWorkers [isFree=%d, current=%d, final=%d])\n",
						project->debugName, isFree, b->maxUnitWorking, finalWorkers);
					b->maxUnitWorking=finalWorkers;
					b->maxUnitWorkingLocal=finalWorkers;
					b->update();
					project->timer=timer;
					return new OrderModifyBuilding(b->gid, finalWorkers);
				}
			}
		}
		if (buildingSum[project->shortTypeNum][1]==0)
		{
			project->finished=true;
			printf("(%s) (all finalWorkers set) (project succeded)\n", project->debugName);
		}
	}
	else
		assert(false);
	
	return NULL;
}

bool AICastor::enoughFreeWorkers()
{
	int totalWorkers=team->stats.getTotalUnits(WORKER);
	int workersBalance=team->stats.getWorkersBalance();
	int partFree=(totalWorkers/strategy.isFreePart);
	
	int minBalance;
	if (buildsAmount<=0)
		minBalance=-partFree;
	else if (buildsAmount<=2)
		minBalance=0;
	else if (buildsAmount<=4)
		minBalance=partFree;
	else
		minBalance=(partFree<<1);
	int minOverWorkers=minBalance+partFree;
	
	bool enough=(workersBalance>minBalance);
	overWorkers=(workersBalance>minOverWorkers);
	
	assert(buildsAmount<1024);
	static int oldEnough[1024];
	static bool first=true;
	if (first)
	{
		memset(oldEnough, 2, 1024);
		first=false;
	}
	if (oldEnough[buildsAmount]==2 || enough!=oldEnough[buildsAmount])
	{
		printf("enoughFreeWorkers()=%d, workersBalance=%d, totalWorkers=%d, partFree=%d, buildsAmount=%d, minBalance=%d\n",
			enough, workersBalance, totalWorkers, partFree, buildsAmount, minBalance);
		oldEnough[buildsAmount]=enough;
	}
	return enough;
}

int AICastor::getFreeWorkers()
{
	if (lastFreeWorkersComputed!=timer)
	{
		lastFreeWorkersComputed=timer;
		int sum=0;
		Unit **myUnits=team->myUnits;
		for (int i=0; i<1024; i++)
		{
			Unit *u=myUnits[i];
			if (u && u->medical==Unit::MED_FREE && u->activity==Unit::ACT_RANDOM && u->typeNum==WORKER && !u->subscribed)
				sum++;
		}
		freeWorkers=sum;
	}
	return freeWorkers;
}

void AICastor::computeCanSwim()
{
	//printf("computeCanSwim()...\n");
	// If our population has more healthy-working-units able to swimm than healthy-working-units
	// unable to swimm then we choose to be able to go trough water:
	Unit **myUnits=team->myUnits;
	int sumCanSwim=0;
	int sumCantSwim=0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0)
		{
			if (u->performance[SWIM]>0)
				sumCanSwim++;
			else
				sumCantSwim++;
		}
	}
	
	canSwim=(sumCanSwim>sumCantSwim);
	//printf("...computeCanSwim() done\n");
}

void AICastor::computeNeedSwim()
{
	int w=map->w;
	int h=map->h;
	size_t size=w*h;
	
	canSwim=false;
	computeObstacleUnitMap();
	computeWorkRangeMap();
	
	Sint32 baseCount=0;
	for (size_t i=0; i<size; i++)
		if (workRangeMap[i]!=0)
			baseCount++;
	
	canSwim=true;
	computeObstacleUnitMap();
	computeWorkRangeMap();
	
	Sint32 extendedCount=0;
	for (size_t i=0; i<size; i++)
		if (workRangeMap[i]!=0)
			extendedCount++;
	
	needSwim=((baseCount<<4)>(7*extendedCount));
	printf("needSwim=%d\n", needSwim);
	
	computeCanSwim();
}

void AICastor::computeBuildingSum()
{
	for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<4; li++)
				buildingLevels[bi][si][li]=0;
	
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			if (b->buildingState==Building::WAITING_FOR_CONSTRUCTION && b->constructionResultState==Building::UPGRADE)
				buildingLevels[b->type->shortTypeNum][b->type->isBuildingSite][b->type->level+1]++;
			else
				buildingLevels[b->type->shortTypeNum][b->type->isBuildingSite][b->type->level]++;
		}
	}
	for (int bi=0; bi<BuildingType::NB_HARD_BUILDING; bi++)
		for (int si=0; si<2; si++)
		{
			int sum=0;
			for (int li=0; li<4; li++)
				sum+=buildingLevels[bi][si][li];
			buildingSum[bi][si]=sum;
		}
}

void AICastor::computeWarLevel()
{
	if (timer>strategy.warTimeTriger)
	{
		printf("timer=%d, strategy.warTimeTriger=%d\n", timer, strategy.warTimeTriger);
		warTimeTrigerLevel++;
		strategy.warTimeTriger=strategy.warTimeTriger+((1+strategy.warTimeTriger)>>1);
	}
	int warTimeTrigerLevelUse=warTimeTrigerLevel;
	if (warTimeTrigerLevelUse>2)
		warTimeTrigerLevelUse=2;
	
	int sum=0;
	for (int si=0; si<2; si++)
		for (int li=strategy.warLevelTriger; li<4; li++)
			sum+=buildingLevels[BuildingType::ATTACK_BUILDING][si][li];
	if (sum>1)
		warLevelTrigerLevel=2;
	else if (sum>0)
		warLevelTrigerLevel=1;
	else
		warLevelTrigerLevel=0;
	
	if (buildsAmount>strategy.warAmountTriger)
		warAmountTrigerLevel=2;
	else if (buildsAmount>=strategy.warAmountTriger)
		warAmountTrigerLevel=1;
	else
		warAmountTrigerLevel=0;
	warLevel=warTimeTrigerLevelUse+warLevelTrigerLevel+warAmountTrigerLevel;
	static int oldWarLevel=-1;
	if (oldWarLevel!=warLevel)
	{
		printf("warLevel=%d, warTimeTrigerLevelUse=%d, warLevelTrigerLevel=%d, warAmountTrigerLevel=%d\n",
			warLevel, warTimeTrigerLevelUse, warLevelTrigerLevel, warAmountTrigerLevel);
		oldWarLevel=warLevel;
	}
}

void AICastor::computeObstacleUnitMap()
{
	//printf("computeObstacleUnitMap()...\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//size_t size=w*h;
	Case *cases=map->cases;
	Uint32 teamMask=team->me;
		
	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			Case c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.ressource.type!=NO_RES_TYPE)
					obstacleUnitMap[wyx]=0;
				else if (c.forbidden&teamMask)
					obstacleUnitMap[wyx]=0;
				else if (!canSwim && (c.terrain>=256) && (c.terrain<256+16)) // !canSwim && isWatter ?
					obstacleUnitMap[wyx]=0;
				else
					obstacleUnitMap[wyx]=1;
			}
			else
				obstacleUnitMap[wyx]=0;
		}
	}
	//printf("...computeObstacleUnitMap() done\n");
}


void AICastor::computeObstacleBuildingMap()
{
	//printf("computeObstacleBuildingMap()...\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	//size_t size=w*h;
	Case *cases=map->cases;
	
	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			Case c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.terrain>=16) // if (!isGrass)
					obstacleBuildingMap[wyx]=0;
				else if (c.ressource.type!=NO_RES_TYPE)
					obstacleBuildingMap[wyx]=0;
				else
					obstacleBuildingMap[wyx]=1;
			}
			else
				obstacleBuildingMap[wyx]=0;
		}
	}
	//printf("...computeObstacleBuildingMap() done\n");
}

void AICastor::computeSpaceForBuildingMap(int max)
{
	//printf("computeSpaceForBuildingMap()...\n");
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	memcpy(spaceForBuildingMap, obstacleBuildingMap, size);
	
	for (int i=1; i<max; i++)
	{
		for (int y=0; y<h; y++)
		{
			int wy0=w*y;
			int wy1=w*((y+1)&wMask);
			
			for (int x=0; x<w; x++)
			{
				int wyx[4];
				wyx[0]=wy0+x+0;
				wyx[1]=wy0+x+1;
				wyx[2]=wy1+x+0;
				wyx[3]=wy1+x+1;
				Uint8 obs[4];
				for (int i=0; i<4; i++)
					obs[i]=spaceForBuildingMap[wyx[i]];
				Uint8 min=255;
				for (int i=0; i<4; i++)
					if (min>obs[i])
						min=obs[i];
				if (min!=0)
					spaceForBuildingMap[wyx[0]]=min+1;
			}
		}
	}
	//printf("...computeSpaceForBuildingMap() done\n");
}


void AICastor::computeBuildingNeighbourMap(int dw, int dh)
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=buildingNeighbourMap;
	Case *cases=map->cases;
	
	//Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	memset(gradient, 0, size);
	
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b && !b->type->isVirtual)
		{
			int bx=b->posX;
			int by=b->posY;
			int bw=b->type->width;
			int bh=b->type->height;
			
			// we skip building with already a neighbour:
			bool neighbour=false;
			//bool wheat=false;
			for (int xi=bx-1; xi<=bx+bw; xi++)
			{
				int index;
				index=(xi&wMask)+(((by-1 )&hMask)<<wDec);
				if (cases[index].building!=NOGBID)
					neighbour=true;
				//if (wheatGradient[index]==255)
				//	wheat=true;
				index=(xi&wMask)+(((by+bh)&hMask)<<wDec);
				if (cases[index].building!=NOGBID)
					neighbour=true;
				//if (wheatGradient[index]==255)
				//	wheat=true;
			}
			if (!neighbour)
				for (int yi=by-1; yi<=by+bh; yi++)
				{
					int index;
					index=((bx-1 )&wMask)+((yi&hMask)<<wDec);
					if (cases[index].building!=NOGBID)
						neighbour=true;
					//if (wheatGradient[index]==255)
					//	wheat=true;
					index=((bx+bw)&wMask)+((yi&hMask)<<wDec);
					if (cases[index].building!=NOGBID)
						neighbour=true;
					//if (wheatGradient[index]==255)
					//	wheat=true;
				}
			
			Uint8 dirty;
			if (neighbour || /*!wheat ||*/ bw!=dw || bh!=dh)
				dirty=1;
			else
				dirty=0;
			
			// dirty at a range of 1 space case, without corners;
			for (int xi=bx-dw+1; xi<bx+bw; xi++)
			{
				gradient[(xi&wMask)+(((by-dh-1)&hMask)<<wDec)]|=1;
				gradient[(xi&wMask)+(((by+bh+1)&hMask)<<wDec)]|=1;
			}
			for (int yi=by-dh+1; yi<by+bh; yi++)
			{
				gradient[((bx-dw-1)&wMask)+((yi&hMask)<<wDec)]|=1;
				gradient[((bx+bw+1)&wMask)+((yi&hMask)<<wDec)]|=1;
			}
			{
				// the same with inner inner corners:
				gradient[((bx-dw)&wMask)+(((by-dh)&hMask)<<wDec)]|=1;
				gradient[((bx-dw)&wMask)+(((by+bh)&hMask)<<wDec)]|=1;
				gradient[((bx+bw)&wMask)+(((by-dh)&hMask)<<wDec)]|=1;
				gradient[((bx+bw)&wMask)+(((by+bh)&hMask)<<wDec)]|=1;
			}
			
			// At a range of 0 space case (neignbours), without corners,
			// we increment (bit 1 to 3), and dirty bit 0 in case:
			for (int xi=bx-dw+1; xi<bx+bw; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by-dh)&hMask)<<wDec)];
				*p=((*p+2)|dirty)&(~16);
				p=&gradient[(xi&wMask)+(((by+bh)&hMask)<<wDec)];
				*p=((*p+2)|dirty)&(~16);
			}
			for (int yi=by-dh+1; yi<by+bh; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx-dw)&wMask)+((yi&hMask)<<wDec)];
				*p=((*p+2)|dirty)&(~16);
				p=&gradient[((bx+bw)&wMask)+((yi&hMask)<<wDec)];
				*p=((*p+2)|dirty)&(~16);
			}
			
			// At a range of 2 space case, without corners,
			// we increment (bit 5 to 7):
			for (int xi=bx-dw; xi<bx+bw+1; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by-dh-2)&hMask)<<wDec)];
				(*p)+=32;
				p=&gradient[(xi&wMask)+(((by+bh+2)&hMask)<<wDec)];
				(*p)+=32;
			}
			for (int yi=by-dh; yi<by+bh+1; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx-dw-2)&wMask)+((yi&hMask)<<wDec)];
				(*p)+=32;
				p=&gradient[((bx+bw+2)&wMask)+((yi&hMask)<<wDec)];
				(*p)+=32;
			}
			{
				// the same with inner inner corners:
				Uint8 *p;
				p=&gradient[((bx-dw-1)&wMask)+(((by-dh-1)&hMask)<<wDec)];
				(*p)+=32;
				p=&gradient[((bx-dw-1)&wMask)+(((by+bh+1)&hMask)<<wDec)];
				(*p)+=32;
				p=&gradient[((bx+bw+1)&wMask)+(((by-dh-1)&hMask)<<wDec)];
				(*p)+=32;
				p=&gradient[((bx+bw+1)&wMask)+(((by+bh+1)&hMask)<<wDec)];
				(*p)+=32;
			}
		}
	}
}

void AICastor::computeTwoSpaceNeighbourMap()
{
}


void AICastor::computeWorkPowerMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=workPowerMap;
	Uint8 maxRange=64;
	if (maxRange>w/2)
		maxRange=w/2;
	if (maxRange>h/2)
		maxRange=h/2;
	
	memset(gradient, 0, size);
	
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>1)/u->race->unitTypes[0][0].hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>maxRange)
				range=maxRange;
			int ux=u->posX;
			int uy=u->posY;
			static const int reducer=3;
			{
				Uint8 *gp=&gradient[(ux&wMask)+((uy&hMask)<<wDec)];
				Uint16 sum=*gp+(range>>reducer);
				if (sum>255)
					sum=255;
				*gp=sum;
			}
			for (int r=1; r<range; r++)
			{
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((ux+dx)&wMask)+(((uy -r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((ux+dx)&wMask)+(((uy +r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((ux -r)&wMask)+(((uy+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((ux +r)&wMask)+(((uy+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
			}
		}
	}
}


void AICastor::computeWorkRangeMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=workRangeMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>1)/u->race->unitTypes[0][0].hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>255)
				range=255;
			int index=(u->posX&wMask)+((u->posY&hMask)<<wDec);
			gradient[index]=(Uint8)range;
		}
	}
	
	map->updateGlobalGradient(gradient);
}


void AICastor::computeWorkAbilityMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 workPower=workPowerMap[i];
		Uint8 workRange=workRangeMap[i];
		
		Uint32 workAbility=((workPower*workRange)>>5);
		if (workAbility>255)
			workAbility=255;
		
		workAbilityMap[i]=(Uint8)workAbility;
	}
}

void AICastor::computeHydratationMap()
{
	printf("computeHydratationMap()...\n");
	int w=map->w;
	int h=map->w;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	
	memset(hydratationMap, 0, size);
	
	Case *cases=map->cases;
	for (size_t i=0; i<size; i++)
	{
		Uint16 t=cases[i].terrain;
		if ((t>=256) && (t<256+16)) // if WATER
			hydratationMap[i]=16;
	}
	
	updateGlobalGradientNoObstacle(hydratationMap);
	hydratationMapComputed=true;
	printf("...computeHydratationMap() done\n");
}

void AICastor::computeWheatGrowthMap(int dw, int dh)
{
	int w=map->w;
	int h=map->w;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	memcpy(wheatGrowthMap, obstacleBuildingMap, size);
	memcpy(wheatCareMap, obstacleUnitMap, size);
	
	for (size_t i=0; i<size; i++)
		if (wheatGradient[i]==255)
			wheatGrowthMap[i]=1+hydratationMap[i];
	
	map->updateGlobalGradient(wheatGrowthMap);
	
	Case *cases=map->cases;
	for (size_t i=0; i<size; i++)
		if (wheatGrowthMap[i]==13 && wheatGradient[i]==254 && cases[i].terrain<16)
			wheatCareMap[i]=8;
	
	map->updateGlobalGradient(wheatCareMap);
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 care=wheatCareMap[i];
		if (care>1)
		{
			care=(care<<1);
			Uint8 *p=&wheatGrowthMap[i];
			Uint8 growth=*p;
			if (growth>care)
				(*p)=growth-care;
			else
				(*p)=1;
		}
	}
}

Order *AICastor::findGoodBuilding(Sint32 typeNum, bool food, bool critical)
{
	int w=map->w;
	int h=map->w;
	int bw=globalContainer->buildingsTypes.get(typeNum)->width;
	int bh=globalContainer->buildingsTypes.get(typeNum)->height;
	assert(bw==bh);
	//int hDec=map->hDec;
	int wDec=map->wDec;
	int wMask=map->wMask;
	int hMask=map->hMask;
	size_t size=w*h;
	Uint32 *mapDiscovered=map->mapDiscovered;
	Uint32 me=team->me;
	printf("findGoodBuilding(%d, %d, %d) b=(%d, %d)\n", typeNum, food, critical, bw, bh);
	
	// minWork computation:
	Sint32 bestWorkScore=2;
	for (size_t i=0; i<size; i++)
	{
		if ((mapDiscovered[i]&me)==0)
			continue;
		Uint8 work=workAbilityMap[i];
		if (bestWorkScore<work)
			bestWorkScore=work;
	}
	Sint32 minWork=bestWorkScore*2;
	if (critical)
	{
		if (minWork>15*4)
			minWork=15*4;
	}
	else
	{
		if (minWork>30*4)
			minWork=30*4;
	}
	printf(" bestWorkScore=%d, minWork=%d\n", bestWorkScore, minWork/4);
	
	// wheatGradientLimit computation:
	Uint32 wheatGradientLimit;
	if (food)
	{
		if (critical)
			wheatGradientLimit=(255-16)*4;
		else
			wheatGradientLimit=(255-4)*4;
	}
	else
	{
		if (critical)
			wheatGradientLimit=(255-5)*4;
		else
			wheatGradientLimit=(255-7)*4;
	}
	printf(" wheatGradientLimit=%d\n", wheatGradientLimit/4);
	
	// we find the best place possible:
	size_t bestIndex;
	Sint32 bestScore=0;
	
	//wheatLimit=(wheatLimit<<2);
	//printf(" (scaled) minWork=%d, wheatLimit=%d\n", minWork, wheatLimit);
	
	Uint8 *wheatGradientMap=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	memset(goodBuildingMap, 0, size);
	
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			size_t corner0=(x|(y<<wDec));
			size_t corner1=(((x+bw-1)&wMask)|(y<<wDec));
			size_t corner2=(x|(((y+bw-1)&hMask)<<wDec));
			size_t corner3=(((x+bw-1)&wMask)|(((y+bw-1)&hMask)<<wDec));
			
			if (critical
				&& (mapDiscovered[corner0]&me)==0
				&& (mapDiscovered[corner1]&me)==0
				&& (mapDiscovered[corner2]&me)==0
				&& (mapDiscovered[corner3]&me)==0)
				continue;
			
			//goodBuildingMap[corner0]=1;
			
			Uint8 space=spaceForBuildingMap[corner0];
			if (space<bw)
				continue;
			
			//goodBuildingMap[corner0]=2;
			
			Sint32 work=workAbilityMap[corner0]+workAbilityMap[corner1]+workAbilityMap[corner2]+workAbilityMap[corner3];
			if (work<minWork)
				continue;
			
			//goodBuildingMap[corner0]=3;
			
			Uint32 wheatGradient=wheatGradientMap[corner0]+wheatGradientMap[corner1]+wheatGradientMap[corner2]+wheatGradientMap[corner3];
			if (food)
			{
				if (wheatGradient<wheatGradientLimit)
					continue;
				//if (wheatGrowth<wheatGrowthLimit)
				//	continue;
			}
			else
			{
				if (wheatGradient>wheatGradientLimit)
					continue;
				//if (wheatGrowth>wheatGrowthLimit)
				//	continue;
			}
			
			//goodBuildingMap[corner0]=4;
			
			Sint32 wheatGrowth=wheatGrowthMap[corner0]+wheatGrowthMap[corner1]+wheatGrowthMap[corner2]+wheatGrowthMap[corner3];
			
			Uint8 neighbour=buildingNeighbourMap[corner0];
			Uint8 directNeighboursCount=(neighbour>>1)&7; // [0, 7]
			Uint8 farNeighboursCount=(neighbour>>5)&7; // [0, 7]
			if ((neighbour&1)||(directNeighboursCount>1))
				continue;
			
			Sint32 score;
			if (food)
				score=((wheatGrowth<<8)+work+wheatGradient)*(8+(directNeighboursCount<<2)+farNeighboursCount);
			else
				score=(4096+work-(wheatGrowth<<8))*(8+(directNeighboursCount<<2)+farNeighboursCount);
			
			// 8 + directNeighboursCount*4 + farNeighboursCount
			
			if (score<0)
				goodBuildingMap[corner0]=0;
			else if ((score>>9)>=255)
				goodBuildingMap[corner0]=255;
			else
				goodBuildingMap[corner0]=(score>>9);
			
			if (bestScore<score)
			{
				bestScore=score;
				bestIndex=corner0;
			}
		}
	
	if (bestScore>0)
	{
	
		printf(" found a cool placen");
		printf("  score=%d, wheatGrowth=%d, wheatGradientMap=%d, work=%d\n",
			bestScore, wheatGrowthMap[bestIndex], wheatGradientMap[bestIndex], workAbilityMap[bestIndex]);
		
		Uint8 neighbour=buildingNeighbourMap[bestIndex];
		Uint8 directNeighboursCount=(neighbour>>1)&7; // [0, 7]
		Uint8 farNeighboursCount=(neighbour>>5)&7; // [0, 7]
			
		printf(" directNeighboursCount=%d, farNeighboursCount=%d\n",
			directNeighboursCount, farNeighboursCount);
		
		Sint32 x=(bestIndex&map->wMask);
		Sint32 y=((bestIndex>>map->wDec)&map->hMask);
		return new OrderCreate(team->teamNumber, x, y, typeNum);
	}
	
	return NULL;
}

void AICastor::computeRessourcesCluster()
{
	printf("computeRessourcesCluster()\n");
	int w=map->w;
	int h=map->w;
	int wMask=map->wMask;
	int hMask=map->hMask;
	size_t size=w*h;
	
	memset(ressourcesCluster, 0, size*2);
	
	//int i=0;
	Uint8 old=0xFF;
	Uint16 id=0;
	bool usedid[65536];
	memset(usedid, 0, 65536*sizeof(bool));
	for (int y=0; y<h; y++)
	{
		for (int x=0; x<w; x++)
		{
			Case *c=map->cases+w*(y&hMask)+(x&wMask); // case
			Ressource r=c->ressource; // ressource
			Uint8 rt=r.type; // ressources type
			
			int rci=x+y*w; // ressource cluster index
			Uint16 *rcp=&ressourcesCluster[rci]; // ressource cluster pointer
			Uint16 rc=*rcp; // ressource cluster
			
			if (rt==0xFF)
			{
				*rcp=0;
				old=0xFF;
			}
			else
			{
				printf("ressource rt=%d, at (%d, %d)\n", rt, x, y);
				if (rt!=old)
				{
					printf(" rt!=old\n");
					id=1;
					while (usedid[id])
						id++;
					if (id)
						usedid[id]=true;
					old=rt;
					printf("  id=%d\n", id);
				}
				if (rc!=id)
				{
					if (rc==0)
					{
						*rcp=id;
						printf(" wrote.\n");
					}
					else
					{
						Uint16 oldid=id;
						usedid[oldid]=false;
						id=rc; // newid
						printf(" cleaning oldid=%d to id=%d.\n", oldid, id);
						// We have to correct last ressourcesCluster values:
						*rcp=id;
						while (*rcp==oldid)
						{
							*rcp=id;
							rcp--;
						}
					}
				}
			}
		}
		memcpy(ressourcesCluster+((y+1)&hMask)*w, ressourcesCluster+y*w, w*2);
	}
	
	int used=0;
	for (int id=1; id<65536; id++)
		if (usedid[id])
			used++;
	printf("computeRessourcesCluster(), used=%d\n", used);
}

void AICastor::updateGlobalGradientNoObstacle(Uint8 *gradient)
{
	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	// Warning, this is *nearly* a copy-past, 4 times, once for each direction.
	int w=map->w;
	int h=map->h;
	int hMask=map->hMask;
	int wMask=map->wMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	for (int yi=0; yi<h; yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi-1)&hMask)<<wDec);
		for (int xi=yi; xi<(yi+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int y=hMask; y>=0; y--)
	{
		int wy=(y<<wDec);
		int wyd=(((y+1)&hMask)<<wDec);
		for (int xi=y; xi<(y+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=0; x<w; x++)
	{
		int xl=(x-1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=wMask; x>=0; x--)
	{
		int xr=(x+1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}
