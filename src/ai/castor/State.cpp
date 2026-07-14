// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

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
	else if (buildsAmount<=AI_CASTOR_BUILDS_LOW)
		minBalance=0;
	else if (buildsAmount<=AI_CASTOR_BUILDS_MID)
		minBalance=partFree;
	else
		minBalance=(partFree<<AI_CASTOR_BALANCE_LATE_SHIFT);
	if (foodLock)
		minBalance+=AI_CASTOR_FOODLOCK_BALANCE_BIAS;
	int minOverWorkers=minBalance+partFree;

	bool enough=(workersBalance>minBalance);
	overWorkers=(workersBalance>minOverWorkers);

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
	
	needSwim=((baseCount<<AI_CASTOR_SWIM_GAIN_NUMER_SHIFT)>(AI_CASTOR_SWIM_GAIN_DENOM*extendedCount));

	computeCanSwim();
}

void AICastor::computeBuildingSum()
{
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<NB_UNIT_LEVELS; li++)
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
			for (int li=0; li<NB_UNIT_LEVELS; li++)
				sum+=buildingLevels[bi][si][li];
			buildingSum[bi][si]=sum;
		}

	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<NB_UNIT_LEVELS; li++)
				if (buildingLevels[bi][si][li]>0)
					if ((timer&AI_CASTOR_VERBOSE_LOG_INTERVAL_MASK)==0)
						if (verbose)
							printf("buildingLevels[%d][%d][%d]=%d\n", bi, si, li, buildingLevels[bi][si][li]);
}

void AICastor::computeWarLevel()
{
	if (timer>strategy.warTimeTrigger)
	{
		warTimeTriggerLevel++;
		strategy.warTimeTrigger=strategy.warTimeTrigger+((AI_CASTOR_WARTIME_TRIGGER_GROWTH_BIAS+strategy.warTimeTrigger)>>AI_CASTOR_WARTIME_TRIGGER_GROWTH_SHIFT);
	}
	int warTimeTriggerLevelUse=warTimeTriggerLevel;
	if (warTimeTriggerLevelUse>AI_CASTOR_WARTIME_LEVEL_CAP)
		warTimeTriggerLevelUse=AI_CASTOR_WARTIME_LEVEL_CAP;

	int sum=0;
	for (int si=0; si<2; si++)
		for (int li=strategy.warLevelTrigger; li<NB_UNIT_LEVELS; li++)
			sum+=buildingLevels[IntBuildingType::ATTACK_BUILDING][si][li];
	if (sum>AI_CASTOR_WARLEVEL_BUILDINGS_HIGH)
		warLevelTriggerLevel=AI_CASTOR_WAR_LEVEL_HIGH;
	else if (sum>0)
		warLevelTriggerLevel=AI_CASTOR_WAR_LEVEL_MID;
	else
		warLevelTriggerLevel=0;

	if (buildsAmount>strategy.warAmountTrigger)
		warAmountTriggerLevel=AI_CASTOR_WAR_LEVEL_HIGH;
	else if (buildsAmount>=strategy.warAmountTrigger)
		warAmountTriggerLevel=AI_CASTOR_WAR_LEVEL_MID;
	else
		warAmountTriggerLevel=0;
	warLevel=warTimeTriggerLevelUse+warLevelTriggerLevel+warAmountTriggerLevel;

	static int oldWarLevel=AI_CASTOR_WAR_LEVEL_UNSET;
	if (oldWarLevel!=warLevel)
	{
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
	static int oldWarPowerSum=AI_CASTOR_WAR_POWER_UNSET;
	if (oldWarPowerSum!=warPowerSum)
	{
		oldWarPowerSum=warPowerSum;
	}

	if (warPowerSum<strategy.strikeWarPowerTriggerDown)
	{
		if (onStrike)
		{
			strikeTeamSelected=false;
			onStrike=false;

			strikeTimeTrigger=timer+strategy.strikeTimeTrigger;
			strategy.strikeWarPowerTriggerUp=strategy.strikeWarPowerTriggerUp+strategy.strikeWarPowerTriggerUp/AI_CASTOR_STRIKE_TRIGGER_GROWTH_DIV;
		}
	}
	else if (timer>strikeTimeTrigger || warPowerSum>strategy.strikeWarPowerTriggerUp)
	{
		onStrike=true;
	}
}

