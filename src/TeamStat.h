// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "UnitConsts.h"
#include "IntBuildingType.h"
#include "Ressource.h"

#include <vector>
#include "AITelemetry.h"

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
		
	int happiness[HAPPINESS_COUNT+1];
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

// Diagnostic only: never used by AI, orders, RNG or simulation checksums.
struct GameplayMeasurements
{
	enum DeathCause
	{
		COMBAT,
		STARVATION,
		CLEARING,
		TRAPPED,
		UNKNOWN,
		DEATH_CAUSES
	};
	enum DamageSource
	{
		MELEE,
		MAGIC,
		TOWER,
		DAMAGE_SOURCES
	};
	enum Target
	{
		UNIT,
		BUILDING,
		TARGETS
	};
	enum Purpose
	{
		MEAL,
		SPAWNING,
		AMMUNITION,
		CONSTRUCTION,
		UPGRADE,
		PURPOSES
	};
	enum Completion
	{
		NEW_BUILDING,
		UPGRADED,
		REPAIRED,
		COMPLETIONS
	};
	enum Removal
	{
		DESTROYED,
		DEMOLISHED,
		OTHER,
		REMOVALS
	};
	bool operator==(const GameplayMeasurements &) const = default;
	Uint32 tick = 0;
	Uint64 births[NB_UNIT_TYPE]{};
	Uint64 deaths[NB_UNIT_TYPE][DEATH_CAUSES]{};
	Uint64 conversionsIn[NB_UNIT_TYPE]{};
	Uint64 conversionsOut[NB_UNIT_TYPE]{};
	Uint64 harvested[MAX_NB_RESOURCES]{};
	Uint64 cleared[MAX_NB_RESOURCES]{};
	Uint64 delivered[MAX_NB_RESOURCES]{};
	Uint64 withdrawn[MAX_NB_RESOURCES]{};
	Uint64 transferredIn[MAX_NB_RESOURCES]{};
	Uint64 transferredOut[MAX_NB_RESOURCES]{};
	Uint64 consumed[PURPOSES][MAX_NB_RESOURCES]{};
	Uint64 repairDelivered[MAX_NB_RESOURCES]{};
	Uint64 meals{};
	Uint64 healingVisits{};
	Uint64 hpRestored{};
	Uint64 damageDealt[DAMAGE_SOURCES][TARGETS]{};
	Uint64 damageReceived[DAMAGE_SOURCES][TARGETS]{};
	Uint64 shots[DAMAGE_SOURCES]{};
	Uint64 impacts[DAMAGE_SOURCES][TARGETS]{};
	Uint64 completed[COMPLETIONS][IntBuildingType::NB_BUILDING][NB_UNIT_LEVELS]{};
	Uint64 removed[REMOVALS][IntBuildingType::NB_BUILDING][NB_BUILDING_LONG_LEVELS]{};
	Uint64 trainingVisits[NB_UNIT_TYPE]{};
	Uint64 abilityGains[NB_UNIT_TYPE][NB_ABILITY]{};
	Uint64 stock[MAX_NB_RESOURCES]{};
	Uint64 carried[MAX_NB_RESOURCES]{};
	Uint64 buildings[IntBuildingType::NB_BUILDING][NB_BUILDING_LONG_LEVELS]{};
	Uint64 hungry{};
	Uint64 critical{};
	Uint64 feeding{};
	Uint64 healing{};
	// Snapshot diagnostics. Structural counts ignore temporary unit occupancy.
	Uint64 trappedUnits[2][NB_UNIT_TYPE]{};
	Uint64 trappedBuildings[2][2][IntBuildingType::NB_BUILDING]{}; // blockage, swim ability, type
	Uint64 lowHP[3][NB_UNIT_TYPE]{};
	Uint64 lowFood[3][NB_UNIT_TYPE]{};
	Uint32 trappedTick = 0;
	// Cumulative natural map growth within 8, 16 and 32 tiles of this team.
	Uint64 growthTiles[3][MAX_NB_RESOURCES]{};
	Uint64 growthAmount[3][MAX_NB_RESOURCES]{};
	Uint64 growthReduction[3][MAX_NB_RESOURCES]{};
	Uint64 growthGlobal[3][MAX_NB_RESOURCES]{};
};

class Team;

class TeamStats
{
public:
  struct CoverageBuilding { int x, y, width, height; bool operator==(const CoverageBuilding &) const = default; };
  std::vector<CoverageBuilding> coverageBuildings;
  Uint32 coverageBuildingTick = 0;
  Uint32 coverageBuildingGeneration = 0;
  std::vector<std::shared_ptr<AITelemetry::Series>> aiTelemetry;
  GameplayMeasurements measurements;
  Uint32 coverageStartTick = 0;
  Uint32 extendedCoverageStartTick = 0;
  bool needsMeasurementInitialization = false;
  std::vector<GameplayMeasurements> measurementHistory;
  void initializeMeasurements(Uint32 tick);
  void refreshMeasurements(Team *team);
  void sampleTraps(Team *team);
  void beginMeasurementSnapshot(Team *team);
  void observeMeasurementUnit(class Unit *unit);
  void observeMeasurementBuilding(class Building *building);
  void printMeasurements(int team, bool final = false) const;
  static void recordDamage(Team *source, Team *target, int kind, int targetKind, int hp,
						   int damage);
  void drawMeasurements(int x, int y);
  void drawExpandedMeasurements(int x, int y);
  static Uint64 graphValue(const GameplayMeasurements &m, int metric);
  static const char *measurementLabel(int metric);

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
	
	//! Those stats are used when player has ended the game
	friend class Team;
	friend class Game;
	
	std::vector<EndOfGameStat> endOfGameStats;
	
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);

public:
	//! Read-only access for code that only measures, such as the win probability
	//! model, so it does not need friendship to reach the sample ring.
	const TeamStat *getLatestStat(void) const { return &(stats[statsIndex]); }
	TeamStat *getLatestStat(void) { return &(stats[statsIndex]); }
	//! Read-only access to the 512-tick EndOfGameStat history, used by the
	//! headless team-timeline dump (Engine::printAutomaticEndingSummary).
	const std::vector<EndOfGameStat> &getEndOfGameStats(void) const { return endOfGameStats; }
};
