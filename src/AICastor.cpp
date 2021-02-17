/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
#include <Stream.h>

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

using namespace boost;

// AICastor::Project part:

AICastor::Project::Project(IntBuildingType::Number shortTypeNum, const char *suffix)
{
	this->shortTypeNum=shortTypeNum;
	init(suffix);
}
AICastor::Project::Project(IntBuildingType::Number shortTypeNum, int amount, Sint32 mainWorkers, const char *suffix)
{
	this->shortTypeNum=shortTypeNum;
	init(suffix);
	this->amount=amount;
	this->mainWorkers=mainWorkers;
}
void AICastor::Project::init(const char *suffix)
{
	amount=1;
	food=(this->shortTypeNum==IntBuildingType::SWARM_BUILDING
		|| this->shortTypeNum==IntBuildingType::FOOD_BUILDING);
	defense=(this->shortTypeNum==IntBuildingType::DEFENSE_BUILDING);
	
	debugStdName += IntBuildingType::typeFromShortNumber(this->shortTypeNum);
	debugStdName += "-";
	debugStdName += suffix;
	this->debugName=debugStdName.c_str();
	
	//printf("new project(%s)\n", debugName);
	
	subPhase=0;
	
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
	
	warLevelTrigger=0;
	warTimeTrigger=0;
	maxAmountGoal=0;
};

// AICastor main class part:

void AICastor::firstInit()
{
	obstacleUnitMap=NULL;
	obstacleBuildingMap=NULL;
	spaceForBuildingMap=NULL;
	buildingNeighbourMap=NULL;
	
	workPowerMap=NULL;
	workRangeMap=NULL;
	workAbilityMap=NULL;
	hydratationMap=NULL;
	notGrassMap=NULL;
	wheatGrowthMap=NULL;
	for (int i=0; i<4; i++)
		oldWheatGradient[i]=NULL;
	for (int i=0; i<2; i++)
		wheatCareMap[i]=NULL;
	
	goodBuildingMap=NULL;
	
	enemyWarriorsMap=NULL;
	enemyPowerMap=NULL;
	enemyRangeMap=NULL;
	
	ressourcesCluster=NULL;
}

AICastor::AICastor(Player *player)
{
	logFile=globalContainer->logFileManager->getFile("AICastor.log");
	//logFile=stdout;
	
	firstInit();
	init(player);
}

AICastor::AICastor(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
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
	lastWheatGrowthMapComputed=(Uint32)-1;
	lastEnemyRangeMapComputed=(Uint32)-1;
	lastEnemyPowerMapComputed=(Uint32)-1;
	lastEnemyWarriorsMapComputed=(Uint32)-1;
	computeNeedSwimTimer=0;
	controlSwarmsTimer=0;
	expandFoodTimer=0;
	controlFoodTimer=0;
	controlUpgradeTimer=0;
	controlUpgradeDelay=32;
	controlStrikesTimer=0;
	
	warLevel=0;
	warTimeTriggerLevel=0;
	warLevelTriggerLevel=0;
	warAmountTriggerLevel=0;
	
	onStrike=false;
	strikeTimeTrigger=0;
	strikeTeamSelected=false;
	strikeTeam=0;
	
	foodWarning=false;
	foodLock=false;
	foodSurplus=false;
	foodLockStats[0]=0;
	foodLockStats[1]=0;
	overWorkers=false;
	starvingWarning=false;
	starvingWarningStats[0]=0;
	starvingWarningStats[1]=0;
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
	
	computeBoot=0;
	
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

	if (notGrassMap!=NULL)
		delete[] notGrassMap;
	notGrassMap=new Uint8[size];
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	wheatGrowthMap=new Uint8[size];
	
	for (int i=0; i<4; i++)
	{
		if (oldWheatGradient[i]!=NULL)
			delete[] oldWheatGradient[i];
		oldWheatGradient[i]=new Uint8[size];
	}
	
	for (int i=0; i<2; i++)
	{
		if (wheatCareMap[i]!=NULL)
			delete[] wheatCareMap[i];
		wheatCareMap[i]=new Uint8[size];
	}
	
	if (goodBuildingMap!=NULL)
		delete[] goodBuildingMap;
	goodBuildingMap=new Uint8[size];
	
	if (enemyPowerMap!=NULL)
		delete[] enemyPowerMap;
	enemyPowerMap=new Uint8[size];
	
	if (enemyRangeMap!=NULL)
		delete[] enemyRangeMap;
	enemyRangeMap=new Uint8[size];
	
	if (enemyWarriorsMap!=NULL)
		delete[] enemyWarriorsMap;
	enemyWarriorsMap=new Uint8[size];
	
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
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	
	if (notGrassMap!=NULL)
		delete[] notGrassMap;
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	
	for (int i=0; i<4; i++)
		if (oldWheatGradient[i]!=NULL)
			delete[] oldWheatGradient[i];
	
	for (int i=0; i<2; i++)
		if (wheatCareMap[i]!=NULL)
			delete[] wheatCareMap[i];
	
	if (goodBuildingMap!=NULL)
		delete[] goodBuildingMap;
	
	if (enemyPowerMap!=NULL)
		delete[] enemyPowerMap;
	
	if (enemyRangeMap!=NULL)
		delete[] enemyRangeMap;
	
	if (enemyWarriorsMap!=NULL)
		delete[] enemyWarriorsMap;
	
	if (ressourcesCluster!=NULL)
		delete[] ressourcesCluster;

	for(std::list<Project *>::iterator i=projects.begin(); i!=projects.end(); ++i)
	{
		delete *i;
	}

}

bool AICastor::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	fprintf(logFile, "load(%d)\n", versionMinor);
	init(player);
	assert(game);
	
	stream->readEnterSection("AICastor");
	Sint32 aiFileVersion = stream->readSint32("aiFileVersion");
	if (aiFileVersion<AI_FILE_MIN_VERSION)
	{
		fprintf(stderr, " error: aiFileVersion=%d<AI_FILE_MIN_VERSION=%d\n", aiFileVersion, AI_FILE_MIN_VERSION);
		fprintf(logFile, " error: aiFileVersion=%d<AI_FILE_MIN_VERSION=%d\n", aiFileVersion, AI_FILE_MIN_VERSION);
		stream->readLeaveSection();
		return false;
	}
	if (aiFileVersion>=1)
		timer = stream->readUint32("timer");
	else
		timer=0;
		
	stream->readLeaveSection();
	fprintf(logFile, "load success\n");
	return true;
}

void AICastor::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AICastor");
	stream->writeSint32(AI_FILE_VERSION, "aiFileVersion");
	stream->writeUint32(timer, "timer");
	stream->writeLeaveSection();
}

