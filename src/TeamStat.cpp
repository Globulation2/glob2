// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <sstream>
#include <iostream>
#include <cstdlib>

#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>

#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "TeamStat.h"
#include "Unit.h"
#include "Bullet.h"


EndOfGameStat::EndOfGameStat(int units, int buildings, int prestige, int hp, int attack, int defense)
{
	value[TYPE_UNITS]=units;
	value[TYPE_BUILDINGS]=buildings;
	value[TYPE_PRESTIGE]=prestige;
	value[TYPE_HP]=hp;
	value[TYPE_ATTACK]=attack;
	value[TYPE_DEFENSE]=defense;
}

TeamStat::TeamStat()
{
	reset();
}


void TeamStat::reset()
{
	totalUnit=0;
	for(int i=0; i<NB_UNIT_TYPE; ++i)
	{
		numberUnitPerType[i]=0;
		isFree[i]=0;
	}
	totalFree=0;
	totalNeeded=0;
	for(int i=0; i<NB_UNIT_LEVELS; ++i)
		totalNeededPerLevel[i]=0;
	totalBuilding=0;
	for(int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		numberBuildingPerType[i]=0;
		for(int j=0; j<NB_BUILDING_LONG_LEVELS; ++j)
			numberBuildingPerTypePerLevel[i][j]=0;
	}
	needFoodCritical=0;
	needFood=0;
	needFoodNoInns=0;
	needHeal=0;
	needNothing=0;
	for(int i=0; i<NB_ABILITY; ++i)
		for(int j=0; j<NB_UNIT_LEVELS; ++j)
		{
			upgradeState[i][j]=0;
		}
	for(int k=0; k<NB_UNIT_TYPE; ++k)
	{
		for(int i=0; i<NB_ABILITY; ++i)
			for(int j=0; j<NB_UNIT_LEVELS; ++j)
				upgradeStatePerType[k][i][j]=0;
	}
	totalFood=0;
	totalFoodCapacity=0;
	totalUnitFoodable=0;
	totalUnitFooded=0;

	totalHP=0;
	totalAttackPower=0;
	totalDefensePower=0;

	for(int i=0; i<HAPPYNESS_COUNT+1; ++i)
		happiness[i]=0;
}



TeamSmoothedStat::TeamSmoothedStat()
{
	reset();
}


void TeamSmoothedStat::reset()
{
	totalFree=0;
	for(int i=0; i<NB_UNIT_TYPE; ++i)
	{
		isFree[i]=0;
	}
	totalNeeded=0;
	for(int i=0; i<NB_UNIT_LEVELS; ++i)
		totalNeededPerLevel[i]=0;
}



TeamStats::TeamStats()
{
	statsIndex=0;
	smoothedIndex=0;
	haveSetMapSize=false;
}

TeamStats::~TeamStats()
{
	
}

