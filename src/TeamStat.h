/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __TEAM_STAT_H
#define __TEAM_STAT_H

#include "UnitConsts.h"
#include "BuildingType.h"

struct TeamStat
{
	int totalUnit;
	int numberUnitPerType[NB_UNIT_TYPE];
	int totalFree;
	int isFree[NB_UNIT_TYPE];
	int totalNeeded;

	int totalBuilding;
	int numberBuildingPerType[BuildingType::NB_BUILDING];
	int numberBuildingPerTypePerLevel[BuildingType::NB_BUILDING][6];

	int needFoodCritical;
	int needFood;
	int needHeal;
	int needNothing;
	int upgradeState[NB_ABILITY][4];

	int totalFood;
	int totalFoodCapacity;
	int totalUnitFoodable;
	int totalUnitFooded;
	
	//int totalPrestige;
	
	int happiness[HAPPYNESS_COUNT+1];
};

struct TeamSmoothedStat
{
	int totalFree;
	int isFree[NB_UNIT_TYPE];
	int totalNeeded;
};

struct EndOfGameStat
{
	enum Type
	{
		TYPE_UNITS = 0,
		TYPE_BUILDINGS = 1,
		TYPE_PRESTIGE = 2,
		TYPE_NB_STATS = 3
	};
	
	// units, buildings, prestige
	int value[TYPE_NB_STATS];
};

class Team;

class TeamStats
{
public:
	TeamStats();
	virtual ~TeamStats(void);
	
	void step(Team *team);
	void drawText(int pos);
	void drawStat(int pos);
	int getFreeUnits(int type);
	int getTotalUnits(int type);
	int getUnitsNeeded();

private:
	enum {
		STATS_SMOOTH_SIZE=32,
		STATS_SIZE=128
	};
	
	int statsIndex;
	TeamStat stats[STATS_SIZE];
	
	int smoothedIndex;
	TeamSmoothedStat smoothedStats[STATS_SMOOTH_SIZE];
	
	friend class EndGameStat;
	friend class EndGameScreen;
	
	enum { END_OF_GAME_STATS_SIZE=128 };
	
	//! Thoses stats are used when player has ned the game
	friend class Team;
	
	int endOfGameStatIndex;
	EndOfGameStat endOfGameStats[END_OF_GAME_STATS_SIZE];

public:
	TeamStat *getLatestStat(void) { return &(stats[statsIndex]); }
};

#endif
