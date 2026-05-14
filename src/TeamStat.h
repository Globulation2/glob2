// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "UnitConsts.h"
#include "IntBuildingType.h"
#include "Ressource.h"

#include <vector>

class Map;

//! Number of "long level" slots for the per-building-type histogram. The
//! long level is `(type->level << 1) + 1 - isBuildingSite` (see
//! Building::getLongLevel in building/Misc.cpp), giving the range 0..5
//! inclusive — six slots — so that finished buildings and their sites at
//! each level land in distinct bins. Distinct from Game.h's
//! MAX_BUILDING_LEVELS even though they happen to share the value 6.
static constexpr int NB_BUILDING_LONG_LEVELS = 6;
//! Highest valid long-level index (= NB_BUILDING_LONG_LEVELS - 1).
//! Used by the assertion in TeamStat.cpp guarding the histogram write.
static constexpr int MAX_BUILDING_LONG_LEVEL = NB_BUILDING_LONG_LEVELS - 1;

//! Bitmask used by TeamStats::step to append an EndOfGameStat snapshot
//! every 512 ticks (~20.5 s at 25 Hz): `(stepCounter & MASK) == 0`.
//! The 512-tick cadence is the gameplay-meaningful constant — the mask
//! width is independent of Team::MAX_COUNT. See TeamStat.cpp:122.
static constexpr int END_OF_GAME_STAT_INTERVAL_MASK = 0x1FF;

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
	int numberBuildingPerTypePerLevel[IntBuildingType::NB_BUILDING][NB_BUILDING_LONG_LEVELS];

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
	int totalNeededPerLevel[NB_UNIT_LEVELS];
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