void TeamStats::step(Team *team, bool reloaded)
{
	// handle end of game stat step
	if (((team->game->stepCounter & END_OF_GAME_STAT_INTERVAL_MASK) == 0) && !reloaded)
	{
		endOfGameStats.push_back(EndOfGameStat(stats[statsIndex].totalUnit, stats[statsIndex].totalBuilding, team->prestige,
			stats[statsIndex].totalHP, stats[statsIndex].totalAttackPower, stats[statsIndex].totalDefensePower));

		// Optional rich per-team economy/food trace for AI debugging. Gated by
		// GLOB2_TEAM_TIMELINE (same flag as Engine::printTeamTimeline). Emitted
		// here so it captures the live food/worker state at each 512-tick sample
		// — data that the archived EndOfGameStat (6 scalars) cannot carry.
		if (getenv("GLOB2_TEAM_TIMELINE"))
		{
			const TeamStat &s = stats[statsIndex];
			std::cout << "GLOB2_ECON team=" << team->teamNumber
				<< " tick=" << team->game->stepCounter
				<< " workers=" << s.numberUnitPerType[WORKER]
				<< " warriors=" << s.numberUnitPerType[WARRIOR]
				<< " explorers=" << s.numberUnitPerType[EXPLORER]
				<< " food=" << s.totalFood << "/" << s.totalFoodCapacity
				<< " fooded=" << s.totalUnitFooded << "/" << s.totalUnitFoodable
				<< " foodCritical=" << s.needFoodCritical
				<< " needFood=" << s.needFood
				<< " swarm=" << s.numberBuildingPerType[IntBuildingType::SWARM_BUILDING]
				<< " inn=" << s.numberBuildingPerType[IntBuildingType::FOOD_BUILDING]
				<< " school=" << s.numberBuildingPerType[IntBuildingType::SCIENCE_BUILDING]
				<< " barracks=" << s.numberBuildingPerType[IntBuildingType::ATTACK_BUILDING]
				<< " tower=" << s.numberBuildingPerType[IntBuildingType::DEFENSE_BUILDING]
				<< std::endl;
		}
	}
	
	// handle in game stat step
	TeamSmoothedStat &smoothedStat=smoothedStats[smoothedIndex];
	smoothedStat.reset();
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=team->myUnits[i];
		if ((u)&&(u->medical==Unit::MED_FREE)&&(u->activity==Unit::ACT_RANDOM))
		{
			smoothedStat.isFree[(int)u->typeNum]++;
			smoothedStat.totalFree++;
		}
	}
	
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b = team->myBuildings[i];
		if (b)
		{
            if(b->type->foodable || b->type->fillable || b->type->zonable[WORKER])
            {
		        smoothedStat.totalNeeded+=b->desiredMaxUnitWorking-(int)b->unitsWorking.size();
		        smoothedStat.totalNeededPerLevel[b->type->level]+=b->desiredMaxUnitWorking-(int)b->unitsWorking.size();
            }
        }
    }


	smoothedIndex++;
	smoothedIndex%=STATS_SMOOTH_SIZE;
	if (smoothedIndex)
		return;
	
	TeamSmoothedStat maxStat;
	for (int i=0; i<STATS_SMOOTH_SIZE; i++)
	{
		TeamSmoothedStat &smoothedStat=smoothedStats[i];

		if (smoothedStat.totalFree>maxStat.totalFree)
			maxStat.totalFree=smoothedStat.totalFree;
		for (int j=0; j<NB_UNIT_TYPE; j++)
			if (smoothedStat.isFree[j]>maxStat.isFree[j])
				maxStat.isFree[j]=smoothedStat.isFree[j];
		if (smoothedStat.totalNeeded>maxStat.totalNeeded)
		{
			maxStat.totalNeeded=smoothedStat.totalNeeded;
		}
		for(int k=0; k<NB_UNIT_LEVELS; ++k)
		{
			if (smoothedStat.totalNeededPerLevel[k]>maxStat.totalNeededPerLevel[k])
				maxStat.totalNeededPerLevel[k]=smoothedStat.totalNeededPerLevel[k];
		}
	}

	// We change current stats:
	statsIndex++;
	statsIndex%=STATS_SIZE;
	TeamStat &stat=stats[statsIndex];

	stat.reset();

	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=team->myUnits[i];
		if (u)
		{
			stat.totalUnit++;
			stat.numberUnitPerType[(int)u->typeNum]++;
			stat.totalHP+=u->hp;

			if (u->isUnitHungry())
			{
				if (u->attachedBuilding && u->insideTimeout<0 && u->attachedBuilding->type->canFeedUnit)
					stat.needNothing++;
				else if (u->hp<u->performance[HP])
				{
					stat.needFoodCritical++;
				}
				else
					stat.needFood++;
					
				if(u->activity != Unit::ACT_UPGRADING)
				{
					stat.needFoodNoInns++;
				}
			}
			else if (u->medical==Unit::MED_DAMAGED)
			{
				if (u->attachedBuilding && u->insideTimeout<0 && u->attachedBuilding->type->canHealUnit)
					stat.needNothing++;
				else
				{
					stat.needHeal++;
				}
			}
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
				{
					stat.upgradeState[j][u->level[j]]++;
					stat.upgradeStatePerType[(int)u->typeNum][j][u->level[j]]++;
				}
			}
			if (u->typeNum==WARRIOR)
				stat.totalAttackPower+=u->performance[ATTACK_SPEED]*u->getRealAttackStrength();
			
			stat.happiness[u->fruitCount]++;
		}
	}

	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b = team->myBuildings[i];
		if (b)
		{
			stat.numberBuildingPerType[b->type->shortTypeNum]++;
			int longLevel=b->getLongLevel();
			assert(longLevel>=0);
			assert(longLevel<=MAX_BUILDING_LONG_LEVEL);
			stat.numberBuildingPerTypePerLevel[b->type->shortTypeNum][longLevel]++;
			stat.totalHP += b->hp;
			stat.totalDefensePower += (b->type->shootDamage*b->type->shootRythme) >> SHOOTING_COOLDOWN_MAGNITUDE;
			if ((!b->type->isBuildingSite) && (!b->type->isVirtual))
				stat.totalBuilding++;
		}
	}
	
	// We override unsmoothed stats:
	stat.totalFree=maxStat.totalFree;
	for (int j=0; j<NB_UNIT_TYPE; j++)
		stat.isFree[j]=maxStat.isFree[j];
	stat.totalNeeded=maxStat.totalNeeded;
	for(int k=0; k<NB_UNIT_LEVELS; ++k)
		stat.totalNeededPerLevel[k]=maxStat.totalNeededPerLevel[k];
}

