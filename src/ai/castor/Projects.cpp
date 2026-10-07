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

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

bool AICastor::addProject(Project *project)
{
	telemetry.count(AITrace::AI2::AICastor_addProject_calls);
	// Reject the project before adding its critical wait and workforce reservation.
	// An unavailable bootstrap project must not hold every later expansion hostage.
	if (buildingSum[project->demand][0]>=project->amount
		|| (observation->rules.hungerDisabled && project->demand==AICastor::FeedUnits)
		|| !demandAvailable(project->demand))
	{
		delete project;
		return telemetry.returnedBool(AITrace::AI2::AICastor_addProject_result,
									  AITrace::AI2::AICastor_addProject_true, false);
	}
	for (std::list<Project *>::iterator pi=projects.begin(); pi!=projects.end(); pi++)
		if (project->demand==(*pi)->demand)
		{
			if (project->amount<=(*pi)->amount)
			{
				(*pi)->timer=timer;
				delete project;
				return telemetry.returnedBool(AITrace::AI2::AICastor_addProject_result,
											  AITrace::AI2::AICastor_addProject_true, false);
			}
			else
			{
				delete (*pi);
				projects.erase(pi);
				projects.push_back(project);
				return telemetry.returnedBool(AITrace::AI2::AICastor_addProject_result,
											  AITrace::AI2::AICastor_addProject_true, true);
			}
		}
	projects.push_back(project);
	return telemetry.returnedBool(AITrace::AI2::AICastor_addProject_result,
								  AITrace::AI2::AICastor_addProject_true, true);
}

