/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "TeamStat.h"
#include "GlobalContainer.h"
#include "Team.h"

TeamStats::TeamStats()
{
	statsIndex=0;
	smoothedIndex=0;
	
	memset(stats, 0, sizeof(TeamStat)*STATS_SIZE);
	memset(smoothedStats, 0, sizeof(TeamSmoothedStat)*STATS_SMOOTH_SIZE);
}

TeamStats::~TeamStats()
{
	
}

void TeamStats::step(Team *team)
{
	int i;
	TeamSmoothedStat &smoothedStat=smoothedStats[smoothedIndex];
	memset(&smoothedStat, 0, sizeof(TeamSmoothedStat));
	for (i=0; i<1024; i++)
	{
		Unit *u=team->myUnits[i];
		if ((u)&&(u->medical==Unit::MED_FREE)&&(u->activity==Unit::ACT_RANDOM))
		{
			smoothedStat.isFree[(int)u->typeNum]++;
			smoothedStat.totalFree++;
		}
	}
	
	std::list<Building *> upgradesList=team->upgrade[HARVEST];
	for (std::list<Building *>::iterator bi=upgradesList.begin(); bi!=upgradesList.end(); bi++)
		smoothedStat.totalNeeded+=(*bi)->maxUnitWorking-(*bi)->unitsWorking.size();
	
	std::list<Building *> jobsList=team->job[HARVEST];
	for (std::list<Building *>::iterator bi=jobsList.begin(); bi!=jobsList.end(); bi++)
		smoothedStat.totalNeeded+=(*bi)->maxUnitWorking-(*bi)->unitsWorking.size();
	
	smoothedIndex++;
	smoothedIndex%=STATS_SMOOTH_SIZE;
	if (smoothedIndex)
		return;
	
	TeamSmoothedStat maxStat;
	memset(&maxStat, 0, sizeof(TeamSmoothedStat));
	for (i=0; i<STATS_SMOOTH_SIZE; i++)
	{
		TeamSmoothedStat &smoothedStat=smoothedStats[i];

		if (smoothedStat.totalFree>maxStat.totalFree)
			maxStat.totalFree=smoothedStat.totalFree;
		for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
			if (smoothedStat.isFree[j]>maxStat.isFree[j])
				maxStat.isFree[j]=smoothedStat.isFree[j];
		if (smoothedStat.totalNeeded>maxStat.totalNeeded)
			maxStat.totalNeeded=smoothedStat.totalNeeded;
	}

	// We change current stats:
	statsIndex++;
	statsIndex%=STATS_SIZE;
	TeamStat &stat=stats[statsIndex];
	memset(&stat, 0, sizeof(TeamStat));
	for (i=0; i<1024; i++)
	{
		Unit *u=team->myUnits[i];
		if (u)
		{
			stat.totalUnit++;
			stat.numberUnitPerType[(int)u->typeNum]++;

			if (u->isUnitHungry())
				if (u->hp<u->performance[HP])
					stat.needFoodCritical++;
				else
					stat.needFood++;
			else if (u->medical==Unit::MED_DAMAGED)
				stat.needHeal++;
			else
			{
				stat.needNothing++;
				if (u->activity==Unit::ACT_RANDOM)
				{
					stat.isFree[(int)u->typeNum]++;
					stat.totalFree++;
				}
			}
			for (int j=0; j<NB_ABILITY; j++)
			{
				if (u->performance[j])
					stat.upgradeState[j][u->level[j]]++;
			}
		}
	}

	for (i=0; i<512; i++)
	{
		if (team->myBuildings[i])
		{
			stat.totalBuilding++;
			stat.numberBuildingPerType[team->myBuildings[i]->type->type]++;
			int tabLevel=((team->myBuildings[i]->type->level)<<1)+1-team->myBuildings[i]->type->isBuildingSite;
			assert(tabLevel>=0);
			assert(tabLevel<=5);
			stat.numberBuildingPerTypePerLevel[team->myBuildings[i]->type->type][tabLevel]++;
		}
	}
	
	// We override unsmoothed stats:
	stat.totalFree=maxStat.totalFree;
	for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
		stat.isFree[j]=maxStat.isFree[j];
	stat.totalNeeded=maxStat.totalNeeded;
}