void TeamStats::drawText(int posx, int posy)
{
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFont;
	StringTable *strings=Toolkit::getStringTable();
	int textStartPosX=posx+4;
	int textStartPosY=posy;
	
	TeamStat &newStats=stats[statsIndex];
	
	// general
	//gfx->drawString(textStartPosX, textStartPosY, font, strings->getString("[Statistics]"));
	textStartPosY -= 5; // this is to correct for the removal of the title
	gfx->drawString(textStartPosX, textStartPosY+15, font, FormatableString("%0 %1").arg(newStats.totalUnit).arg(strings->getString("[Units]")).c_str());
	if (newStats.totalUnit)
	{
		// worker
		int free=newStats.isFree[WORKER]-newStats.totalNeeded;
		int seeking=newStats.totalNeeded;
		if (free<0)
		{
			free=0;
			seeking=newStats.isFree[WORKER];
		}
		gfx->drawString(textStartPosX, textStartPosY+30, font, FormatableString("%0 %1 (%2 %)").arg(newStats.numberUnitPerType[WORKER]).arg(strings->getString("[workers]")).arg(((float)newStats.numberUnitPerType[WORKER])*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+42, font, FormatableString("%0 %1 %2").arg(strings->getString("[of which]")).arg(free).arg(strings->getString("[free]")).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+54, font, FormatableString("%0 %1 %2").arg(strings->getString("[and]")).arg(seeking).arg(strings->getString("[seeking a job]")).c_str());

		// explorer
		gfx->drawString(textStartPosX, textStartPosY+69, font, FormatableString("%0 %1 (%2 %)").arg(newStats.numberUnitPerType[EXPLORER]).arg(strings->getString("[explorers]")).arg(((float)newStats.numberUnitPerType[EXPLORER])*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+81, font, FormatableString("%0 %1 %2").arg(strings->getString("[of which]")).arg(newStats.isFree[EXPLORER]).arg(strings->getString("[free]")).c_str());
		// warrior
		gfx->drawString(textStartPosX, textStartPosY+96, font, FormatableString("%0 %1 (%2 %)").arg(newStats.numberUnitPerType[WARRIOR]).arg(strings->getString("[warriors]")).arg(((float)newStats.numberUnitPerType[WARRIOR])*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+108, font, FormatableString("%0 %1 %2").arg(strings->getString("[of which]")).arg(newStats.isFree[WARRIOR]).arg(strings->getString("[free]")).c_str());

		// living state
		gfx->drawString(textStartPosX, textStartPosY+123, font, FormatableString("%0 %1 (%2 %)").arg(newStats.needNothing).arg(strings->getString("[are ok]")).arg(((float)newStats.needNothing)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX, textStartPosY+135, font, FormatableString("%0 %1 (%2 %)").arg(newStats.needFood).arg(strings->getString("[are hungry]")).arg(((float)newStats.needFood)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX, textStartPosY+147, font, FormatableString("%0 %1 (%2 %)").arg(newStats.needFoodCritical).arg(strings->getString("[are dying hungry]")).arg(((float)newStats.needFoodCritical)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX, textStartPosY+159, font, FormatableString("%0 %1 (%2 %)").arg(newStats.needHeal).arg(strings->getString("[are wonded]")).arg(((float)newStats.needHeal)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());

		// upgrade state
		gfx->drawString(textStartPosX,textStartPosY+174, globalContainer->littleFont, FormatableString("%0 %1/%2/%3/%4").arg(strings->getString("[Walk]")).arg(newStats.upgradeState[WALK][0]).arg(newStats.upgradeState[WALK][1]).arg(newStats.upgradeState[WALK][2]).arg(newStats.upgradeState[WALK][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+186, globalContainer->littleFont, FormatableString("%0 %1/%2/%3").arg(strings->getString("[Swim]")).arg(newStats.upgradeState[SWIM][1]).arg(newStats.upgradeState[SWIM][2]).arg(newStats.upgradeState[SWIM][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+198, globalContainer->littleFont, FormatableString("%0 %1/%2/%3/%4").arg(strings->getString("[Build]")).arg(newStats.upgradeState[BUILD][0]).arg(newStats.upgradeState[BUILD][1]).arg(newStats.upgradeState[BUILD][2]).arg(newStats.upgradeState[BUILD][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+210, globalContainer->littleFont, FormatableString("%0 %1/%2/%3/%4").arg(strings->getString("[Harvest]")).arg(newStats.upgradeState[HARVEST][0]).arg(newStats.upgradeState[HARVEST][1]).arg(newStats.upgradeState[HARVEST][2]).arg(newStats.upgradeState[HARVEST][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+222, globalContainer->littleFont, FormatableString("%0 %1/%2/%3/%4").arg(strings->getString("[At. speed]")).arg(newStats.upgradeState[ATTACK_SPEED][0]).arg(newStats.upgradeState[ATTACK_SPEED][1]).arg(newStats.upgradeState[ATTACK_SPEED][2]).arg(newStats.upgradeState[ATTACK_SPEED][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+234, globalContainer->littleFont, FormatableString("%0 %1/%2/%3/%4").arg(strings->getString("[At. strength]")).arg(newStats.upgradeState[ATTACK_STRENGTH][0]).arg(newStats.upgradeState[ATTACK_STRENGTH][1]).arg(newStats.upgradeState[ATTACK_STRENGTH][2]).arg(newStats.upgradeState[ATTACK_STRENGTH][3]).c_str());
	
		// jobs
		gfx->drawString(textStartPosX, textStartPosY+249, globalContainer->littleFont, FormatableString("%0 1 %1: %2").arg(strings->getString("[level]")).arg(strings->getString("[jobs]")).arg(newStats.totalNeededPerLevel[0]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+261, globalContainer->littleFont, FormatableString("%0 2 %1: %2").arg(strings->getString("[level]")).arg(strings->getString("[jobs]")).arg(newStats.totalNeededPerLevel[1]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+273, globalContainer->littleFont, FormatableString("%0 3 %1: %2").arg(strings->getString("[level]")).arg(strings->getString("[jobs]")).arg(newStats.totalNeededPerLevel[2]).c_str());
	
		// happyness
		std::stringstream happyness;
		happyness << strings->getString("[Happyness]") << " " << newStats.happiness[0];
		for (int i=1; i<=HAPPYNESS_COUNT; i++)
			happyness << '/' << newStats.happiness[i];
		gfx->drawString(textStartPosX, textStartPosY+288, globalContainer->littleFont, happyness.str().c_str());
	}
}

void TeamStats::drawStat(int posx, int posy)
{
	assert(STATS_SIZE==128);// We have graphical constraints
	
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFont;
	StringTable *strings=Toolkit::getStringTable();
	int textStartPos=posx+4;
	int startPoxY=posy;
	
	// compute total units
	/*int maxUnit=0;
	int i;
	for (i=0; i<STATS_SIZE; i++)
	{
		if (stats[i].totalUnit>maxUnit)
			maxUnit=stats[i].totalUnit;
	}*/
	int maxWorker=0;
	for (int i=0; i<STATS_SIZE; i++)
		if (stats[i].numberUnitPerType[WORKER]>maxWorker)
			maxWorker=stats[i].numberUnitPerType[WORKER];

	if (maxWorker==0)
		return;

	// captions
	{
		//gfx->drawString(textStartPos, startPoxY, font, strings->getString("[Statistics]"));
		startPoxY -= 10; // this is to correct for the removal of the title
		
		int dec=0;
		std::string Total=strings->getString("[Total]");
		std::string free=strings->getString("[free]");
		std::string seeking=strings->getString("[seeking]");
		std::string slash="/";
		int sLen=font->getStringWidth(slash);

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 34, 66, 163));
		gfx->drawString(textStartPos, startPoxY+20, font, Total);
		font->popStyle();

		dec+=font->getStringWidth(Total);
		gfx->drawString(textStartPos+dec, startPoxY+20, font, "/");
		dec+=sLen;

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 22, 229, 40));
		gfx->drawString(textStartPos+dec, startPoxY+20, font, free);
		font->popStyle();

		dec+=font->getStringWidth(free);
		gfx->drawString(textStartPos+dec, startPoxY+20, font, "/");
		dec+=sLen;

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 150, 50, 50));
		gfx->drawString(textStartPos+dec, startPoxY+20, font, seeking);
		font->popStyle();

		dec=0;
		std::string Free=strings->getString("[Free]");
		std::string hungry=strings->getString("[hungry]");
		std::string starving=strings->getString("[starving]");
		std::string wounded=strings->getString("[wounded]");

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 22, 229, 40));
		gfx->drawString(textStartPos, startPoxY+104, font, Free);
		font->popStyle();

		/*dec+=font->getStringWidth(Free);
		gfx->drawString(textStartPos+dec, startPoxY+104, font, "/");
		dec+=sLen;*/

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 224, 210, 17));
		gfx->drawString(textStartPos+64, startPoxY+104, font, hungry);
		font->popStyle();

		/*dec+=font->getStringWidth(hungry);
		gfx->drawString(textStartPos+dec, startPoxY+104, font, "/");
		dec+=sLen;*/

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 249, 167, 14));
		gfx->drawString(textStartPos, startPoxY+104+12, font, starving);
		font->popStyle();

		/*dec+=font->getStringWidth(starving);
		gfx->drawString(textStartPos+dec, startPoxY+104, font, "/");
		dec+=sLen;*/

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 250, 25, 25));
		gfx->drawString(textStartPos+64, startPoxY+104+12, font, wounded);
		font->popStyle();
	}

	// graph
	for (int i=0; i<STATS_SIZE; i++)
	{
		int index=(statsIndex+i+1)&(STATS_SIZE - 1);

		int free=stats[index].isFree[WORKER]-stats[index].totalNeeded;
		int seeking=stats[index].totalNeeded;
		if (free<0)
		{
			free=0;
			seeking=stats[index].isFree[WORKER];
		}
		
		int nbFree=(free*64)/maxWorker;
		int nbSeeking=(seeking*64)/maxWorker;
		int nbTotal=(stats[index].numberUnitPerType[WORKER]*64)/maxWorker;
		
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 36 +64-nbTotal, nbTotal-nbFree-nbSeeking, 34, 66, 163);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 36 +64-nbFree-nbSeeking, nbFree, 22, 229, 40);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 36 +64-nbSeeking, nbSeeking, 150, 50, 50);

		int nbOk, nbNeedFood, nbNeedFoodCritical, nbNeedHeal;
		if (stats[index].totalUnit)
		{
			// to avoid some roundoff errors
			if (stats[index].needNothing>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
				nbNeedFoodCritical=(stats[index].needFoodCritical*64)/stats[index].totalUnit;
				nbOk=64-(nbNeedHeal+nbNeedFood+nbNeedFoodCritical);
			}
			else if (stats[index].needFood>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
				nbNeedFoodCritical=(stats[index].needFoodCritical*64)/stats[index].totalUnit;
				nbNeedFood=64-(nbNeedHeal+nbNeedFoodCritical);
				nbOk=0;
			}
			else if (stats[index].needFoodCritical>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=0;
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
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12  +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood-nbOk, nbOk, 22, 229, 40);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood, nbNeedFood, 224, 210, 17);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal-nbNeedFoodCritical, nbNeedFoodCritical, 249, 167, 14);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal, nbNeedHeal, 250, 25, 25);
	}
}

