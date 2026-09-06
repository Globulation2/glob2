// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

bool AICastor::addProject(Project *project)
{
	if (buildingSum[project->shortTypeNum][0]>=project->amount)
	{
		delete project;
		return false;
	}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (project->shortTypeNum==(*pi)->shortTypeNum)
		{
			if (project->amount<=(*pi)->amount)
			{
				(*pi)->timer=timer;
				delete project;
				return false;
			}
			else
			{
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
	
	buildsAmount=-1;
	
	if (buildingSum[IntBuildingType::FOOD_BUILDING][0]==0)
	{
		Project *project=new Project(IntBuildingType::FOOD_BUILDING, "boot");

		project->successWait=strategy.successWait;
		project->critical=true;
		project->priority=AI_CASTOR_PROJECT_PRIORITY_CRITICAL;
		project->food=true;

		project->mainWorkers=AI_CASTOR_BOOT_FOOD_MAIN_WORKERS;
		project->foodWorkers=AI_CASTOR_BOOT_FOOD_FOOD_WORKERS;
		project->otherWorkers=AI_CASTOR_BOOT_OTHER_WORKERS_OFF;

		project->multipleStart=true;
		project->waitFinished=true;
		project->finalWorkers=AI_CASTOR_BOOT_FOOD_FINAL_WORKERS;

		if (addProject(project))
			return;
	}
	if (buildingSum[IntBuildingType::SWARM_BUILDING][0]+buildingSum[IntBuildingType::SWARM_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::SWARM_BUILDING, "boot");

		project->successWait=strategy.successWait;
		project->critical=true;
		project->priority=AI_CASTOR_PROJECT_PRIORITY_CRITICAL;
		project->food=true;

		project->mainWorkers=AI_CASTOR_BOOT_SWARM_MAIN_WORKERS;
		project->foodWorkers=AI_CASTOR_BOOT_SWARM_FOOD_WORKERS;
		project->otherWorkers=AI_CASTOR_BOOT_OTHER_WORKERS_OFF;

		project->multipleStart=false;
		project->waitFinished=true;
		project->finalWorkers=AI_CASTOR_BOOT_SWARM_FINAL_WORKERS;

		if (addProject(project))
			return;
	}
	if (buildingSum[IntBuildingType::SWIMSPEED_BUILDING][0]+buildingSum[IntBuildingType::SWIMSPEED_BUILDING][1]==0)
	{
		if (timer>computeNeedSwimTimer)
		{
			computeNeedSwimTimer=timer+AI_CASTOR_NEED_SWIM_REFRESH;// every 41s
			computeNeedSwim();
		}
		if (needSwim)
		{
			Project *project=new Project(IntBuildingType::SWIMSPEED_BUILDING, AI_CASTOR_BOOT_SWIM_AMOUNT, AI_CASTOR_BOOT_SWIM_MAIN_WORKERS, "boot");
			project->successWait=strategy.successWait;
			project->critical=true;
			project->priority=AI_CASTOR_PROJECT_PRIORITY_CRITICAL;
			if (addProject(project))
				return;
		}
	}
	if (buildingSum[IntBuildingType::ATTACK_BUILDING][0]+buildingSum[IntBuildingType::ATTACK_BUILDING][1]==0)
	{
		Project *project=new Project(IntBuildingType::ATTACK_BUILDING, AI_CASTOR_BOOT_ATTACK_AMOUNT, AI_CASTOR_BOOT_ATTACK_MAIN_WORKERS, "boot");
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
		for (int li=AI_CASTOR_FIRST_UPGRADE_LEVEL; li<NB_UNIT_LEVELS; li++)
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
	
	for (Sint32 agi=1; agi<NB_UNIT_LEVELS; agi++)
	{
		buildsAmount=AI_CASTOR_BUILDS_TIER_BASE_PRE+(agi<<AI_CASTOR_BUILDS_TIER_SHIFT);
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
							amountGoal[bi], strategy.build[bi].newWorkers+(agi-AI_CASTOR_TIER_WORKERS_SCALE_BIAS), "loop");
						project->successWait=strategy.successWait;
						project->finalWorkers=strategy.build[bi].finalWorkers;
						if (addProject(project))
							return;
					}
		buildsAmount=AI_CASTOR_BUILDS_TIER_BASE_MID+(agi<<AI_CASTOR_BUILDS_TIER_SHIFT);

		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
			upgradeGoal[bi]+=strategy.build[bi].newUpgrade;
		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
		{
			int upgradeSum=0;
			for (int li=agi; li<NB_UNIT_LEVELS; li++)
				upgradeSum+=buildingLevels[bi][0][li];
			if (upgradeSum<upgradeGoal[bi])
				return;
		}

		buildsAmount=AI_CASTOR_BUILDS_TIER_BASE_POST+(agi<<AI_CASTOR_BUILDS_TIER_SHIFT);
	}
}

std::shared_ptr<Order>AICastor::continueProject(Project *project)
{
	// Phase alpha will make a new Food Building at any price.
	
	if (timer<project->timer+AI_CASTOR_PROJECT_STEP_INTERVAL)
		return shared_ptr<Order>();

	if (foodLock && !project->critical && project->shortTypeNum==IntBuildingType::SWARM_BUILDING)
	{
		if (starvingWarning)
			project->timer=timer+AI_CASTOR_SWARM_STARVE_BACKOFF; // 5min28s
		else
			project->timer=timer+AI_CASTOR_SWARM_FOODLOCK_BACKOFF; // 1min22s
		project->blocking=false;
		project->critical=false;
	}
	
	if (project->subPhase==AICastor::AI_CASTOR_SUBPHASE_BOOT)
	{
		// boot phase
		project->subPhase=AICastor::AI_CASTOR_SUBPHASE_CHECK_SITES;
	}
	else if (project->subPhase==AICastor::AI_CASTOR_SUBPHASE_FIND_PLACE)
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
		
		std::shared_ptr<Order>gfbm=findGoodBuilding(typeNum, project->food, project->defense, project->critical);
		project->timer=timer;
		if (gfbm)
		{
			if (project->successWait>0)
			{
				project->successWait--;
			}
			else
			{
				project->subPhase=AICastor::AI_CASTOR_SUBPHASE_CHECK_SITES;
				return gfbm;
			}
		}
		else if (project->triesLeft>0)
		{
			project->triesLeft--;
		}
		else
		{
			project->timer=timer+AI_CASTOR_PROJECT_ABORT_BACKOFF; // 5min27s
			project->blocking=false;
			project->critical=false;
		}
	}
	else if (project->subPhase==AICastor::AI_CASTOR_SUBPHASE_CHECK_SITES)
	{
		// do we have enough building sites ?

		int real=buildingSum[project->shortTypeNum][0];
		int site=buildingSum[project->shortTypeNum][1];
		int sum=real+site;

		if (real>=project->amount)
		{
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_BALANCE_FINAL;
			if (!project->waitFinished)
			{
				project->blocking=false;
				project->critical=false;
			}
		}
		else if (sum<project->amount)
		{
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_FIND_PLACE;
		}
		else
		{
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_BALANCE_MAIN;
			if (!project->waitFinished)
			{
				project->blocking=false;
				project->critical=false;
			}
		}
	}
	else if (project->subPhase==AICastor::AI_CASTOR_SUBPHASE_BALANCE_MAIN)
	{
		// balance workers:
		
		int isFree=team->stats.getWorkersBalance();
		Sint32 mainWorkers=project->mainWorkers;
		Sint32 finalWorkers=project->finalWorkers;
		if (isFree<=AI_CASTOR_FREE_WORKERS_LOW)
		{
			if (mainWorkers>AI_CASTOR_FREE_WORKERS_LOW)
				mainWorkers=((AI_CASTOR_FREE_WORKERS_LOW+mainWorkers)>>1);
			//if (finalWorkers>AI_CASTOR_FREE_WORKERS_LOW)
			//	finalWorkers=AI_CASTOR_FREE_WORKERS_LOW;
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
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_BALANCE_FINAL;
		}
		else if (sum<project->amount)
		{
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_FIND_PLACE;
		}
		else if (project->multipleStart)
		{
			if (isFree>AI_CASTOR_FREE_WORKERS_SPARE)
			{
				project->subPhase=AICastor::AI_CASTOR_SUBPHASE_FIND_PLACE;
			}
			else
			{
				project->subPhase=AICastor::AI_CASTOR_SUBPHASE_WAIT_FINISHED;
			}
		}
		else
		{
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_WAIT_FINISHED;
		}
	}
	else if (project->subPhase==AICastor::AI_CASTOR_SUBPHASE_WAIT_FINISHED)
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
					b->maxUnitWorking++;
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
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_BALANCE_FINAL;
		}
		else if (sum<project->amount)
		{
			project->subPhase=AICastor::AI_CASTOR_SUBPHASE_CHECK_SITES;
		}
	}
	else if (project->subPhase==AICastor::AI_CASTOR_SUBPHASE_BALANCE_FINAL)
	{
		// balance final workers:
		
		if (project->blocking)
		{
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
					b->maxUnitWorking=finalWorkers;
					b->update();
					project->timer=timer;
					return shared_ptr<Order>(new OrderModifyBuilding(b->gid, finalWorkers));
				}
			}
		}
		if (buildingSum[project->shortTypeNum][1]==0)
		{
			project->finished=true;
		}
	}
	else
		assert(false);
	
	return shared_ptr<Order>();
}

