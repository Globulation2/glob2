// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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

using std::shared_ptr;

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

