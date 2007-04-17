/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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
#include "IntBuildingType.h"
#include "Ressource.h"

#include <vector>

class Map;

struct TeamStat
{
	TeamStat();
	void reset();
	void setMapSize(int w, int h);

	int totalUnit;
	int numberUnitPerType[NB_UNIT_TYPE];
	int totalFree;
	int isFree[NB_UNIT_TYPE];
	int totalNeeded;
	int totalNeededPerLevel[4];

	int totalBuilding; // Note that this is the total number of *finished* buildings, building sites are ignored
	int numberBuildingPerType[IntBuildingType::NB_BUILDING];
	int numberBuildingPerTypePerLevel[IntBuildingType::NB_BUILDING][6];

	int needFoodCritical;
	int needFood;
	int needHeal;
	int needNothing;
	int upgradeState[NB_ABILITY][4];

	int totalFood;
	int totalFoodCapacity;
	int totalUnitFoodable;
	int totalUnitFooded;

	int totalHP;
	int totalAttackPower;
	int totalDefensePower;
		
	int happiness[HAPPYNESS_COUNT+1];

	int width;
	int getPos(int x, int y) { return y*width+x; } 
	void increasePoint(std::vector<int>& numberMap, int& max, int x, int y, Map* map);
	void spreadPoint(std::vector<int>& numberMap, int& max, int x, int y, Map* map, int value, int distance);

	std::vector<int> starvingMap;
	int starvingMax;
	std::vector<int> damagedMap;
	int damagedMax;
	std::vector<int> defenseMap;
	int defenseMax;
};

struct TeamSmoothedStat
{
	TeamSmoothedStat();
	void reset();
	void setMapSize(int w, int h);

	int totalFree;
	int isFree[NB_UNIT_TYPE];
	int totalNeeded;
	int totalNeededPerLevel[4];
};

struct EndOfGameStat
{
	EndOfGameStat(int units, int buildings, int prestige, int hp, int attack, int defense);

	enum Type
	{
		TYPE_UNITS = 0,
		TYPE_BUILDINGS = 1,
		TYPE_PRESTIGE = 2,
		TYPE_HP = 3,
		TYPE_ATTACK = 4,
		TYPE_DEFENSE = 5,
		TYPE_NB_STATS = 6
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
	
	void step(Team *team, bool reloaded = false);

	void setMapSize(int w, int h);

	void drawText(int pos);
	void drawStat(int pos);
	int getFreeUnits(int type);
	int getTotalUnits(int type);
	int getWorkersNeeded();
	int getWorkersBalance();
	int getWorkersLevel(int level);
	int getStarvingUnits();

private:
	enum
	{
		STATS_SMOOTH_SIZE=32,
		STATS_SIZE=128
	};
	
	int statsIndex;
	TeamStat stats[STATS_SIZE];
	bool haveSetMapSize;
	
	int smoothedIndex;
	TeamSmoothedStat smoothedStats[STATS_SMOOTH_SIZE];
	
	friend class EndGameStat;
	friend class EndGameScreen;
	
	//! Thoses stats are used when player has ended the game
	friend class Team;
	friend class Game;
	
	std::vector<EndOfGameStat> endOfGameStats;
	
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);

	int width;
	int height;

public:
	TeamStat *getLatestStat(void) { return &(stats[statsIndex]); }
};

#endif