boost::shared_ptr<Order>AICastor::getOrder()
{
	timer++;
	
	if (!strategy.defined)
		defineStrategy();
	
	if (computeBoot<32)
	{
		computeBoot++;
		return shared_ptr<Order>(new NullOrder());
	}
	else if (computeBoot<17+32)
	{
		switch (computeBoot-32)
		{
			case 0:
			computeHydratationMap();
			break;
			case 1:
			computeNotGrassMap();
			break;
			case 2:
			computeCanSwim();
			break;
			case 3:
			computeNeedSwim();
			break;
			case 4:
			computeBuildingSum();
			break;
			case 5:
			computeWarLevel();
			break;
			case 6:
			computeObstacleUnitMap();
			break;
			case 7:
			computeObstacleBuildingMap();
			break;
			case 8:
			computeWorkPowerMap();
			break;
			case 9:
			computeWorkRangeMap();
			break;
			case 10:
			computeWorkAbilityMap();
			break;
			case 11:
			computeHydratationMap();
			break;
			case 12:
			//computeWheatCareMap();
			{
				size_t size=map->w*map->h;
				Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
				for (int i=0; i<4; i++)
					memcpy(oldWheatGradient[i], wheatGradient, size);
				for (int i=0; i<2; i++)
					memset(wheatCareMap[i], 1, size);
			}
			break;
			case 13:
			computeWheatGrowthMap();
			break;
			case 14:
			computeEnemyPowerMap();
			break;
			case 15:
			computeEnemyRangeMap();
			break;
			case 16:
			computeEnemyWarriorsMap();
			break;
			default:
			assert(false);
		}
		computeBoot++;
		return shared_ptr<Order>(new NullOrder());
	}
	
	if ((timer&511)==0)
	{
		Uint8 *temp=oldWheatGradient[3];
		for (int i=3; i>0; i--)
			oldWheatGradient[i]=oldWheatGradient[i-1];
		oldWheatGradient[0]=temp;
		Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
		memcpy(oldWheatGradient[0], wheatGradient, map->w*map->h);
		computeObstacleUnitMap();
		computeWheatCareMap();
	}
	
	/*// Defense, we check it first, because it will only return true if there is an attack and free warriors
	{
		boost::shared_ptr<Order>order = controlBaseDefense();
		if (order)
			return order;
	}*/
		
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
	
	if (timer>controlSwarmsTimer)
	{
		computeWarLevel();
		controlSwarmsTimer=timer+256; // each 10s
		boost::shared_ptr<Order>order=controlSwarms();
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
				boost::shared_ptr<Order>order=continueProject(*pi);
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
				boost::shared_ptr<Order>order=continueProject(*pi);
				if (order)
					return order;
			}
		}
	
	if (priority>0 && timer>expandFoodTimer)
	{
		expandFoodTimer=timer+256; // each 10s
		boost::shared_ptr<Order>order=expandFood();
		if (order)
			return order;
	}
	
	if (timer>lastEnemyRangeMapComputed+1024) // each 41s
	{
		computeEnemyRangeMap();
	}
	if (timer>lastEnemyWarriorsMapComputed+1024) // each 41s
	{
		computeEnemyWarriorsMap();
	}
	
	/*if (onStrike)
	{
		if (timer>lastEnemyPowerMapComputed+128) // each 5s
			computeEnemyPowerMap();
	}
	else
	{
		if (timer>lastEnemyPowerMapComputed+4096) // each 2min44s
			computeEnemyPowerMap();
	}*/
	
	if (priority>0)
	{
		boost::shared_ptr<Order>order=controlFood();
		if (order)
			return order;
	}
	
	if (priority>0)
	{
		boost::shared_ptr<Order>order=controlUpgrades();
		if (order)
			return order;
	}
	
	if (timer>controlStrikesTimer)
	{
		boost::shared_ptr<Order>order=controlStrikes();
		if (order)
			return order;
	}
	
	return shared_ptr<Order>(new NullOrder());
}

void AICastor::defineStrategy()
{
	strategy.defined=true;
	
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		strategy.build[bi].baseOrder=-1;
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		strategy.build[bi].newOrder=-1;
	
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		strategy.build[bi].finalWorkers=-1;
	
	strategy.build[IntBuildingType::SCIENCE_BUILDING].baseOrder=0;
	strategy.build[IntBuildingType::SWARM_BUILDING].baseOrder=1;
	strategy.build[IntBuildingType::ATTACK_BUILDING].baseOrder=2;
	strategy.build[IntBuildingType::DEFENSE_BUILDING].baseOrder=3;
	strategy.build[IntBuildingType::FOOD_BUILDING].baseOrder=4;
	strategy.build[IntBuildingType::HEAL_BUILDING].baseOrder=5;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].baseOrder=6;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].baseOrder=7;
	
	strategy.build[IntBuildingType::SCIENCE_BUILDING].base=2;
	strategy.build[IntBuildingType::SWARM_BUILDING].base=2;
	strategy.build[IntBuildingType::ATTACK_BUILDING].base=2;
	strategy.build[IntBuildingType::DEFENSE_BUILDING].base=2;
	strategy.build[IntBuildingType::FOOD_BUILDING].base=4;
	strategy.build[IntBuildingType::HEAL_BUILDING].base=2;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].base=1;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].base=1;
	
	strategy.build[IntBuildingType::SCIENCE_BUILDING].baseWorkers=5;
	strategy.build[IntBuildingType::SWARM_BUILDING].baseWorkers=2;
	strategy.build[IntBuildingType::ATTACK_BUILDING].baseWorkers=2;
	strategy.build[IntBuildingType::DEFENSE_BUILDING].baseWorkers=2;
	strategy.build[IntBuildingType::FOOD_BUILDING].baseWorkers=3;
	strategy.build[IntBuildingType::HEAL_BUILDING].baseWorkers=1;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].baseWorkers=3;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].baseWorkers=5;
	
	strategy.build[IntBuildingType::SCIENCE_BUILDING].baseUpgrade=2;
	strategy.build[IntBuildingType::SWARM_BUILDING].baseUpgrade=0;
	strategy.build[IntBuildingType::ATTACK_BUILDING].baseUpgrade=2;
	strategy.build[IntBuildingType::DEFENSE_BUILDING].baseUpgrade=1;
	strategy.build[IntBuildingType::FOOD_BUILDING].baseUpgrade=2;
	strategy.build[IntBuildingType::HEAL_BUILDING].baseUpgrade=2;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].baseUpgrade=0;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].baseUpgrade=0;
	
	
	strategy.build[IntBuildingType::SWARM_BUILDING].finalWorkers=2;
	strategy.build[IntBuildingType::FOOD_BUILDING].finalWorkers=1;
	strategy.build[IntBuildingType::DEFENSE_BUILDING].finalWorkers=2;
	
	
	strategy.build[IntBuildingType::DEFENSE_BUILDING].newOrder=0;
	strategy.build[IntBuildingType::SWARM_BUILDING].newOrder=1;
	strategy.build[IntBuildingType::FOOD_BUILDING].newOrder=2;
	strategy.build[IntBuildingType::SCIENCE_BUILDING].newOrder=3;
	strategy.build[IntBuildingType::ATTACK_BUILDING].newOrder=4;
	strategy.build[IntBuildingType::HEAL_BUILDING].newOrder=5;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].newOrder=6;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].newOrder=7;
	
	strategy.build[IntBuildingType::DEFENSE_BUILDING].news=10;
	strategy.build[IntBuildingType::SWARM_BUILDING].news=1;
	strategy.build[IntBuildingType::FOOD_BUILDING].news=7;
	strategy.build[IntBuildingType::SCIENCE_BUILDING].news=2;
	strategy.build[IntBuildingType::ATTACK_BUILDING].news=2;
	strategy.build[IntBuildingType::HEAL_BUILDING].news=5;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].news=1;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].news=1;
	
	strategy.build[IntBuildingType::DEFENSE_BUILDING].newWorkers=4;
	strategy.build[IntBuildingType::SWARM_BUILDING].newWorkers=3;
	strategy.build[IntBuildingType::FOOD_BUILDING].newWorkers=2;
	strategy.build[IntBuildingType::SCIENCE_BUILDING].newWorkers=7;
	strategy.build[IntBuildingType::ATTACK_BUILDING].newWorkers=5;
	strategy.build[IntBuildingType::HEAL_BUILDING].newWorkers=2;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].newWorkers=4;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].newWorkers=4;
	
	strategy.build[IntBuildingType::DEFENSE_BUILDING].newUpgrade=10;
	strategy.build[IntBuildingType::SWARM_BUILDING].newUpgrade=0;
	strategy.build[IntBuildingType::FOOD_BUILDING].newUpgrade=3;
	strategy.build[IntBuildingType::SCIENCE_BUILDING].newUpgrade=2;
	strategy.build[IntBuildingType::ATTACK_BUILDING].newUpgrade=2;
	strategy.build[IntBuildingType::HEAL_BUILDING].newUpgrade=5;
	strategy.build[IntBuildingType::WALKSPEED_BUILDING].newUpgrade=0;
	strategy.build[IntBuildingType::SWIMSPEED_BUILDING].newUpgrade=0;
	
	strategy.successWait=0; // TODO: use a "lowDiscovered" flag instead
	strategy.isFreePart=10; // good in [3..20]
	
	strategy.warLevelTrigger=1;
	strategy.warTimeTrigger=8192;
	strategy.warAmountTrigger=3;
	
	strategy.strikeWarPowerTriggerUp=4096;
	strategy.strikeWarPowerTriggerDown=2048;
	strategy.strikeTimeTrigger=32768; //21min51s
	strikeTimeTrigger=strategy.strikeTimeTrigger;
	
	strategy.maxAmountGoal=10;
}

