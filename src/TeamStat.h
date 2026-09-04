// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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

	int totalUnit;
	int numberUnitPerType[NB_UNIT_TYPE];
	int totalFree;
	int isFree[NB_UNIT_TYPE];
	int totalNeeded;
	int totalNeededPerLevel[NB_UNIT_LEVELS];

	int totalBuilding; // Note that this is the total number of *finished* buildings, building sites are ignored
	int numberBuildingPerType[IntBuildingType::NB_BUILDING];
	int numberBuildingPerTypePerLevel[IntBuildingType::NB_BUILDING][6];

	int needFoodCritical;
	// Number of units that are hungry but there aren't able to eat
	int needFoodNoInns;
	int needFood;
	int needHeal;
	int needNothing;
	int upgradeState[NB_ABILITY][NB_UNIT_LEVELS];
	int upgradeStatePerType[NB_UNIT_TYPE][NB_ABILITY][NB_UNIT_LEVELS];

	int totalFood;
	int totalFoodCapacity;
	int totalUnitFoodable;
	int totalUnitFooded;

	int totalHP;
	int totalAttackPower;
	int totalDefensePower;
		
	int happiness[HAPPYNESS_COUNT+1];
};

struct TeamSmoothedStat
{
	TeamSmoothedStat();
	void reset();

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

	void drawText(int posx, int posy);
	void drawStat(int posx, int posy);
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

public:
	TeamStat *getLatestStat(void) { return &(stats[statsIndex]); }
};

#endif