void TeamStats::drawText()
{
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFont;
	StringTable *strings=&(globalContainer->texts);
	int textStartPos=gfx->getW()-124;
	
	TeamStat &newStats=stats[statsIndex];
	
	// general
	gfx->drawString(textStartPos, 132, font, strings->getString("[Statistics]"));
	gfx->drawString(textStartPos, 132+17, font, "%d %s", newStats.totalUnit, strings->getString("[Units]"));
	if (newStats.totalUnit)
	{
		// worker
		int free=newStats.isFree[0]-newStats.totalNeeded;
		int seeking=newStats.totalNeeded;
		if (free<0)
		{
			free=0;
			seeking=newStats.isFree[0];
		}
		gfx->drawString(textStartPos, 132+34, font, "%d %s (%.0f %%)", newStats.numberUnitPerType[0], strings->getString("[worker]"), ((float)newStats.numberUnitPerType[0])*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos+5, 132+46, font, "%s %d %s", strings->getString("[of which]"), free, strings->getString("[free]"));
		gfx->drawString(textStartPos+5, 132+56, font, "%s %d %s", strings->getString("[and]"), seeking, strings->getString("[seeking a job]"));
		
		// explorer
		gfx->drawString(textStartPos, 132+73, font, "%d %s (%.0f %%)", newStats.numberUnitPerType[1], strings->getString("[Explorer]"), ((float)newStats.numberUnitPerType[1])*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos+5, 132+85, font, "%s %d %s", strings->getString("[of which]"), newStats.isFree[1], strings->getString("[free]"));
		// warrior
		gfx->drawString(textStartPos, 132+102, font, "%d %s (%.0f %%)", newStats.numberUnitPerType[2], strings->getString("[Warrior]"), ((float)newStats.numberUnitPerType[2])*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos+5, 132+114, font, "%s %d %s", strings->getString("[of which]"), newStats.isFree[2], strings->getString("[free]"));

		// living state 
		gfx->drawString(textStartPos, 132+131, font, "%d %s (%.0f %%)", newStats.needNothing, strings->getString("[are ok]"), ((float)newStats.needNothing)*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos, 132+143, font, "%d %s (%.0f %%)", newStats.needFood, strings->getString("[are hungry]"), ((float)newStats.needFood)*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos, 132+155, font, "%d %s (%.0f %%)", newStats.needFoodCritical, strings->getString("[are dying hungry]"), ((float)newStats.needFoodCritical)*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos, 132+167, font, "%d %s (%.0f %%)", newStats.needHeal, strings->getString("[are wonded]"), ((float)newStats.needHeal)*100.0f/((float)newStats.totalUnit));

		// upgrade state
		gfx->drawString(globalContainer->gfx->getW()-124, 132+184, globalContainer->littleFont, "%s %d/%d/%d/%d", strings->getString("[Walk]"), newStats.upgradeState[WALK][0], newStats.upgradeState[WALK][1], newStats.upgradeState[WALK][2], newStats.upgradeState[WALK][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+196, globalContainer->littleFont, "%s %d/%d/%d/%d", strings->getString("[Swim]"), newStats.upgradeState[SWIM][0], newStats.upgradeState[SWIM][1], newStats.upgradeState[SWIM][2], newStats.upgradeState[SWIM][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+208, globalContainer->littleFont, "%s %d/%d/%d/%d", strings->getString("[Build]"), newStats.upgradeState[BUILD][0], newStats.upgradeState[BUILD][1], newStats.upgradeState[BUILD][2], newStats.upgradeState[BUILD][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+220, globalContainer->littleFont, "%s %d/%d/%d/%d", strings->getString("[Harvest]"), newStats.upgradeState[HARVEST][0], newStats.upgradeState[HARVEST][1], newStats.upgradeState[HARVEST][2], newStats.upgradeState[HARVEST][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+232, globalContainer->littleFont, "%s %d/%d/%d/%d", strings->getString("[At. speed]"), newStats.upgradeState[ATTACK_SPEED][0], newStats.upgradeState[ATTACK_SPEED][1], newStats.upgradeState[ATTACK_SPEED][2], newStats.upgradeState[ATTACK_SPEED][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+244, globalContainer->littleFont, "%s %d/%d/%d/%d", strings->getString("[At. strength]"), newStats.upgradeState[ATTACK_STRENGTH][0], newStats.upgradeState[ATTACK_STRENGTH][1], newStats.upgradeState[ATTACK_STRENGTH][2], newStats.upgradeState[ATTACK_STRENGTH][3]);
	}	
}

void TeamStats::drawStat()
{
	assert(STATS_SIZE==128);// We have graphical constraints
	
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFont;
	StringTable *strings=&(globalContainer->texts);
	int textStartPos=gfx->getW()-124;
	
	// compute total unites
	/*int maxUnit=0;
	int i;
	for (i=0; i<STATS_SIZE; i++)
	{
		if (stats[i].totalUnit>maxUnit)
			maxUnit=stats[i].totalUnit;
	}*/
	int maxWorker=0;
	for (int i=0; i<STATS_SIZE; i++)
		if (stats[i].numberUnitPerType[0]>maxWorker)
			maxWorker=stats[i].numberUnitPerType[0];

	// captions
	gfx->drawString(textStartPos, 132, font, strings->getString("[Statistics]"));
	gfx->drawString(textStartPos, 132+16, font, strings->getString("[Total/free/seeking]"));
	gfx->drawString(textStartPos, 132+100, font, strings->getString("[Ok/hungry/wounded]"));

	// graph
	for (int i=0; i<128; i++)
	{
		int index=(statsIndex+i+1)&0x7F;
		
		int free=stats[index].isFree[UnitType::WORKER]-stats[index].totalNeeded;
		int seeking=stats[index].totalNeeded;
		if (free<0)
		{
			free=0;
			seeking=stats[index].isFree[UnitType::WORKER];
		}
		
		int nbFree=(free*64)/maxWorker;
		int nbSeeking=(seeking*64)/maxWorker;
		int nbTotal=(stats[index].numberUnitPerType[UnitType::WORKER]*64)/maxWorker;
		
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbTotal, nbTotal-nbFree-nbSeeking, 34, 66, 163);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbFree-nbSeeking, nbFree, 22, 229, 40);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbSeeking, nbSeeking, 150, 50, 50);
		/*int nbFree=((stats[index].totalFree)*64)/maxUnit;
		int nbNeeded=((stats[index].totalNeeded)*64)/maxUnit;
		int nbTotal=(stats[index].totalUnit*64)/maxUnit;
		int realyFree=nbFree-nbNeeded;
		if (realyFree>0)
		{
			globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbTotal, nbTotal-nbFree, 0, 0, 250);
			globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbFree, realyFree, 0, 250, 0);

			globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbNeeded, nbNeeded, 250, 0, 0);
		}
		else
			globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbTotal, nbTotal, 0, 0, 250);*/
		
		int nbOk, nbNeedFood, nbNeedFoodCritical, nbNeedHeal;
		if (stats[index].totalUnit)
		{
			// to avoid some roundoff errors
			if (stats[index].needNothing>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
				nbNeedFoodCritical=(stats[index].needFoodCritical*64)/stats[index].totalUnit;
				nbOk=64-(nbNeedHeal + nbNeedFood+nbNeedFoodCritical);
			}
			else if (stats[index].needFood>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFoodCritical=(stats[index].needFoodCritical*64)/stats[index].totalUnit;
				nbNeedFood=64-(nbNeedHeal-nbNeedFoodCritical);
				nbOk=0;
			}
			else if (stats[index].needFoodCritical>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFoodCritical=64-nbNeedHeal;
				nbOk=0;
			}
			else
			{
				nbOk=nbNeedFood=nbNeedFoodCritical=0;
				nbNeedHeal=64;
			}
		}
		else
		{
			nbOk=nbNeedFood=nbNeedHeal=nbNeedFoodCritical=0;
		}
		//globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFood-nbOk, nbOk, 0, 220, 0);
		//globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFood, nbNeedFood, 224, 210, 17);
		//globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal, nbNeedHeal, 255, 0, 0);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood-nbOk, nbOk, 22, 229, 40);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood, nbNeedFood, 224, 210, 17);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFoodCritical, nbNeedFoodCritical, 249, 167, 14);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal, nbNeedHeal, 250, 25, 25);
	}
}

int TeamStats::getFreeUnits()
{
	return (stats[statsIndex].isFree[UnitType::WORKER]);
}

int TeamStats::getUnitsNeeded()
{
	return (stats[statsIndex].totalNeeded);
}
