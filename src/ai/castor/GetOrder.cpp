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


std::shared_ptr<Order>AICastor::getOrder()
{
	timer++;
	
	if (!strategy.defined)
		defineStrategy();
	
	if (computeBoot<AI_CASTOR_BOOT_IDLE_TICKS)
	{
		computeBoot++;
		return shared_ptr<Order>(new NullOrder());
	}
	else if (computeBoot<AI_CASTOR_BOOT_COMPUTE_STEPS+AI_CASTOR_BOOT_IDLE_TICKS)
	{
		switch (computeBoot-AI_CASTOR_BOOT_IDLE_TICKS)
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
	
	if ((timer&AI_CASTOR_WHEAT_HISTORY_INTERVAL_MASK)==0)
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
		controlSwarmsTimer=timer+AI_CASTOR_CONTROL_SWARMS_INTERVAL; // each 10s
		std::shared_ptr<Order>order=controlSwarms();
		if (order)
			return order;
	}
	
	//bool critical=false;
	//for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
	//	if ((*pi)->critical)
	//		critical=true;
	
	int minReal=Building::MAX_COUNT;
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
		expandFoodTimer=timer+AI_CASTOR_EXPAND_FOOD_INTERVAL; // each 10s
		std::shared_ptr<Order>order=expandFood();
		if (order)
			return order;
	}
	
	if (timer>lastEnemyRangeMapComputed+AI_CASTOR_ENEMY_RANGE_REFRESH) // each 41s
	{
		computeEnemyRangeMap();
	}
	if (timer>lastEnemyWarriorsMapComputed+AI_CASTOR_ENEMY_WARRIORS_REFRESH) // each 41s
	{
		computeEnemyWarriorsMap();
	}

	/*if (onStrike)
	{
		if (timer>lastEnemyPowerMapComputed+AI_CASTOR_ENEMY_POWER_STRIKE_REFRESH) // each 5s
			computeEnemyPowerMap();
	}
	else
	{
		if (timer>lastEnemyPowerMapComputed+AI_CASTOR_ENEMY_POWER_IDLE_REFRESH) // each 2min44s
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

// Default build-policy table for AICastor::defineStrategy().
//
// One row per hard building, indexed by IntBuildingType::Number 0..7
// (SWARM, FOOD, HEAL, WALKSPEED, SWIMSPEED, ATTACK, SCIENCE, DEFENSE).
// Field names mirror Strategy::Build exactly so the table copies into
// strategy.build[i] field-for-field.
//
// finalWorkers is -1 for HEAL / WALKSPEED / SWIMSPEED / ATTACK / SCIENCE
// because the original defineStrategy() left those slots at the -1 set
// by the pre-fill loop (only SWARM, FOOD, DEFENSE were re-assigned).
// Encoding -1 explicitly here preserves identical post-init state.
//
// Behavior is byte-for-byte preserved vs the previous column-by-column
// init in GetOrder.cpp:257-333. Network checksums and replay output
// are unaffected.
namespace
{
	struct CastorStrategyDefaults
	{
		Sint32 successWait;
		Sint32 isFreePart;
		Sint32 warLevelTrigger;
		Uint32 warTimeTrigger;
		Sint32 warAmountTrigger;
		Sint32 strikeWarPowerTriggerUp;
		Sint32 strikeWarPowerTriggerDown;
		Uint32 strikeTimeTrigger;
		Sint32 maxAmountGoal;
	};

	// Per-hard-building base/new policy table.
	// Column order matches Strategy::Build field order.
	// Row order matches IntBuildingType::Number 0..7.
	static constexpr AICastor::Strategy::Build DEFAULT_BUILD_POLICIES[AICastor::NB_HARD_BUILDING] =
	{
		// baseOrder, base, baseWorkers, baseUpgrade, finalWorkers, newOrder, news, newWorkers, newUpgrade
		/* 0 SWARM_BUILDING     */ { 1, 2, 2, 0,  2, 1,  1, 3,  0 },
		/* 1 FOOD_BUILDING      */ { 4, 4, 3, 2,  1, 2,  7, 2,  3 },
		/* 2 HEAL_BUILDING      */ { 5, 2, 1, 2, -1, 5,  5, 2,  5 },
		/* 3 WALKSPEED_BUILDING */ { 7, 1, 5, 0, -1, 6,  1, 4,  0 },
		/* 4 SWIMSPEED_BUILDING */ { 6, 1, 3, 0, -1, 7,  1, 4,  0 },
		/* 5 ATTACK_BUILDING    */ { 2, 2, 2, 2, -1, 4,  2, 5,  2 },
		/* 6 SCIENCE_BUILDING   */ { 0, 2, 5, 2, -1, 3,  2, 7,  2 },
		/* 7 DEFENSE_BUILDING   */ { 3, 2, 2, 1,  2, 0, 10, 4, 10 },
	};

	// Scalar strategy defaults set once per game by defineStrategy().
	// strikeTimeTrigger = 32768 ticks ≈ 21 min 51 s.
	// isFreePart = 10 (denominator for "1/N of pop = excess"; "good in [3..20]" per source comment).
	static constexpr CastorStrategyDefaults DEFAULTS =
	{
		/* successWait               */ 0,     // TODO: use a "lowDiscovered" flag instead
		/* isFreePart                */ 10,    // good in [3..20]
		/* warLevelTrigger           */ 1,
		/* warTimeTrigger            */ 8192,
		/* warAmountTrigger          */ 3,
		/* strikeWarPowerTriggerUp   */ 4096,
		/* strikeWarPowerTriggerDown */ 2048,
		/* strikeTimeTrigger         */ 32768, // 21 min 51 s
		/* maxAmountGoal             */ 10,
	};
}

void AICastor::defineStrategy()
{
	strategy.defined=true;

	// Pre-fill all NB_BUILDING (=13) slots, including the non-hard
	// EXPLORATION_FLAG / WAR_FLAG / CLEARING_FLAG / STONE_WALL /
	// MARKET_BUILDING entries, with the "unset" sentinel for the three
	// fields the original code touched in this pre-pass. The remaining
	// six Build fields stay uninitialized for slots 8..12, matching the
	// pre-refactor behavior (Strategy::Build has no default ctor).
	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
	{
		strategy.build[bi].baseOrder    = -1;
		strategy.build[bi].newOrder     = -1;
		strategy.build[bi].finalWorkers = -1;
	}

	// Apply the per-hard-building default policy table to slots 0..7.
	for (int bi=0; bi<NB_HARD_BUILDING; bi++)
		strategy.build[bi] = DEFAULT_BUILD_POLICIES[bi];

	strategy.successWait              = DEFAULTS.successWait;
	strategy.isFreePart               = DEFAULTS.isFreePart;

	strategy.warLevelTrigger          = DEFAULTS.warLevelTrigger;
	strategy.warTimeTrigger           = DEFAULTS.warTimeTrigger;
	strategy.warAmountTrigger         = DEFAULTS.warAmountTrigger;

	strategy.strikeWarPowerTriggerUp  = DEFAULTS.strikeWarPowerTriggerUp;
	strategy.strikeWarPowerTriggerDown= DEFAULTS.strikeWarPowerTriggerDown;
	strategy.strikeTimeTrigger        = DEFAULTS.strikeTimeTrigger;
	strikeTimeTrigger                 = strategy.strikeTimeTrigger;

	strategy.maxAmountGoal            = DEFAULTS.maxAmountGoal;
}