boost::shared_ptr<Order>AICastor::controlSwarms()
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
	
	foodWarning=((unitSumAll+11)>=(foodSum<<1));
	foodLock=((unitSumAll+3)>=(foodSum<<1));
	foodLockStats[foodLock]++;
	
	foodSurplus=(unitSumAll+4<foodSum);
	
	starvingWarning=(((unitSumAll>>5)+3)<team->stats.getStarvingUnits());
	starvingWarningStats[starvingWarning]++;
	fprintf(logFile,  "starvingWarning=%d\n", starvingWarning);
	
	bool realFoodLock;
	
	if (warriorGoal>1)
		realFoodLock=((unitSumAll)>=(foodSum*3));
	else
		realFoodLock=((unitSumAll)>=(foodSum*2));
	
	fprintf(logFile,  "unitSum=[%d, %d, %d], unitSumAll=%d, foodSum=%d, foodWarning=%d, foodLock=%d, realFoodLock=%d, foodLockStats=[%d, %d]\n",
		unitSum[0], unitSum[1], unitSum[2], unitSumAll, foodSum, foodWarning, foodLock, realFoodLock, foodLockStats[0], foodLockStats[1]);
	
	if ((timer>2048) && (realFoodLock || starvingWarning || starvingWarningStats[1]>starvingWarningStats[0]))
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
						for (int ri=0; ri<NB_UNIT_TYPE; ri++)
						{
							b->ratio[ri]=0;
							b->ratioLocal[ri]=0;
						}
						b->update();
						return shared_ptr<Order>(new OrderModifySwarm(b->gid, b->ratioLocal));
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
	
	fprintf(logFile,  "discovered=%d, seeable=%d, size=%zd, explorerGoal=%d\n",
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
				return shared_ptr<Order>(new OrderModifySwarm(b->gid, b->ratioLocal));
			}
		}
	}
	
	return shared_ptr<Order>();
}

boost::shared_ptr<Order>AICastor::expandFood()
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

boost::shared_ptr<Order>AICastor::controlFood()
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
	for (int i=0; i<8; i++)
		if (b==NULL)
		{
			bi=(controlFoodTimer++)&1023;
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
	
	if (worstCare>4)
	{
		if (b->maxUnitWorking!=0)
		{
			b->maxUnitWorking=0;
			b->maxUnitWorkingLocal=0;
			b->update();
			if (verbose)
				printf("controlFood(), worstCare=%d\n", worstCare);
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, 0));
		}
	}
	else if (worstCare>2)
	{
		if (b->maxUnitWorking>1)
		{
			b->maxUnitWorking=1;
			b->maxUnitWorkingLocal=1;
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
				workers=3+b->type->level; //TODO: random 2 or 3
			else
				workers=1+b->type->level;
			b->maxUnitWorking=workers;
			b->maxUnitWorkingLocal=workers;
			b->update();
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, workers));
		}
		else if (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING)
		{
			Sint32 workers;
			if (foodWarning)
				workers=1;
			else
				workers=2;
			b->maxUnitWorking=workers;
			b->maxUnitWorkingLocal=workers;
			b->update();
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, workers));
		}
		else
			assert(false);
	}
	return shared_ptr<Order>();
}