void AICastor::addProjects()
{
	telemetry.count(AITrace::AI2::AICastor_addProjects_calls);

	buildsAmount=-1;
	
	if (!observation->rules.hungerDisabled && buildingSum[AICastor::FeedUnits][0]==0)
	{
		Project *project=new Project(AICastor::FeedUnits, "boot");

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
	if (buildingSum[AICastor::ProduceWorkers][0]+buildingSum[AICastor::ProduceWorkers][1]==0)
	{
		Project *project=new Project(AICastor::ProduceWorkers, "boot");

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
	if (buildingSum[AICastor::TrainSwimming][0]+buildingSum[AICastor::TrainSwimming][1]==0)
	{
		if (timer>computeNeedSwimTimer)
		{
			computeNeedSwimTimer=timer+AI_CASTOR_NEED_SWIM_REFRESH;// every 41s
			computeNeedSwim();
		}
		if (needSwim)
		{
			Project *project=new Project(AICastor::TrainSwimming, AI_CASTOR_BOOT_SWIM_AMOUNT, AI_CASTOR_BOOT_SWIM_MAIN_WORKERS, "boot");
			project->successWait=strategy.successWait;
			project->critical=true;
			project->priority=AI_CASTOR_PROJECT_PRIORITY_CRITICAL;
			if (addProject(project))
				return;
		}
	}
	if (buildingSum[AICastor::TrainAttack][0]+buildingSum[AICastor::TrainAttack][1]==0)
	{
		Project *project=new Project(AICastor::TrainAttack, AI_CASTOR_BOOT_ATTACK_AMOUNT, AI_CASTOR_BOOT_ATTACK_MAIN_WORKERS, "boot");
		project->successWait=strategy.successWait;
		project->critical=true;
		if (addProject(project))
			return;
	}
	// all critical projects succeeded.
	
	// enough workers
	buildsAmount=0;
	if (!enoughFreeWorkers())
		return;
	
	for (int bpi=0; bpi<NB_HARD_BUILDING; bpi++)
		for (int bi=0; bi<NB_HARD_BUILDING; bi++)
			if (bpi==strategy.build[bi].baseOrder)
				if (buildingSum[bi][0]+buildingSum[bi][1]<strategy.build[bi].base)
				{
					if (bi==AICastor::ProduceWorkers
						&& (foodWarning
							|| foodLockStats[1]>foodLockStats[0]
							|| starvingWarning
							|| starvingWarningStats[1]>starvingWarningStats[0]))
						continue;
					Project *project=new Project((int)bi,
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
		if (!observation->rules.upgradesDisabled && upgradeSum<strategy.build[bi].baseUpgrade
			&& demandAvailable(bi))
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
						if (bi==AICastor::ProduceWorkers
							&& (foodWarning
								|| foodLockStats[1]>foodLockStats[0]
								|| starvingWarning
								|| starvingWarningStats[1]>starvingWarningStats[0]))
							continue;
						Project *project=new Project((int)bi,
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
			if (!observation->rules.upgradesDisabled && upgradeSum<upgradeGoal[bi]
				&& demandAvailable(bi))
				return;
		}

		buildsAmount=AI_CASTOR_BUILDS_TIER_BASE_POST+(agi<<AI_CASTOR_BUILDS_TIER_SHIFT);
	}
}

std::shared_ptr<Order>AICastor::continueProject(Project *project)
{
	telemetry.count(AITrace::AI2::AICastor_continueProject_calls);
	if (!demandAvailable(project->demand)
		|| (observation->rules.hungerDisabled && project->demand==AICastor::FeedUnits))
	{ project->finished=true; return {}; }
	telemetry.set(AITrace::AI2::project_shortTypeNum, project->demand);
	telemetry.set(AITrace::AI2::project_amount, project->amount);
	telemetry.set(AITrace::AI2::project_subPhase, project->subPhase);
	telemetry.set(AITrace::AI2::project_priority, project->priority);
	telemetry.set(AITrace::AI2::project_triesLeft, project->triesLeft);
	telemetry.set(AITrace::AI2::project_mainWorkers, project->mainWorkers);
	telemetry.set(AITrace::AI2::project_foodWorkers, project->foodWorkers);
	telemetry.set(AITrace::AI2::project_otherWorkers, project->otherWorkers);
	telemetry.set(AITrace::AI2::project_critical, project->critical);
	telemetry.set(AITrace::AI2::project_blocking, project->blocking);
	// Phase alpha will make a new Food Building at any price.
	
	if (timer<project->timer+AI_CASTOR_PROJECT_STEP_INTERVAL)
		return telemetry.returnedOrder(AITrace::AI2::AICastor_continueProject_result,
									   shared_ptr<Order>());

	if (foodLock && !project->critical && project->demand==AICastor::ProduceWorkers)
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
			return telemetry.returnedOrder(AITrace::AI2::AICastor_continueProject_result,
										   shared_ptr<Order>());
		}
		// find any good building place
		
		Sint32 typeNum=selectBuilding(project->demand);
  if (typeNum < 0) { project->finished=true; return {}; }
		int bw=(&queries->kind(typeNum).resolvedType)->width;
		int bh=(&queries->kind(typeNum).resolvedType)->height;

		
		computeCanSwim();
		computeObstacleBuildingMap();
		computeSpaceForBuildingMap(std::max(bw,bh));
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
				return telemetry.returnedOrder(AITrace::AI2::AICastor_continueProject_result, gfbm);
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

		int real=buildingSum[project->demand][0];
		int site=buildingSum[project->demand][1];
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
		
		int isFree=observedTeam->workerBalance;
		Sint32 mainWorkers=project->mainWorkers;
		Sint32 finalWorkers=project->finalWorkers;
		if (isFree<=AI_CASTOR_FREE_WORKERS_LOW)
		{
			if (mainWorkers>AI_CASTOR_FREE_WORKERS_LOW)
				mainWorkers=((AI_CASTOR_FREE_WORKERS_LOW+mainWorkers)>>1);
		}
		else
		{
			if (mainWorkers>isFree)
				mainWorkers=((isFree+mainWorkers)>>1);
		}
		
		const auto myBuildings=observation->buildingSlots(observedTeam->number);
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			const AIEngine::BuildingView *b=myBuildings[i];
			if (b)
			{
				if (provides(*b, project->demand))
				{
					if (queries->kind(*b).resolvedType.isBuildingSite)
					{
						// a main building site
						if (mainWorkers>=0 && requestedWorkers(*b)!=desiredWorkers(*b,mainWorkers))
						{
							project->timer=timer;
							return telemetry.returnedOrder(
								AITrace::AI2::AICastor_continueProject_result,
								requestWorkers(*b, desiredWorkers(*b,mainWorkers)));
						}
					}
					else
					{
						// a main building
						if (finalWorkers>=0 && requestedWorkers(*b)!=desiredWorkers(*b,finalWorkers))
						{
							project->timer=timer;
							return telemetry.returnedOrder(
								AITrace::AI2::AICastor_continueProject_result,
								requestWorkers(*b, desiredWorkers(*b,finalWorkers)));
						}
					}
				}
				else if (provides(*b, AICastor::ProduceWorkers)
					|| provides(*b, AICastor::FeedUnits))
				{
					// food buildings
					if (project->foodWorkers>=0 && requestedWorkers(*b)!=desiredWorkers(*b,project->foodWorkers))
					{
						project->timer=timer;
						return telemetry.returnedOrder(
							AITrace::AI2::AICastor_continueProject_result,
							requestWorkers(*b, desiredWorkers(*b,project->foodWorkers)));
					}
				}
				else if (queries->kind(*b).resolvedType.maxUnitWorking!=0)
				{
					// others buildings:
					if (project->otherWorkers>=0 && requestedWorkers(*b)!=desiredWorkers(*b,project->otherWorkers))
					{
						project->timer=timer;
						return telemetry.returnedOrder(
							AITrace::AI2::AICastor_continueProject_result,
							requestWorkers(*b, desiredWorkers(*b,project->otherWorkers)));
					}
				}
			}
		}
		
		int real=buildingSum[project->demand][0];
		int site=buildingSum[project->demand][1];
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
			const auto myBuildings=observation->buildingSlots(observedTeam->number);
			for (int i=0; i<Building::MAX_COUNT; i++)
			{
				const AIEngine::BuildingView *b=myBuildings[i];
				if (b && provides(*b, project->demand) && requestedWorkers(*b)<desiredWorkers(*b,project->mainWorkers))
				{
					const int workers=requestedWorkers(*b)+1;
					project->timer=timer;
					return telemetry.returnedOrder(
						AITrace::AI2::AICastor_continueProject_result,
						requestWorkers(*b, workers));
				}
			}
		}
		
		int real=buildingSum[project->demand][0];
		int site=buildingSum[project->demand][1];
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
			
			const auto myBuildings=observation->buildingSlots(observedTeam->number);
			for (int i=0; i<Building::MAX_COUNT; i++)
			{
				const AIEngine::BuildingView *b=myBuildings[i];
				if (b && provides(*b, project->demand) && requestedWorkers(*b)!=desiredWorkers(*b,finalWorkers))
				{

					project->timer=timer;
					return telemetry.returnedOrder(
						AITrace::AI2::AICastor_continueProject_result,
						requestWorkers(*b, desiredWorkers(*b,finalWorkers)));
				}
			}
		}
		if (buildingSum[project->demand][1]==0)
		{
			project->finished=true;
		}
	}
	else
		assert(false);

	return telemetry.returnedOrder(AITrace::AI2::AICastor_continueProject_result,
								   shared_ptr<Order>());
}

