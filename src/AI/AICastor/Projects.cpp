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

using std::shared_ptr;

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

std::shared_ptr<Order>AICastor::continueProject(Project *project)
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
		
		std::shared_ptr<Order>gfbm=findGoodBuilding(typeNum, project->food, project->defense, project->critical);
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