boost::shared_ptr<Order>AICastor::controlUpgrades()
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
	int bi=((controlUpgradeTimer++)&1023);
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
	if (numberOfAbleWorkers <= 2 || numberOfFreeWorkers <= 4 || numberOfAbleWorkers <= (numberOfFreeWorkers/8))
		return shared_ptr<Order>();
	// Is it any repair:
	if (!b->type->isBuildingSite)
	{
		if (b->type->type == "defencetower")
		{
			if (b->hp*4<b->type->hpMax*1)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		else if (b->type->maxUnitInside)
		{
			if (b->hp*4<b->type->hpMax*3)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		else
		{
			if (b->hp*4<b->type->hpMax*2)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
	}
	// Do we want to upgrade it:
	// We compute the number of buildings satifying the strategy:
	int shortTypeNum=b->type->shortTypeNum;
	if (shortTypeNum>=NB_HARD_BUILDING)
		return shared_ptr<Order>();
	int level=b->type->level;
	int upgradeLevelGoal=((buildsAmount+1)>>1);
	if (upgradeLevelGoal>3)
		upgradeLevelGoal=3;
	if (level>=upgradeLevelGoal)
		return shared_ptr<Order>();
	int sumOver=0;
	for (int li=(level+1); li<4; li++)
		for (int si=0; si<2; si++)
			sumOver+=buildingLevels[shortTypeNum][si][li];
	
	int upgradeAmountGoal=strategy.build[shortTypeNum].baseUpgrade;
	for (int ai=1; ai<=upgradeLevelGoal; ai++)
		upgradeAmountGoal+=strategy.build[shortTypeNum].newUpgrade;
	
	fprintf(logFile,  "controlUpgrades(%d)\n", bi);
	fprintf(logFile,  " shortTypeNum=%d\n", shortTypeNum);
	fprintf(logFile,  " sumOver=%d\n", sumOver);
	fprintf(logFile,  " upgradeAmountGoal=%d\n", upgradeAmountGoal);
	//fprintf(logFile,  "controlUpgrades(%d), shortTypeNum=%d, sumOver=%d, upgradeAmountGoal=%d\n",
	//	bi, shortTypeNum, sumOver, upgradeAmountGoal);
	
	if (sumOver>=upgradeAmountGoal)
		return shared_ptr<Order>();
	
	if (shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
	{
		int buildBase=team->stats.getWorkersLevel(0);
		int buildSum=0;
		for (int i=0; i<4; i++)
			buildSum+=team->stats.getWorkersLevel(i);
		fprintf(logFile,  " buildBase=%d, buildSum=%d\n", buildBase, buildSum);
		if (buildBase>buildSum)
			return shared_ptr<Order>();
		int sumEqual=0;
		for (int li=level; li<4; li++)
			sumEqual+=buildingLevels[shortTypeNum][0][li];
		fprintf(logFile,  " sumEqual=%d\n", sumEqual);
		if (sumEqual<2)
		{
			fprintf(logFile,  " not another building level %d\n", level);
			return shared_ptr<Order>();
		}
	}
	controlUpgradeDelay=32;
	return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
}


// WARNING : Using wasEvent is *NOT* safe, and will *NOT* work through the network
/*boost::shared_ptr<Order>AICastor::controlBaseDefense()
{
	int freeWarriors = team->stats.getFreeUnits(WARRIOR);
	if (team->wasEvent(Team::BUILDING_UNDER_ATTACK_EVENT) && (freeWarriors>0))
	{
		int x, y;
		team->getEventPos(&x, &y);
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::WAR_FLAG, 0, false);
		fprintf(logFile, "controlBaseDefense()\n I'm, under attack !\n Defense war flag set at (%d,%d)\n", x, y);
		onStrike = true;
		return shared_ptr<Order>(new OrderCreate(team->teamNumber, x, y, typeNum));
	}
	return NULL;
}*/


boost::shared_ptr<Order>AICastor::controlStrikes()
{
	controlStrikesTimer=timer+64;
	
	if (!onStrike)
		return shared_ptr<Order>();
	fprintf(logFile,  "controlStrikes()\n");
	
	int warriors=team->stats.getTotalUnits(WARRIOR);
	int warFlagsGoal=(warriors+16)/32;
	int warFlagsReal=buildingSum[IntBuildingType::WAR_FLAG][0];
	fprintf(logFile,  " warriors=%d, warFlagsGoal=%d, warFlagsReal=%d\n", warriors, warFlagsGoal, warFlagsReal);
	
	if (!strikeTeamSelected)
	{
		int bestLevel=-1;
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
		int bestScore=-1;
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
					score+=2;
				else
					score++;
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
	fprintf(logFile,  " strikeTeam=%d\n", strikeTeam);
	
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
		Uint32 score=(1+workRange)*(1+level);
		if (b->type->isBuildingSite)
			score=(score>>2);
		int shortTypeNum=b->type->shortTypeNum;
		if (shortTypeNum==IntBuildingType::ATTACK_BUILDING
			||shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
			score=(score<<1);
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
		
		fprintf(logFile,  " target found bestScore=%d, p=(%d, %d)\n", bestScore, x, y);
		
		if (warFlagsReal<warFlagsGoal)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
			fprintf(logFile,  " create\n");
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
			if (maxSqDist>2 && maxFlag!=NULL)
			{
				fprintf(logFile,  " move %d\n", maxFlag->gid);
				return shared_ptr<Order>(new OrderMoveFlag(maxFlag->gid, x, y, true));
			}
			for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
				if ((*it)->type->shortTypeNum==IntBuildingType::WAR_FLAG
					&& (*it)->maxUnitWorking<20)
				{
					fprintf(logFile,  " modify %d\n", (*it)->gid);
					return shared_ptr<Order>(new OrderModifyBuilding((*it)->gid, 20));
				}
		}
	}
	else
	{
		for (std::list<Building *>::iterator it=virtualBuildings->begin(); it!=virtualBuildings->end(); ++it)
			if ((*it)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				fprintf(logFile,  " removed %d\n", (*it)->gid);
				return shared_ptr<Order>(new OrderDelete((*it)->gid));
			}
		strikeTeamSelected=false;
		onStrike=false;
	}
	
	return shared_ptr<Order>();
}



bool AICastor::addProject(Project *project)
{
	if (buildingSum[project->shortTypeNum][0]>=project->amount)
	{
		fprintf(logFile,  "will not add project (%s x%d) as it already succeded\n", project->debugName, project->amount);
		delete project;
		return false;
	}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (project->shortTypeNum==(*pi)->shortTypeNum)
		{
			if (project->amount<=(*pi)->amount)
			{
				//fprintf(logFile,  "will not add project (%s x%d) as project (%s x%d) has shortTypeNum (%d) too\n",
				//	project->debugName, project->amount, (*pi)->debugName, (*pi)->amount, project->shortTypeNum);
				(*pi)->timer=timer;
				delete project;
				return false;
			}
			else
			{
				fprintf(logFile,  "adding project (%s x%d) as project (%s x%d) has shortTypeNum (%d) too will replace it\n",
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
	
	buildsAmount=-1;
	
	if (buildingSum[IntBuildingType::FOOD_BUILDING][0]==0)
	{
		Project *project=new Project(IntBuildingType::FOOD_BUILDING, "boot");
		
		project->successWait=strategy.successWait;
		project->critical=true;
		project->priority=0;
		project->food=true;
		
		project->mainWorkers=3;
		project->foodWorkers=2;
		project->otherWorkers=0;
		
		project->multipleStart=true;
		project->waitFinished=true;
		project->finalWorkers=1;
		
		if (addProject(project))
			return;
	}
	if (buildingSum[IntBuildingType::SWARM_BUILDING][0]+buildingSum[IntBuildingType::SWARM_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::SWARM_BUILDING, "boot");
		
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
	if (buildingSum[IntBuildingType::SWIMSPEED_BUILDING][0]+buildingSum[IntBuildingType::SWIMSPEED_BUILDING][1]==0)
	{
		if (timer>computeNeedSwimTimer)
		{
			computeNeedSwimTimer=timer+1024;// every 41s
			computeNeedSwim();
		}
		if (needSwim)
		{
			Project *project=new Project(IntBuildingType::SWIMSPEED_BUILDING, 1, 2, "boot");
			project->successWait=strategy.successWait;
			project->critical=true;
			project->priority=0;
			if (addProject(project))
				return;
		}
	}
	if (buildingSum[IntBuildingType::ATTACK_BUILDING][0]+buildingSum[IntBuildingType::ATTACK_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::ATTACK_BUILDING, 1, 2, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		if (addProject(project))
			return;
	}
	/*if (buildingSum[IntBuildingType::WALKSPEED_BUILDING][0]+buildingSum[IntBuildingType::WALKSPEED_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::WALKSPEED_BUILDING, 1, 7, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		if (addProject(project))
			return;
	}
	if (buildingSum[IntBuildingType::HEAL_BUILDING][0]+buildingSum[IntBuildingType::HEAL_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::HEAL_BUILDING, 1, 3, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		project->multipleStart=true;
		if (addProject(project))
			return;
	}
	if (buildingSum[IntBuildingType::SCIENCE_BUILDING][0]+buildingSum[IntBuildingType::SCIENCE_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::SCIENCE_BUILDING, 1, 5, "boot");
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
	
	for (int bpi=0; bpi<NB_HARD_BUILDING; bpi++)
		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
			if (bpi==strategy.build[bi].baseOrder)
				if (buildingSum[bi][0]+buildingSum[bi][1]<strategy.build[bi].base)
				{
					if (bi==IntBuildingType::SWARM_BUILDING
						&& (foodWarning
							|| foodLockStats[1]>foodLockStats[0]
							|| starvingWarning
							|| starvingWarningStats[1]>starvingWarningStats[0]))
						continue;
					Project *project=new Project((IntBuildingType::Number)bi,
						strategy.build[bi].base, strategy.build[bi].baseWorkers, "base");
					project->successWait=strategy.successWait;
					project->finalWorkers=strategy.build[bi].finalWorkers;
					if (addProject(project))
						return;
				}
	buildsAmount=1;
	
	for (int bi=0; bi<NB_HARD_BUILDING; bi++)
	{
		int upgradeSum=0;
		for (int li=1; li<4; li++)
			upgradeSum+=buildingLevels[bi][0][li];
		if (upgradeSum<strategy.build[bi].baseUpgrade)
			return;
	}
	buildsAmount=2;
	
	int amountGoal[NB_HARD_BUILDING];
	for (int bi=0; bi<NB_HARD_BUILDING; bi++)
		amountGoal[bi]=strategy.build[bi].base;
	
	int upgradeGoal[NB_HARD_BUILDING];
	for (int bi=0; bi<NB_HARD_BUILDING; bi++)
		upgradeGoal[bi]=strategy.build[bi].baseUpgrade;
	
	for (Sint32 agi=1; agi<4; agi++)
	{
		buildsAmount=0+(agi<<1);
		if (!enoughFreeWorkers())
			return;
		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
			amountGoal[bi]+=strategy.build[bi].news;
		
		for (int bpi=0; bpi<NB_HARD_BUILDING; bpi++)
			for (int bi=0; bi<NB_HARD_BUILDING; bi++)
				if (bi==strategy.build[bpi].newOrder)
					if (buildingSum[bi][0]+buildingSum[bi][1]<amountGoal[bi])
					{
						if (bi==IntBuildingType::SWARM_BUILDING
							&& (foodWarning
								|| foodLockStats[1]>foodLockStats[0]
								|| starvingWarning
								|| starvingWarningStats[1]>starvingWarningStats[0]))
							continue;
						Project *project=new Project((IntBuildingType::Number)bi,
							amountGoal[bi], strategy.build[bi].newWorkers+(agi-1), "loop");
						project->successWait=strategy.successWait;
						project->finalWorkers=strategy.build[bi].finalWorkers;
						if (addProject(project))
							return;
					}
		buildsAmount=1+(agi<<1);
		
		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
			upgradeGoal[bi]+=strategy.build[bi].newUpgrade;
		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
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

boost::shared_ptr<Order>AICastor::continueProject(Project *project)
{
	// Phase alpha will make a new Food Building at any price.
	//printf("(%s)(stn=%d, f=%d, w=[%d, %d, %d], ms=%d, wf=%d), sp=%d\n",
	//	project->debugName,
	//	project->shortTypeNum, project->food,
	//	project->mainWorkers, project->foodWorkers, project->otherWorkers,
	//	project->multipleStart, project->waitFinished, project->subPhase);
	
	if (timer<project->timer+32)
		return shared_ptr<Order>();
	
	if (foodLock && !project->critical && project->shortTypeNum==IntBuildingType::SWARM_BUILDING)
	{
		fprintf(logFile,  "(%s) (give up by foodLock [%d, %d])\n", project->debugName, project->blocking, project->critical);
		if (starvingWarning)
			project->timer=timer+8192; // 5min28s
		else
			project->timer=timer+2048; // 1min22s
		project->blocking=false;
		project->critical=false;
	}
	
	if (project->subPhase==0)
	{
		// boot phase
		project->subPhase=2;
		fprintf(logFile,  "(%s) (boot) (switching to subphase 2)\n", project->debugName);
	}
	else if (project->subPhase==1)
	{
		if (!project->critical && !enoughFreeWorkers())
		{
			project->timer=timer;
			return shared_ptr<Order>();
		}
		// find any good building place
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(project->shortTypeNum), 0, true);
		int bw=globalContainer->buildingsTypes.get(typeNum)->width;
		int bh=globalContainer->buildingsTypes.get(typeNum)->height;
		assert(bw==bh);
		
		computeCanSwim();
		computeObstacleBuildingMap();
		computeSpaceForBuildingMap(bw);
		computeBuildingNeighbourMap(bw, bh);
		computeObstacleUnitMap();
		computeWheatGrowthMap();
		computeWorkPowerMap();
		computeWorkRangeMap();
		computeWorkAbilityMap();
		
		boost::shared_ptr<Order>gfbm=findGoodBuilding(typeNum, project->food, project->defense, project->critical);
		project->timer=timer;
		if (gfbm)
		{
			if (project->successWait>0)
			{
				fprintf(logFile,  "(%s) (successWait [%d])\n", project->debugName, project->successWait);
				project->successWait--;
			}
			else
			{
				project->subPhase=2;
				fprintf(logFile,  "(%s) (one construction site placed) (switching to next subphase 2)\n", project->debugName);
				return gfbm;
			}
		}
		else if (project->triesLeft>0)
		{
			project->triesLeft--;
		}
		else
		{
			fprintf(logFile,  "(%s) (give up by failures [%d, %d])\n", project->debugName, project->blocking, project->critical);
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
			fprintf(logFile,  "(%s) ([%d>=%d] building finished) (switching to subphase 6).\n",
				project->debugName, real, project->amount);
			if (!project->waitFinished)
			{
				fprintf(logFile,  "(%s) (deblocking [%d, %d])\n", project->debugName, project->blocking, project->critical);
				project->blocking=false;
				project->critical=false;
			}
		}
		else if (sum<project->amount)
		{
			project->subPhase=1;
			fprintf(logFile,  "(%s) (need more construction site [%d+%d<%d]) (switching back to subphase 1)\n",
				project->debugName, real, site, project->amount);
		}
		else
		{
			project->subPhase=3;
			fprintf(logFile,  "(%s) (enough real building site found [%d+%d>=%d]) (switching to next subphase 3)\n",
				project->debugName, real, site, project->amount);
			if (!project->waitFinished)
			{
				fprintf(logFile,  "(%s) (deblocking [%d, %d])\n", project->debugName, project->blocking, project->critical);
				project->blocking=false;
				project->critical=false;
			}
		}
	}
	else if (project->subPhase==3)
	{
		// balance workers:
		
		int isFree=team->stats.getWorkersBalance();
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
		
		Building **myBuildings=team->myBuildings;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=myBuildings[i];
			if (b)
			{
				if (b->type->shortTypeNum==project->shortTypeNum)
				{
					if (b->type->isBuildingSite)
					{
						// a main building site
						if (mainWorkers>=0 && b->maxUnitWorking!=mainWorkers)
						{
							b->maxUnitWorking=mainWorkers;
							b->maxUnitWorkingLocal=mainWorkers;
							b->update();
							project->timer=timer;
							return shared_ptr<Order>(new OrderModifyBuilding(b->gid, mainWorkers));
						}
					}
					else
					{
						// a main building
						if (finalWorkers>=0 && b->maxUnitWorking!=finalWorkers)
						{
							b->maxUnitWorking=finalWorkers;
							b->maxUnitWorkingLocal=finalWorkers;
							b->update();
							project->timer=timer;
							return shared_ptr<Order>(new OrderModifyBuilding(b->gid, finalWorkers));
						}
					}
				}
				else if (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING
					|| b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING)
				{
					// food buildings
					if (project->foodWorkers>=0 && b->maxUnitWorking!=project->foodWorkers)
					{
						b->maxUnitWorking=project->foodWorkers;
						b->maxUnitWorkingLocal=project->foodWorkers;
						b->update();
						project->timer=timer;
						return shared_ptr<Order>(new OrderModifyBuilding(b->gid, project->foodWorkers));
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
						return shared_ptr<Order>(new OrderModifyBuilding(b->gid, project->otherWorkers));
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
			fprintf(logFile,  "(%s) (building finished [%d+%d>=%d]) (switching to subphase 6).\n",
				project->debugName, real, site, project->amount);
		}
		else if (sum<project->amount)
		{
			project->subPhase=1;
			fprintf(logFile,  "(%s) (need more construction site [%d+%d<%d]) (switching back to subphase 1)\n",
				project->debugName, real, site, project->amount);
		}
		else if (project->multipleStart)
		{
			fprintf(logFile,  "(%s) (want more construction site [%d+%d>=%d])\n",
				project->debugName, real, site, project->amount);
			if (isFree>1)
			{
				project->subPhase=1;
				fprintf(logFile,  "(%s) (enough free workers %d) (switching back to subphase 1)\n", project->debugName, isFree);
			}
			else
			{
				project->subPhase=5;
				fprintf(logFile,  "(%s) (no more free workers) (switching to next subphase 5)\n", project->debugName);
			}
		}
		else
		{
			project->subPhase=5;
			fprintf(logFile,  "(%s) (enough construction site [%d+%d>=%d]) (switching to next subphase 5)\n",
				project->debugName, real, site, project->amount);
		}
	}
	else if (project->subPhase==5)
	{
		// We simply wait for the building to be finished,
		// and add free workers if available and project.waitFinished:
		
		if ((project->waitFinished || overWorkers) && enoughFreeWorkers())
		{
			Building **myBuildings=team->myBuildings;
			for (int i=0; i<Building::MAX_COUNT; i++)
			{
				Building *b=myBuildings[i];
				if (b && b->type->shortTypeNum==project->shortTypeNum && b->maxUnitWorking<project->mainWorkers)
				{
					//printf("(%s) (incrementing workers) isFree=%d, current=%d\n",
					//	project->debugName, isFree, b->maxUnitWorking);
					b->maxUnitWorking++;
					b->maxUnitWorkingLocal=b->maxUnitWorking;
					b->update();
					project->timer=timer;
					return shared_ptr<Order>(new OrderModifyBuilding(b->gid, b->maxUnitWorking));
				}
			}
		}
		
		int real=buildingSum[project->shortTypeNum][0];
		int site=buildingSum[project->shortTypeNum][1];
		int sum=real+site;
		
		if (real>=project->amount)
		{
			project->subPhase=6;
			fprintf(logFile,  "(%s) (building finished [%d+%d>=%d]) (switching to subphase 6).\n",
				project->debugName, real, site, project->amount);
		}
		else if (sum<project->amount)
		{
			project->subPhase=2;
			fprintf(logFile,  "(%s) (building destroyed! [%d+%d<%d]) (switching to subphase 2).\n",
				project->debugName, real, site, project->amount);
		}
	}
	else if (project->subPhase==6)
	{
		// balance final workers:
		
		if (project->blocking)
		{
			fprintf(logFile,  "(%s) (deblocking [%d, %d])\n", project->debugName, project->blocking, project->critical);
			project->blocking=false;
			project->critical=false;
		}
		
		if (project->finalWorkers>=0)
		{
			Sint32 finalWorkers=project->finalWorkers;
			
			Building **myBuildings=team->myBuildings;
			for (int i=0; i<Building::MAX_COUNT; i++)
			{
				Building *b=myBuildings[i];
				if (b && b->type->shortTypeNum==project->shortTypeNum && b->maxUnitWorking!=finalWorkers)
				{
					assert(b->type->maxUnitWorking!=0);
					fprintf(logFile,  "(%s) (set finalWorkers [current=%d, final=%d])\n",
						project->debugName, b->maxUnitWorking, finalWorkers);
					b->maxUnitWorking=finalWorkers;
					b->maxUnitWorkingLocal=finalWorkers;
					b->update();
					project->timer=timer;
					return shared_ptr<Order>(new OrderModifyBuilding(b->gid, finalWorkers));
				}
			}
		}
		if (buildingSum[project->shortTypeNum][1]==0)
		{
			project->finished=true;
			fprintf(logFile,  "(%s) (all finalWorkers set) (project succeded)\n", project->debugName);
		}
	}
	else
		assert(false);
	
	return shared_ptr<Order>();
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
	if (foodLock)
		minBalance+=3;
	int minOverWorkers=minBalance+partFree;
	
	bool enough=(workersBalance>minBalance);
	overWorkers=(workersBalance>minOverWorkers);
	
	assert(buildsAmount<1024);
	static int oldEnough[1024];
	static bool first=true;
	if (first)
	{
		memset(oldEnough, 2, 1024*sizeof(*oldEnough));
		first=false;
	}
	if ((oldEnough[buildsAmount]==2) || (enough!=oldEnough[buildsAmount]))
	{
		fprintf(logFile,  "enoughFreeWorkers()=%d, workersBalance=%d, totalWorkers=%d, partFree=%d, buildsAmount=%d, minBalance=%d\n",
			enough, workersBalance, totalWorkers, partFree, buildsAmount, minBalance);
		oldEnough[buildsAmount]=enough;
	}
	return enough;
}

void AICastor::computeCanSwim()
{
	//printf("computeCanSwim()...\n");
	// If our population has more healthy-working-units able to swimm than healthy-working-units
	// unable to swimm then we choose to be able to go trough water:
	Unit **myUnits=team->myUnits;
	int sumCanSwim=0;
	int sumCantSwim=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
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
	fprintf(logFile,  "needSwim=%d\n", needSwim);
	
	computeCanSwim();
}

void AICastor::computeBuildingSum()
{
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<4; li++)
				buildingLevels[bi][si][li]=0;
	
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			if (b->buildingState==Building::WAITING_FOR_CONSTRUCTION && b->constructionResultState==Building::UPGRADE)
				buildingLevels[b->type->shortTypeNum][1][b->type->level+1]++;
			else
				buildingLevels[b->type->shortTypeNum][b->type->isBuildingSite][b->type->level]++;
		}
	}
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		for (int si=0; si<2; si++)
		{
			int sum=0;
			for (int li=0; li<4; li++)
				sum+=buildingLevels[bi][si][li];
			buildingSum[bi][si]=sum;
		}
	
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<4; li++)
				if (buildingLevels[bi][si][li]>0)
					if ((timer&8191)==0)
						if (verbose)
							printf("buildingLevels[%d][%d][%d]=%d\n", bi, si, li, buildingLevels[bi][si][li]);
}

void AICastor::computeWarLevel()
{
	if (timer>strategy.warTimeTrigger)
	{
		fprintf(logFile,  "timer=%d, strategy.warTimeTrigger=%d\n", timer, strategy.warTimeTrigger);
		warTimeTriggerLevel++;
		strategy.warTimeTrigger=strategy.warTimeTrigger+((1+strategy.warTimeTrigger)>>1);
	}
	int warTimeTriggerLevelUse=warTimeTriggerLevel;
	if (warTimeTriggerLevelUse>2)
		warTimeTriggerLevelUse=2;
	
	int sum=0;
	for (int si=0; si<2; si++)
		for (int li=strategy.warLevelTrigger; li<4; li++)
			sum+=buildingLevels[IntBuildingType::ATTACK_BUILDING][si][li];
	if (sum>1)
		warLevelTriggerLevel=2;
	else if (sum>0)
		warLevelTriggerLevel=1;
	else
		warLevelTriggerLevel=0;
	
	if (buildsAmount>strategy.warAmountTrigger)
		warAmountTriggerLevel=2;
	else if (buildsAmount>=strategy.warAmountTrigger)
		warAmountTriggerLevel=1;
	else
		warAmountTriggerLevel=0;
	warLevel=warTimeTriggerLevelUse+warLevelTriggerLevel+warAmountTriggerLevel;
	
	static int oldWarLevel=-1;
	if (oldWarLevel!=warLevel)
	{
		fprintf(logFile,  "warLevel=%d, warTimeTriggerLevelUse=%d, warLevelTriggerLevel=%d, warAmountTriggerLevel=%d\n",
			warLevel, warTimeTriggerLevelUse, warLevelTriggerLevel, warAmountTriggerLevel);
		oldWarLevel=warLevel;
	}
	
	if (warLevel==0)
		return;
	
	int warPowerSum=0;
	Unit **myUnits=team->myUnits;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->medical==Unit::MED_FREE && u->typeNum==WARRIOR)
			warPowerSum+=u->performance[ATTACK_SPEED]*u->performance[ATTACK_STRENGTH];
	}
	static int oldWarPowerSum=-1;
	if (oldWarPowerSum!=warPowerSum)
	{
		fprintf(logFile,  "warPowerSum=%d\n", warPowerSum);
		oldWarPowerSum=warPowerSum;
	}
	
	if (warPowerSum<strategy.strikeWarPowerTriggerDown)
	{
		if (onStrike)
		{
			strikeTeamSelected=false;
			onStrike=false;
			
			strikeTimeTrigger=timer+strategy.strikeTimeTrigger;
			strategy.strikeWarPowerTriggerUp=strategy.strikeWarPowerTriggerUp+strategy.strikeWarPowerTriggerUp/2;
			fprintf(logFile,  " strategy.strikeWarPowerTriggerUp=%d\n", strategy.strikeWarPowerTriggerUp);
		}
	}
	else if (timer>strikeTimeTrigger || warPowerSum>strategy.strikeWarPowerTriggerUp)
	{
		onStrike=true;
	}
}

void AICastor::computeObstacleUnitMap()
{
	//printf("computeObstacleUnitMap()...\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	const auto& cases=map->cases;
	Uint32 teamMask=team->me;
	for (size_t i=0; i<size; i++)
	{
		const auto& c=cases[i];
		if (c.building!=NOGBID)
			obstacleUnitMap[i]=0;
		else if (c.ressource.type!=NO_RES_TYPE)
			obstacleUnitMap[i]=0;
		else if (c.forbidden&teamMask)
			obstacleUnitMap[i]=0;
		else if (!canSwim && (c.terrain>=256) && (c.terrain<256+16)) // !canSwim && isWatter ?
			obstacleUnitMap[i]=0;
		else
			obstacleUnitMap[i]=1;
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
	size_t size=w*h;
	const auto& cases=map->cases;
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.building!=NOGBID)
			obstacleBuildingMap[i]=0;
		else  if (c.terrain>=16) // if (!isGrass)
			obstacleBuildingMap[i]=0;
		else if (c.ressource.type!=NO_RES_TYPE)
			obstacleBuildingMap[i]=0;
		else
			obstacleBuildingMap[i]=1;
	}
	//printf("...computeObstacleBuildingMap() done\n");
}

void AICastor::computeSpaceForBuildingMap(int max)
{
	//printf("computeSpaceForBuildingMap()...\n");
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	memcpy(spaceForBuildingMap, obstacleBuildingMap, size);
	
	for (int i=1; i<max; i++)
	{
		for (int y=0; y<h; y++)
		{
			int wy0=w*y;
			int wy1=w*((y+1)&hMask);
			
			for (int x=0; x<w; x++)
			{
				int wyx[4];
				wyx[0]=wy0+x+0;
				wyx[1]=wy0+((x+1)&wMask);
				wyx[2]=wy1+x+0;
				wyx[3]=wy1+((x+1)&wMask);
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

void AICastor::computeBuildingNeighbourMapOfBuilding(int bx, int by, int bw, int bh, int dw, int dh)
{
	//int w=map->w;
	//int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	//size_t size=w*h;
	Uint8 *gradient=buildingNeighbourMap;
	const auto& cases=map->cases;
	
	//Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	
	/*int bx=b->posX;
	int by=b->posY;
	int bw=b->type->width;
	int bh=b->type->height;*/
	
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
	
	// At a range of 0 space case (neighbours), without corners,
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

void AICastor::computeBuildingNeighbourMap(int dw, int dh)
{
	int w=map->w;
	int h=map->h;
	//size_t size=w*h;
	
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	int wMask=map->wMask;
	int hMask=map->hMask;
	
	Uint8 *gradient=buildingNeighbourMap;
	//memset(gradient, 0, size);
	Uint32 visionMask=team->me;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			for (int dy=0; dy<dh; dy++)
				for (int dx=0; dx<dw; dx++)
				{
					size_t index=(((y+dy)&hMask)<<wDec)+((x+dw)&wMask);
					if ((map->mapDiscovered[index]&visionMask))
						goto doubleBreak;
				}
			gradient[(y<<wDec)+x]=127;
			continue;
		doubleBreak:
			gradient[(y<<wDec)+x]=0;
		}
	
	Game *game=team->game;
	for (Sint32 ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
	{
		Team *team=game->teams[ti];
		assert(team);
		if (!team)
			continue;
		Building **myBuildings=team->myBuildings;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=myBuildings[i];
			if (b && !b->type->isVirtual)
			{
				int bx=b->posX;
				int by=b->posY;
				int bw=b->type->width;
				int bh=b->type->height;
				computeBuildingNeighbourMapOfBuilding(bx, by, bw, bh, dw, dh);
			}
		}
	}
	
	for (std::list<Game::BuildProject>::iterator bpi=game->buildProjects.begin(); bpi!=game->buildProjects.end(); bpi++)
	{
		int bx=bpi->posX&map->getMaskW();
		int by=bpi->posY&map->getMaskH();
		//int teamNumber=bpi->teamNumber;
		Sint32 typeNum=(bpi->typeNum);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
		int bw=bt->width;
		int bh=bt->height;
		computeBuildingNeighbourMapOfBuilding(bx, by, bw, bh, dw, dh);
	}
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
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>1)/u->race->hungryness;
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
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>1)/u->race->hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>255)
				range=255;
			int index=(u->posX&wMask)+((u->posY&hMask)<<wDec);
			gradient[index]=(Uint8)range;
		}
	}
	
	updateGlobalGradient(gradient);
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
fprintf(logFile,  "computeHydratationMap()...\n");
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	
	Uint16 *gradient=(Uint16 *)malloc(2*size);
	memset(gradient, 0, 2*size);
	const auto& cases=map->cases;
	static const int range=16;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			Uint16 t=cases[x+(y<<wDec)].terrain;
			if ((t>=256)&&(t<256+16)) // if SAND
				for (int r=1; r<range; r++)
				{
					for (int dx=-r; dx<=r; dx++)
					{
						Uint16 *gp=&gradient[((x+dx)&wMask)+(((y -r)&hMask)<<wDec)];
						*gp+=(range-r);
					}
					for (int dx=-r; dx<=r; dx++)
					{
						Uint16 *gp=&gradient[((x+dx)&wMask)+(((y +r)&hMask)<<wDec)];
						*gp+=(range-r);
					}
					for (int dy=(1-r); dy<r; dy++)
					{
						Uint16 *gp=&gradient[((x -r)&wMask)+(((y+dy)&hMask)<<wDec)];
						*gp+=(range-r);
					}
					for (int dy=(1-r); dy<r; dy++)
					{
						Uint16 *gp=&gradient[((x +r)&wMask)+(((y+dy)&hMask)<<wDec)];
						*gp+=(range-r);
					}
				}
		}
	for (size_t i=0; i<size; i++)
	{
		Uint16 value=gradient[i]>>4;
		if (value<255)
			hydratationMap[i]=value;
		else
			hydratationMap[i]=255;
	}
	free(gradient);
	fprintf(logFile,  "...computeHydratationMap() done\n");
}

void AICastor::computeNotGrassMap()
{
	fprintf(logFile,  "computeNotGrassMap()...\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	
	memset(notGrassMap, 0, size);
	
	const auto& cases=map->cases;
	for (size_t i=0; i<size; i++)
	{
		Uint16 t=cases[i].terrain;
		if (t>16)// if !GRASS
			notGrassMap[i]=16;
	}
	
	updateGlobalGradientNoObstacle(notGrassMap);
	fprintf(logFile,  "...computeNotGrassMap() done\n");
}

void AICastor::computeWheatCareMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	size_t sizeMask=(size-1);
	//Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	//Case *cases=map->cases;
	//Uint32 teamMask=team->me;
	
	Uint8 *temp=wheatCareMap[1];
	wheatCareMap[1]=wheatCareMap[0];
	wheatCareMap[0]=temp;
	
	memcpy(wheatCareMap[0], obstacleUnitMap, size);
	for (size_t i=0; i<=sizeMask; i++)
		if (wheatCareMap[0][i]!=0 && notGrassMap[i]==15 && hydratationMap[i]>0
			&& ((wheatCareMap[1][i]>7)
				|| ((oldWheatGradient[3][i]==255 || oldWheatGradient[2][i]==255) && (oldWheatGradient[1][i]<255 || oldWheatGradient[0][i]<255))))
		{
			if (oldWheatGradient[1][i]<254 || oldWheatGradient[0][i]<254)
				wheatCareMap[0][i]=10;
			else
				wheatCareMap[0][i]=8;
		}
	map->updateGlobalGradientSlow(wheatCareMap[0]);
}

void AICastor::computeWheatGrowthMap()
{
	if (lastWheatGrowthMapComputed==timer)
		return;
	
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	
	memcpy(wheatGrowthMap, obstacleBuildingMap, size);
	
	for (size_t i=0; i<size; i++)
		if (wheatGradient[i]==255)
			wheatGrowthMap[i]=1+(hydratationMap[i]>>3);
	
	map->updateGlobalGradientSlow(wheatGrowthMap);
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 care=wheatCareMap[0][i];
		if (care>1)
		{
			Uint8 *p=&wheatGrowthMap[i];
			Uint8 growth=*p;
			if (growth>care)
				(*p)=growth-care;
			else
				(*p)=1;
		}
	}
	lastWheatGrowthMapComputed=timer;
}

void AICastor::computeEnemyPowerMap()
{
	if (lastEnemyPowerMapComputed==timer)
		return;
	lastEnemyPowerMapComputed=timer;
	
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=enemyPowerMap;
	
	memset(gradient, 0, size);
	
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
			if (b==NULL || ((b->seenByMask&me)==0))
				continue;
			int bx=b->posX;
			int by=b->posY;
			static const int reducer=3;
			static const int range=32; // max 32
			{
				Uint8 *gp=&gradient[(bx&wMask)+((by&hMask)<<wDec)];
				Uint16 sum=*gp+(range>>reducer);
				if (sum>255)
					sum=255;
				*gp=sum;
			}
			for (int r=1; r<range; r++)
			{
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((bx+dx)&wMask)+(((by -r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((bx+dx)&wMask)+(((by +r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((bx -r)&wMask)+(((by+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((bx +r)&wMask)+(((by+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
			}
		}
	}
}

void AICastor::computeEnemyRangeMap()
{
	if (lastEnemyRangeMapComputed==timer)
		return;
	lastEnemyRangeMapComputed=timer;
	
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=enemyRangeMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	
	for (int ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
	{
		Team *enemyTeam=game->teams[ti];
		Uint32 me=team->me;
		
		if ((team->enemies & enemyTeam->me)==0)
			continue;
		Building **enemyBuildings=enemyTeam->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			Building *b=enemyBuildings[bi];
			if (b==NULL || ((b->seenByMask&me)==0) || b->type->isBuildingSite)
				continue;
			int bx=b->posX;
			int by=b->posY;
			int bw=b->type->width;
			int bh=b->type->height;
			for (int dy=by; dy<by+bh; dy++)
				for (int dx=bx; dx<bx+bw; dx++)
					gradient[(dx&wMask)+((dy&hMask)<<wDec)]=255;
		}
	}
	
	map->updateGlobalGradientSlow(gradient);
}

void AICastor::computeEnemyWarriorsMap()
{
	if (lastEnemyWarriorsMapComputed==timer)
		return;
	lastEnemyWarriorsMapComputed=timer;
	if (verbose)
		printf("computeEnemyWarriorsMap()\n");
	
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=enemyWarriorsMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	for (size_t i=0; i<size; i++)
	{
		if ((map->fogOfWar[i]&team->me)==0)
			continue;
		Uint16 guid=map->cases[i].groundUnit;
		if (guid==NOGUID)
			continue;
		Uint32 teamMask=(1<<(guid>>10));
		if ((teamMask&team->enemies)==0)
			continue;
		gradient[i]=32;
	}
	map->updateGlobalGradientSlow(gradient);
}

boost::shared_ptr<Order>AICastor::findGoodBuilding(Sint32 typeNum, bool food, bool defense, bool critical)
{
	int w=map->w;
	int h=map->h;
	int bw=globalContainer->buildingsTypes.get(typeNum)->width;
	int bh=globalContainer->buildingsTypes.get(typeNum)->height;
	assert(bw==bh);
	//int hDec=map->hDec;
	int wDec=map->wDec;
	int wMask=map->wMask;
	int hMask=map->hMask;
	size_t size=w*h;
	Uint32 *mapDiscovered=&(map->mapDiscovered[0]);
	Uint32 me=team->me;
	fprintf(logFile,  "findGoodBuilding(%d, %d, %d) b=(%d, %d)\n", typeNum, food, critical, bw, bh);
	
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
	fprintf(logFile,  " bestWorkScore=%d, minWork=%d\n", bestWorkScore, minWork/4);
	
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
	fprintf(logFile,  " wheatGradientLimit=%d\n", wheatGradientLimit/4);
	
	// we find the best place possible:
	size_t bestIndex=0;
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
			if (!defense)
			{
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
			}
			//goodBuildingMap[corner0]=4;
			
			Uint32 enemyRange=enemyRangeMap[corner0]+enemyRangeMap[corner1]+enemyRangeMap[corner2]+enemyRangeMap[corner3];
			if (enemyRange>4*(255-8))
				continue;
			//goodBuildingMap[corner0]=5;
			
			Sint32 wheatGrowth=wheatGrowthMap[corner0]+wheatGrowthMap[corner1]+wheatGrowthMap[corner2]+wheatGrowthMap[corner3];
			
			Uint8 neighbour=buildingNeighbourMap[corner0];
			Uint8 directNeighboursCount=(neighbour>>1)&7; // [0, 7]
			Uint8 farNeighboursCount=(neighbour>>5)&7; // [0, 7]
			if ((neighbour&1)||(directNeighboursCount>1))
				continue;
			
			//goodBuildingMap[corner0]=6;
			
			Sint32 score;
			if (defense)
				score=((work<<1)+wheatGradient+(enemyRange<<4))*(16+(directNeighboursCount<<2)+farNeighboursCount);
			else if (food)
				score=((wheatGrowth<<8)+work+(wheatGradient>>1)-enemyRange)*(8+(directNeighboursCount<<2)+farNeighboursCount);
			else
				score=(4096+work-(wheatGrowth<<8)-enemyRange)*(8+(directNeighboursCount<<2)+farNeighboursCount);
			
			if (defense)
			{
				if (score<0)
					goodBuildingMap[corner0]=0;
				else if ((score>>12)>=255)
					goodBuildingMap[corner0]=255;
				else
					goodBuildingMap[corner0]=(score>>12);
			}
			
			if (bestScore<score)
			{
				bestScore=score;
				bestIndex=corner0;
			}
		}
	
	if (bestScore>0)
	{
		fprintf(logFile,  " found a cool place");
		fprintf(logFile,  "  score=%d, wheatGrowth=%d, wheatGradientMap=%d, work=%d\n",
			bestScore, wheatGrowthMap[bestIndex], wheatGradientMap[bestIndex], workAbilityMap[bestIndex]);
		
		Uint8 neighbour=buildingNeighbourMap[bestIndex];
		Uint8 directNeighboursCount=(neighbour>>1)&7; // [0, 7]
		Uint8 farNeighboursCount=(neighbour>>5)&7; // [0, 7]
			
		fprintf(logFile,  " directNeighboursCount=%d, farNeighboursCount=%d\n",
			directNeighboursCount, farNeighboursCount);
		
		Sint32 x=(bestIndex&map->wMask);
		Sint32 y=((bestIndex>>map->wDec)&map->hMask);
		return shared_ptr<Order>(new OrderCreate(team->teamNumber, x, y, typeNum, 1, 1));
	}
	
	return shared_ptr<Order>();
}

void AICastor::computeRessourcesCluster()
{
	fprintf(logFile,  "computeRessourcesCluster()\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
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
			const auto& c = map->cases[map->coordToIndex(x, y)]; // case
			const auto& r=c.ressource; // ressource
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
				fprintf(logFile,  "ressource rt=%d, at (%d, %d)\n", rt, x, y);
				if (rt!=old)
				{
					fprintf(logFile,  " rt!=old\n");
					id=1;
					while (usedid[id])
						id++;
					if (id)
						usedid[id]=true;
					old=rt;
					fprintf(logFile,  "  id=%d\n", id);
				}
				if (rc!=id)
				{
					if (rc==0)
					{
						*rcp=id;
						fprintf(logFile,  " wrote.\n");
					}
					else
					{
						Uint16 oldid=id;
						usedid[oldid]=false;
						id=rc; // newid
						fprintf(logFile,  " cleaning oldid=%d to id=%d.\n", oldid, id);
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
	fprintf(logFile,  "computeRessourcesCluster(), used=%d\n", used);
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
				max++;

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
				max++;

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
				max++;

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
				max++;

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

void AICastor::updateGlobalGradient(Uint8 *gradient)
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
			if (max && max!=255)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
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
			if (max && max!=255)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
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
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
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
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];
				max++;

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}
