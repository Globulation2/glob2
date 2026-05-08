// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <Stream.h>

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


std::shared_ptr<Order>AICastor::getOrder()
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
		std::shared_ptr<Order>order = controlBaseDefense();
		if (order)
			return order;
	}*/
		
	//printf("getOrder(), %d projects\n", projects.size());
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end();)
		if ((*pi)->finished)
		{
			//printf("deleting project (%s)\n", (*pi)->debugName);
			delete *pi;
			pi=projects.erase(pi);
		}
		else
			pi++;
	bool blocking=false;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if ((*pi)->blocking)
			blocking=true;
	
	computeBuildingSum();
	
	if (!blocking)// No blocking project, we can start a new one:
		addProjects();
	Sint32 priority=AICastor::AI_CASTOR_PRIORITY_NONE;
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (priority>(*pi)->priority && (*pi)->critical)
			priority=(*pi)->priority;
	
	if (timer>controlSwarmsTimer)
	{
		computeWarLevel();
		controlSwarmsTimer=timer+256; // each 10s
		std::shared_ptr<Order>order=controlSwarms();
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
				std::shared_ptr<Order>order=continueProject(*pi);
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
				std::shared_ptr<Order>order=continueProject(*pi);
				if (order)
					return order;
			}
		}
	
	if (priority>0 && timer>expandFoodTimer)
	{
		expandFoodTimer=timer+256; // each 10s
		std::shared_ptr<Order>order=expandFood();
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
		std::shared_ptr<Order>order=controlFood();
		if (order)
			return order;
	}
	
	if (priority>0)
	{
		std::shared_ptr<Order>order=controlUpgrades();
		if (order)
			return order;
	}
	
	if (timer>controlStrikesTimer)
	{
		std::shared_ptr<Order>order=controlStrikes();
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