int TeamStats::getFreeUnits(int type)
{
	return (stats[statsIndex].isFree[type]);
}

int TeamStats::getTotalUnits(int type)
{
	return (stats[statsIndex].numberUnitPerType[type]);
}

int TeamStats::getWorkersNeeded()
{
	return (stats[statsIndex].totalNeeded);
}

int TeamStats::getWorkersBalance()
{
	return (stats[statsIndex].isFree[WORKER]-stats[statsIndex].totalNeeded);
}

int TeamStats::getWorkersLevel(int level)
{
	return (stats[statsIndex].upgradeState[BUILD][level]);
}

int TeamStats::getStarvingUnits()
{
	return (stats[statsIndex].needFoodCritical);
}

bool TeamStats::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("TeamStats");
	Uint32 size=0;
	size=stream->readUint32("size");

	bool stop=false;
	
	for (unsigned int i=0; i<size; i++)
	{
		stream->readEnterSection(i);
		Sint32 units = stream->readSint32("EndOfGameStat::TYPE_UNITS");
		Sint32 buildings = stream->readSint32("EndOfGameStat::TYPE_BUILDINGS");
		Sint32 prestige = stream->readSint32("EndOfGameStat::TYPE_PRESTIGE");
		Sint32 hp = 0;
		Sint32 attack = 0;
		Sint32 defense = 0;
		hp = stream->readSint32("EndOfGameStat::TYPE_HP");
		attack = stream->readSint32("EndOfGameStat::TYPE_ATTACK");
		defense = stream->readSint32("EndOfGameStat::TYPE_DEFENSE");
		if(!stop)
			endOfGameStats.push_back(EndOfGameStat(units, buildings, prestige, hp, attack, defense));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
}

void TeamStats::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("TeamStats");
	stream->writeUint32(endOfGameStats.size(), "size");
	for (unsigned int i=0; i<endOfGameStats.size(); i++)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_UNITS], "EndOfGameStat::TYPE_UNITS");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_BUILDINGS], "EndOfGameStat::TYPE_BUILDINGS");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_PRESTIGE], "EndOfGameStat::TYPE_PRESTIGE");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_HP], "EndOfGameStat::TYPE_HP");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_ATTACK], "EndOfGameStat::TYPE_ATTACK");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_DEFENSE], "EndOfGameStat::TYPE_DEFENSE");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

