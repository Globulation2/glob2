// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Stream.h>

#include "AICastor.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

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
	amount=AI_CASTOR_PROJECT_DEFAULT_AMOUNT;
	food=(this->shortTypeNum==IntBuildingType::SWARM_BUILDING
		|| this->shortTypeNum==IntBuildingType::FOOD_BUILDING);
	defense=(this->shortTypeNum==IntBuildingType::DEFENSE_BUILDING);

	debugStdName += IntBuildingType::typeFromShortNumber(this->shortTypeNum);
	debugStdName += "-";
	debugStdName += suffix;
	this->debugName=debugStdName.c_str();

	//printf("new project(%s)\n", debugName);

	subPhase=AI_CASTOR_SUBPHASE_BOOT;

	successWait=0;
	blocking=true;
	critical=false;
	priority=AI_CASTOR_PROJECT_DEFAULT_PRIORITY;
	triesLeft=AI_CASTOR_PROJECT_TRIES_LEFT;

	mainWorkers=AI_CASTOR_WORKERS_UNSET;
	foodWorkers=AI_CASTOR_WORKERS_UNSET;
	otherWorkers=AI_CASTOR_WORKERS_UNSET;

	multipleStart=false;
	waitFinished=false;
	finalWorkers=AI_CASTOR_WORKERS_UNSET;

	finished=false;

	timer=AI_CASTOR_TIMER_NEVER;
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
	firstInit();
	init(player);
}

void AICastor::init(Player *player)
{
	assert(player);
	
	// Logical :
	timer=0;
	canSwim=false;
	needSwim=false;
	lastFreeWorkersComputed=AI_CASTOR_TIMER_NEVER;
	lastWheatGrowthMapComputed=AI_CASTOR_TIMER_NEVER;
	lastEnemyRangeMapComputed=AI_CASTOR_TIMER_NEVER;
	lastEnemyPowerMapComputed=AI_CASTOR_TIMER_NEVER;
	lastEnemyWarriorsMapComputed=AI_CASTOR_TIMER_NEVER;
	computeNeedSwimTimer=0;
	controlSwarmsTimer=0;
	expandFoodTimer=0;
	controlFoodTimer=0;
	controlUpgradeTimer=0;
	controlUpgradeDelay=AI_CASTOR_UPGRADE_DELAY_TICKS;
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
	init(player);
	assert(game);
	
	stream->readEnterSection("AICastor");
	Sint32 aiFileVersion = stream->readSint32("aiFileVersion");
	if (aiFileVersion<AI_FILE_MIN_VERSION)
	{
		fprintf(stderr, " error: aiFileVersion=%d<AI_FILE_MIN_VERSION=%d\n", aiFileVersion, AI_FILE_MIN_VERSION);
		stream->readLeaveSection();
		return false;
	}
	if (aiFileVersion>=1)
		timer = stream->readUint32("timer");
	else
		timer=0;
		
	stream->readLeaveSection();
	return true;
}

void AICastor::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AICastor");
	stream->writeSint32(AI_FILE_VERSION, "aiFileVersion");
	stream->writeUint32(timer, "timer");
	stream->writeLeaveSection();
}
