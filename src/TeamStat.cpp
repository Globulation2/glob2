/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "TeamStat.h"
#include "GlobalContainer.h"


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
	TeamSmoothedStat &smoothedStat=smoothedStats[smoothedIndex];
	memset(&smoothedStat, 0, sizeof(TeamSmoothedStat));
	for (int i=0; i<1024; i++)
	{
		Unit *u=team->myUnits[i];
		if ((u)&&(u->medical==Unit::MED_FREE)&&(u->activity==Unit::ACT_RANDOM))
		{
			smoothedStat.isFree[(int)u->typeNum]++;
			smoothedStat.totalFree++;
		}
	}
	smoothedIndex++;
	smoothedIndex%=STATS_SMOOTH_SIZE;
	if (smoothedIndex)
		return;
	
	TeamSmoothedStat maxStat;
	memset(&maxStat, 0, sizeof(TeamSmoothedStat));
	
	for (int i=0; i<STATS_SMOOTH_SIZE; i++)
	{
		TeamSmoothedStat &smoothedStat=smoothedStats[i];
		
		if (smoothedStat.totalFree>maxStat.totalFree)
			maxStat.totalFree=smoothedStat.totalFree;
		for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
			if (smoothedStat.isFree[j]>maxStat.isFree[j])
				maxStat.isFree[j]=smoothedStat.isFree[j];
	}
	
	// We change current stats:
	statsIndex++;
	statsIndex%=STATS_SIZE;
	TeamStat &stat=stats[statsIndex];
	memset(&stat, 0, sizeof(TeamStat));
	
	for (int i=0; i<1024; i++)
	{
		Unit *u=team->myUnits[i];
		if (u)
		{
			stat.totalUnit++;
			stat.numberPerType[(int)u->typeNum]++;

			if (u->medical==Unit::MED_HUNGRY)
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
	
	// We override unsmoothed stats:
	stat.totalFree=maxStat.totalFree;
	for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
		stat.isFree[j]=maxStat.isFree[j];
}

void TeamStats::drawText()
{
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFontGreen;
	StringTable *strings=&(globalContainer->texts);
	int textStartPos=gfx->getW()-124;
	
	TeamStat &newStats=stats[statsIndex];
	
	// general
	gfx->drawString(textStartPos, 132, font, strings->getString("[Statistics]"));
	gfx->drawString(textStartPos, 132+17, font, "%d %s", newStats.totalUnit, strings->getString("[Units]"));
	if (newStats.totalUnit)
	{
		// worker
		gfx->drawString(textStartPos, 132+34, font, "%d %s (%.0f %%)", newStats.numberPerType[0], strings->getString("[worker]"), ((float)newStats.numberPerType[0])*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos+5, 132+46, font, "%s %d %s", strings->getString("[of which]"), newStats.isFree[0], strings->getString("[free]"));
		// explorer
		gfx->drawString(textStartPos, 132+63, font, "%d %s (%.0f %%)", newStats.numberPerType[1], strings->getString("[Explorer]"), ((float)newStats.numberPerType[1])*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos+5, 132+75, font, "%s %d %s", strings->getString("[of which]"), newStats.isFree[1], strings->getString("[free]"));
		// warrior
		gfx->drawString(textStartPos, 132+92, font, "%d %s (%.0f %%)", newStats.numberPerType[2], strings->getString("[Warrior]"), ((float)newStats.numberPerType[2])*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos+5, 132+104, font, "%s %d %s", strings->getString("[of which]"), newStats.isFree[2], strings->getString("[free]"));

		// living state
		gfx->drawString(textStartPos, 132+121, font, "%d %s (%.0f %%)", newStats.needNothing, strings->getString("[are ok]"), ((float)newStats.needNothing)*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos, 132+133, font, "%d %s (%.0f %%)", newStats.needFood, strings->getString("[are hungry]"), ((float)newStats.needFood)*100.0f/((float)newStats.totalUnit));
		gfx->drawString(textStartPos, 132+145, font, "%d %s (%.0f %%)", newStats.needHeal, strings->getString("[are wonded]"), ((float)newStats.needHeal)*100.0f/((float)newStats.totalUnit));

		// upgrade state
		gfx->drawString(globalContainer->gfx->getW()-124, 132+162, globalContainer->littleFontGreen, "%s %d/%d/%d/%d", strings->getString("[Walk]"), newStats.upgradeState[WALK][0], newStats.upgradeState[WALK][1], newStats.upgradeState[WALK][2], newStats.upgradeState[WALK][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+174, globalContainer->littleFontGreen, "%s %d/%d/%d/%d", strings->getString("[Swim]"), newStats.upgradeState[SWIM][0], newStats.upgradeState[SWIM][1], newStats.upgradeState[SWIM][2], newStats.upgradeState[SWIM][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+186, globalContainer->littleFontGreen, "%s %d/%d/%d/%d", strings->getString("[Build]"), newStats.upgradeState[BUILD][0], newStats.upgradeState[BUILD][1], newStats.upgradeState[BUILD][2], newStats.upgradeState[BUILD][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+198, globalContainer->littleFontGreen, "%s %d/%d/%d/%d", strings->getString("[Harvest]"), newStats.upgradeState[HARVEST][0], newStats.upgradeState[HARVEST][1], newStats.upgradeState[HARVEST][2], newStats.upgradeState[HARVEST][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+210, globalContainer->littleFontGreen, "%s %d/%d/%d/%d", strings->getString("[At. speed]"), newStats.upgradeState[ATTACK_SPEED][0], newStats.upgradeState[ATTACK_SPEED][1], newStats.upgradeState[ATTACK_SPEED][2], newStats.upgradeState[ATTACK_SPEED][3]);
		gfx->drawString(globalContainer->gfx->getW()-124, 132+222, globalContainer->littleFontGreen, "%s %d/%d/%d/%d", strings->getString("[At. strength]"), newStats.upgradeState[ATTACK_STRENGTH][0], newStats.upgradeState[ATTACK_STRENGTH][1], newStats.upgradeState[ATTACK_STRENGTH][2], newStats.upgradeState[ATTACK_STRENGTH][3]);
	}	
}

void TeamStats::drawStat()
{
	assert(STATS_SIZE==128);// We have graphical constraints
	
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFontGreen;
	StringTable *strings=&(globalContainer->texts);
	int textStartPos=gfx->getW()-124;
	
	// compute total unites
	int maxUnit=0;
	int i;
	for (i=0; i<STATS_SIZE; i++)
	{
		if (stats[i].totalUnit>maxUnit)
			maxUnit=stats[i].totalUnit;
	}

	// captions
	gfx->drawString(textStartPos, 132, font, strings->getString("[Statistics]"));
	gfx->drawString(textStartPos, 132+16, font, strings->getString("[Free/total]"));
	gfx->drawString(textStartPos, 132+100, font, strings->getString("[Ok/hungry/wounded]"));

	// graph
	for (i=0; i<128; i++)
	{
		//int index=(statsPtr+i+1)&0x7F;
		int index=(statsIndex+i+1)&0x7F;
		int nbFree=(stats[index].totalFree*64)/maxUnit;
		int nbTotal=(stats[index].totalUnit*64)/maxUnit;
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbTotal, nbTotal-nbFree, 0, 0, 255);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 36 +64-nbFree, nbFree, 0, 255, 0);
		int nbOk, nbNeedFood, nbNeedHeal;
		if (stats[index].totalUnit)
		{
			nbOk=(stats[index].needNothing*64)/stats[index].totalUnit;
			nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
			nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
		}
		else
		{
			nbOk=nbNeedFood=nbNeedHeal=0;
		}
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFood-nbOk, nbOk, 0, 220, 0);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal-nbNeedFood, nbNeedFood, 224, 210, 17);
		globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-128+i, 128+ 120 +64-nbNeedHeal, nbNeedHeal, 255, 0, 0);
	}
}

int TeamStats::getFreeUnits()
{
	return (stats[statsIndex].isFree[UnitType::WORKER]);
}
