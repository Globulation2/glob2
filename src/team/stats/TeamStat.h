// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "UnitConsts.h"
#include "UnitStatistics.h"
#include <array>
#include <span>
#include "Version.h"
#include "IntBuildingType.h"
#include "Ressource.h"

#include <vector>
#include <string>
#include "AITelemetry.h"

class Map;
class UnitCatalog;

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

//! Bitmask used by the snapshot pipeline to sample history and spatial diagnostics
//! every 512 ticks (~20.5 s at 25 Hz): `(stepCounter & MASK) == 0`.
//! The 512-tick cadence is the gameplay-meaningful constant — the mask
//! width is independent of Team::MAX_COUNT.
static constexpr int END_OF_GAME_STAT_INTERVAL_MASK = 0x1FF;

// Wire-locked growth-distance bands: keep order and count stable for old saves.
inline constexpr int GROWTH_COVERAGE_RADII[] = {8, 16, 32};
inline constexpr int GROWTH_COVERAGE_BANDS = sizeof(GROWTH_COVERAGE_RADII) / sizeof(GROWTH_COVERAGE_RADII[0]);

struct TeamStat
{
	TeamStat();
	void reset();
    void configureUnits(std::size_t count);
    std::size_t capacityBytes() const { return buildingCountByVariant.capacity()*sizeof(int)+numberUnitPerType.extraCapacityBytes()+isFree.extraCapacityBytes()+upgradeStatePerType.extraCapacityBytes(); }

	int totalUnit;
	UnitStatistics<int> numberUnitPerType;
	int workersByConstructionLevel[NB_UNIT_LEVELS];
	int totalFree;
	UnitStatistics<int> isFree;
	int totalNeeded;
	int totalNeededPerLevel[NB_UNIT_LEVELS];

	std::vector<int> buildingCountByVariant; // concrete catalog IDs, including construction variants
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
	UnitStatistics<std::array<std::array<int,NB_UNIT_LEVELS>,NB_ABILITY>> upgradeStatePerType;

	int totalFood;
	int totalFoodCapacity;
	int totalUnitFoodable;
	int totalUnitFooded;

	int totalHP;
	int totalAttackPower;
    int carriers=0,builders=0,meleeUnits=0,rangedUnits=0,scouts=0,idleCarriers=0,idleDefenders=0;
	int totalDefensePower;
		
	int happiness[HAPPINESS_COUNT+1];
};

struct TeamSmoothedStat
{
	TeamSmoothedStat();
	void reset();
    void configureUnits(std::size_t count) { isFree.resize(count); }

	int totalFree;
	UnitStatistics<int> isFree;
	int totalNeeded;
	int totalNeededPerLevel[NB_UNIT_LEVELS];
};

struct EndOfGameStat
{
    std::size_t capacityBytes() const { return 0; }
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

// Probability is alliance-wide and sampled after outcome evaluation, separately
// from the delayed per-team statistics pipeline. Explicit ticks preserve gaps
// when continuing older saves; missing history must never become zero chance.
struct WinProbabilitySample
{
    Uint32 tick = 0;
    int permille = 0;
};

// Per-variant diagnostics use the catalog's concrete IDs; no family/tier limit.
struct BuildingMeasurement
{
	Uint64 count = 0;
	Uint64 completed[3]{};
	Uint64 removed[3]{};
	Uint64 trapped[2][2]{};
	bool operator==(const BuildingMeasurement&) const = default;
};

// Collected on the owner and published from snapshots. Diagnostic values do not
// drive rules or RNG; scheduled state is included in current simulation checksums.
struct GameplayMeasurements
{
    std::size_t capacityBytes() const { return variants.capacity()*sizeof(BuildingMeasurement)+unitCapacityBytes(); }
    void configureUnits(std::size_t count) {
        births.resize(count); deaths.resize(count); conversionsIn.resize(count); conversionsOut.resize(count);
        trainingVisits.resize(count); abilityGains.resize(count); combatDeathPlace.resize(count); combatDeathAssignment.resize(count);
        for (auto& row:trappedUnits) row.resize(count);
        for (auto& row:lowHP) row.resize(count);
        for (auto& row:lowFood) row.resize(count);
    }

    std::size_t unitCapacityBytes() const {
        std::size_t bytes=births.extraCapacityBytes()+deaths.extraCapacityBytes()+conversionsIn.extraCapacityBytes()+conversionsOut.extraCapacityBytes()
            +trainingVisits.extraCapacityBytes()+abilityGains.extraCapacityBytes()+combatDeathPlace.extraCapacityBytes()+combatDeathAssignment.extraCapacityBytes();
        for(const auto& row:trappedUnits)bytes+=row.extraCapacityBytes();
        for(const auto& row:lowHP)bytes+=row.extraCapacityBytes();
        for(const auto& row:lowFood)bytes+=row.extraCapacityBytes();
        return bytes;
    }

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
		HEALING_COST,
		TRAINING_COST,
		AREA_UPKEEP,
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
	// Worker time use. Each live worker counts once per tick: in labour[] by
	// activity, or, while filling a building, in filling[][] by job and phase.
	enum LabourActivity
	{
		IDLE,
		EAT_WALKING,
		EAT_INSIDE,
		EAT_NO_INN,
		HEAL_WALKING,
		HEAL_INSIDE,
		HEAL_NO_HOSPITAL,
		TRAIN_WALKING,
		TRAIN_INSIDE,
		FLAG_WORK,
		OTHER_ACTIVITY,
		LABOUR_ACTIVITIES
	};
	// The building a filling worker serves, and what it is doing for it.
	enum LabourJob
	{
		SWARM_JOB,
		INN_JOB,
		SITE_JOB,
		OTHER_JOB,
		LABOUR_JOBS
	};
	enum LabourPhase
	{
		TO_RESOURCE,
		HARVESTING,
		TO_BUILDING,
		OTHER_PHASE,
		LABOUR_PHASES
	};
	// Where a unit is: within GROWTH_COVERAGE_RADII[PLACE_BAND] tiles of one of its
	// team's buildings, else of an enemy's, else neither. Flags are not buildings
	// here, and buildings are those of each team's last 512-tick snapshot.
	enum Place
	{
		HOME,
		AWAY,
		FIELD,
		PLACES
	};
	static constexpr int PLACE_BAND = 1;
	// The building a unit was attached to when it died.
	enum Assignment
	{
		UNASSIGNED,
		WAR_FLAG,
		CLEARING_FLAG,
		EXPLORATION_FLAG,
		OTHER_BUILDING,
		ASSIGNMENTS
	};
	bool operator==(const GameplayMeasurements &) const = default;
	std::vector<BuildingMeasurement> variants;
	Uint32 tick = 0;
	UnitStatistics<Uint64> births;
	UnitStatistics<std::array<Uint64,DEATH_CAUSES>> deaths;
	UnitStatistics<Uint64> conversionsIn;
	UnitStatistics<Uint64> conversionsOut;
	Uint64 harvested[MaterialSlotCount]{};
	Uint64 cleared[MaterialSlotCount]{};
	Uint64 delivered[MaterialSlotCount]{};
	Uint64 withdrawn[MaterialSlotCount]{};
	Uint64 transferredIn[MaterialSlotCount]{};
	Uint64 transferredOut[MaterialSlotCount]{};
	Uint64 consumed[PURPOSES][MaterialSlotCount]{};
	Uint64 repairDelivered[MaterialSlotCount]{};
	Uint64 materialSpillageEvents{};
	Uint64 meals{};
	Uint64 healingVisits{};
	Uint64 hpRestored{};
	Uint64 damageDealt[DAMAGE_SOURCES][TARGETS]{};
	Uint64 damageReceived[DAMAGE_SOURCES][TARGETS]{};
	Uint64 shots[DAMAGE_SOURCES]{};
	Uint64 impacts[DAMAGE_SOURCES][TARGETS]{};
	Uint64 completed[COMPLETIONS][IntBuildingType::NB_BUILDING][NB_UNIT_LEVELS]{};
	Uint64 removed[REMOVALS][IntBuildingType::NB_BUILDING][NB_BUILDING_LONG_LEVELS]{};
	UnitStatistics<Uint64> trainingVisits;
	UnitStatistics<std::array<Uint64,NB_ABILITY>> abilityGains;
	Uint64 stock[MaterialSlotCount]{};
	Uint64 carried[MaterialSlotCount]{};
	Uint64 buildings[IntBuildingType::NB_BUILDING][NB_BUILDING_LONG_LEVELS]{};
	Uint64 hungry{};
	Uint64 critical{};
	Uint64 feeding{};
	Uint64 healing{};
    // Instantaneous population by run/swim class, retained in chart history.
    bool movementClassesKnown = false;
    std::array<Uint32, SWIM_CLASS_COUNT> groundUnitsByMovementClass{};
	// Snapshot diagnostics. Structural counts ignore temporary unit occupancy.
	std::array<UnitStatistics<Uint64>,2> trappedUnits;
	Uint64 trappedBuildings[2][2][IntBuildingType::NB_BUILDING]{}; // blockage, swim ability, type
	std::array<UnitStatistics<Uint64>,3> lowHP;
	std::array<UnitStatistics<Uint64>,3> lowFood;
	Uint32 trappedTick = 0;
	// Cumulative natural map growth within 8, 16 and 32 tiles of this team.
	Uint64 growthTiles[GROWTH_COVERAGE_BANDS][MaterialSlotCount]{};
	Uint64 growthAmount[GROWTH_COVERAGE_BANDS][MaterialSlotCount]{};
	Uint64 growthReduction[GROWTH_COVERAGE_BANDS][MaterialSlotCount]{};
	Uint64 growthGlobal[3][MaterialSlotCount]{};
	// Format 129 (FILE_FORMAT_VERSION_LABOUR_STATS). Cumulative worker-ticks.
	Uint64 labour[LABOUR_ACTIVITIES]{};
	Uint64 filling[LABOUR_JOBS][LABOUR_PHASES]{};
	// Distance sums and sample counts: harvesting workers to the building they
	// fill, and hungry workers walking to their inn.
	Uint64 harvestDistance[LABOUR_JOBS]{};
	Uint64 harvestSamples[LABOUR_JOBS]{};
	Uint64 eatWalkDistance{};
	Uint64 eatWalkSamples{};
	UnitStatistics<std::array<Uint64,PLACES>> combatDeathPlace;
	UnitStatistics<std::array<Uint64,ASSIGNMENTS>> combatDeathAssignment;
	// Defence snapshot at defenceTick: live warriors by place, their summed attack
	// speed and strength levels, and enemy warriors at this team's home.
	Uint64 warriors[PLACES]{};
	Uint64 warriorLevels[PLACES]{};
	Uint64 warriorsHurt{};
	Uint64 warriorsFlagged{};
	Uint64 warriorsInside{};
	Uint64 intruders{};
	Uint64 intruderLevels{};
	Uint32 defenceTick = 0;
};

// Only cumulative events cross the read boundary. Instantaneous measurements and
// labour deltas are calculated afresh; they are never copied from the collector.
struct GameplayEventCounters
{
    struct Variant { Uint64 completed[3]{}, removed[3]{}; };
    std::vector<Variant> variants;
    decltype(GameplayMeasurements::births) births{};
    decltype(GameplayMeasurements::deaths) deaths{};
    decltype(GameplayMeasurements::conversionsIn) conversionsIn{};
    decltype(GameplayMeasurements::conversionsOut) conversionsOut{};
    decltype(GameplayMeasurements::harvested) harvested{};
    decltype(GameplayMeasurements::cleared) cleared{};
    decltype(GameplayMeasurements::delivered) delivered{};
    decltype(GameplayMeasurements::withdrawn) withdrawn{};
    decltype(GameplayMeasurements::transferredIn) transferredIn{};
    decltype(GameplayMeasurements::transferredOut) transferredOut{};
    decltype(GameplayMeasurements::consumed) consumed{};
    decltype(GameplayMeasurements::repairDelivered) repairDelivered{};
    decltype(GameplayMeasurements::materialSpillageEvents) materialSpillageEvents{};
    decltype(GameplayMeasurements::meals) meals{};
    decltype(GameplayMeasurements::healingVisits) healingVisits{};
    decltype(GameplayMeasurements::hpRestored) hpRestored{};
    decltype(GameplayMeasurements::damageDealt) damageDealt{};
    decltype(GameplayMeasurements::damageReceived) damageReceived{};
    decltype(GameplayMeasurements::shots) shots{};
    decltype(GameplayMeasurements::impacts) impacts{};
    decltype(GameplayMeasurements::completed) completed{};
    decltype(GameplayMeasurements::removed) removed{};
    decltype(GameplayMeasurements::trainingVisits) trainingVisits{};
    decltype(GameplayMeasurements::abilityGains) abilityGains{};
    decltype(GameplayMeasurements::growthTiles) growthTiles{};
    decltype(GameplayMeasurements::growthAmount) growthAmount{};
    decltype(GameplayMeasurements::growthReduction) growthReduction{};
    decltype(GameplayMeasurements::growthGlobal) growthGlobal{};
    decltype(GameplayMeasurements::combatDeathPlace) combatDeathPlace{};
    decltype(GameplayMeasurements::combatDeathAssignment) combatDeathAssignment{};
    void capture(const GameplayMeasurements&);
    void applyTo(GameplayMeasurements&) const;
    std::size_t capacityBytes() const;
};

class Team;
struct TeamStatsSample;
namespace SimulationSnapshot { struct Handle; }

class TeamStats
{
public:
    // Unknown on startup/legacy loads: retain periodic work until a sample arrives.
    bool usesGroundMovementClass(int swim) const
    { const auto& m=measurements(); return !m.movementClassesKnown || m.groundUnitsByMovementClass[swim] != 0; }
    std::size_t unitTypeCount=NB_UNIT_TYPE;
    void configureUnits(std::size_t count);

  struct CoverageBuilding { int x, y, width, height; bool operator==(const CoverageBuilding &) const = default; };
  std::vector<CoverageBuilding> coverageBuildings;
  Uint32 coverageBuildingTick = 0;
  Uint32 coverageBuildingGeneration = 0;
  std::vector<std::shared_ptr<AITelemetry::Series>> aiTelemetry;
  // Cold legacy scans/load/fixture edits detach a retained immutable frame.
  GameplayMeasurements& editMeasurements();
  const GameplayMeasurements& measurements() const;
  std::shared_ptr<const GameplayMeasurements> publishedMeasurementFrame() const { return publishedMeasurements; }
  Uint32 coverageStartTick = 0;
  Uint32 extendedCoverageStartTick = 0;
  Uint32 labourCoverageStartTick = 0;
  bool needsMeasurementInitialization = false;
  std::vector<GameplayMeasurements> measurementHistory;
  void initializeMeasurements(Uint32 tick);
  void refreshMeasurements(Team *team);
  void sampleTraps(Team *team);
  void sampleDefence(Team *team);
  void observeLabour(class Unit *unit);
  void recordCombatDeath(class Unit *unit);
  static GameplayMeasurements::Place placeOf(const Team *team, int x, int y);
  void beginMeasurementSnapshot(Team *team);
  void observeMeasurementUnit(class Unit *unit);
  void observeMeasurementBuilding(class Building *building);
  void printMeasurements(int team, bool final = false) const;
  static void recordDamage(Team *source, Team *target, int kind, int targetKind, int hp,
						   int damage);

  TeamStats();
  virtual ~TeamStats(void);

  void step(Team *team, bool reloaded = false);
  void enableCollection();
  GameplayMeasurements& events() { return collectionEnabled ? collectedMeasurements : editMeasurements(); }
  const GameplayMeasurements& events() const { return collectionEnabled ? collectedMeasurements : measurements(); }
  void publishFrame(TeamStatsSample&, Uint32 publicationTick, const std::shared_ptr<const UnitCatalog>&);
  static void completeMeasurements(TeamStatsSample&, const GameplayMeasurements& previous);
  void applySample(TeamStatsSample&&, Uint32 publicationTick, const std::shared_ptr<const UnitCatalog>&);
  void applySample(const TeamStatsSample&, Uint32 publicationTick, const std::shared_ptr<const UnitCatalog>&);
  unsigned samplingPhase() const { return smoothedIndex; }
  // Capture the latest 31 published rows only at aggregate boundaries; workers add their current row.
  void captureSmoothingWindow(std::vector<TeamSmoothedStat>&) const;
  static void finishAggregate(TeamStat&, const UnitStatistics<std::array<int,4>>&,
      std::span<const TeamSmoothedStat>, const UnitCatalog&, const TeamSmoothedStat* current = nullptr);
  Uint32 sourceTick = 0, publishedTick = 0;
  Uint32 checkSum() const;
  static Uint32 measurementChecksum(const GameplayMeasurements&);
  void printEconomy(const Team*, Uint32 tick) const;
  static void saveSample(GAGCore::OutputStream*, const TeamStatsSample&);
  static TeamStatsSample loadSample(GAGCore::InputStream*, int versionMinor = VERSION_MINOR,bool privateAdaptive174=false);


  std::shared_ptr<const TeamStats> frozenDisplay() const;
  std::shared_ptr<const TeamStats> frozenHistory() const;
  size_t displayCapacityBytes() const;
  void drawText(int posx, int posy) const;
  void drawStat(int posx, int posy) const;
  int getFreeUnits(int type) const;
  int getTotalUnits(int type) const;
  int getWorkersNeeded() const;
  int getWorkersBalance() const;
  int getWorkersLevel(int level) const;
  int getStarvingUnits() const;

private:
    GameplayMeasurements legacyMeasurements;
    std::shared_ptr<GameplayMeasurements> publishedMeasurements;
    bool collectionEnabled = false;
    GameplayMeasurements collectedMeasurements;
    mutable std::weak_ptr<const TeamStats> displaySnapshot;
    mutable std::weak_ptr<const TeamStats> historySnapshot;
    // Sampling scratch and immutable qualification rows are derived live state.
    // Custom catalogs allocate these once; snapshots and saves omit them.
    TeamSmoothedStat samplingMaxima;
    UnitStatistics<std::array<int,4>> samplingEligibility;
    UnitStatistics<std::array<Uint8,2>> samplingQualifications;
    std::shared_ptr<const UnitCatalog> samplingCatalog;
    void configureSamplingCatalog(const std::shared_ptr<const UnitCatalog>& catalog);

	// Derived reset index for the live per-variant count, never serialized.
	// Counts are maintained by begin/observe; cold refresh/import rebuilds this
	// index when replacing externally supplied diagnostic measurements.
	std::vector<size_t> measurementCountTouched;
	size_t measurementCountCatalogSize = 0;
	void rebuildMeasurementCountReset();

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
	
	//! Those stats are used when player has ended the game
	friend class Team;
	friend class Game;
	
	std::vector<EndOfGameStat> endOfGameStats;
    std::vector<WinProbabilitySample> winProbabilityHistory;
    std::string winProbabilityModel;
	
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor,bool privateAdaptive174=false);
	void save(GAGCore::OutputStream *stream);

public:
    void recordWinProbability(Uint32 tick, int permille, const std::string &model);
    const std::vector<WinProbabilitySample> &getWinProbabilityHistory() const { return winProbabilityHistory; }
    const std::string &getWinProbabilityModel() const { return winProbabilityModel; }
    // Twice the trapezoidal area, in permille*ticks. Integer until reporting.
    Uint64 winProbabilityAreaTwice() const;
	TeamStat *getLatestStat(void) { displaySnapshot.reset(); return &(stats[statsIndex]); }
	//! Read-only access for code that only measures, such as the win probability
	//! model, so it does not need friendship to reach the sample ring.
	const TeamStat *getLatestStat(void) const { return &(stats[statsIndex]); }
	//! Read-only access to the 512-tick EndOfGameStat history, used by the
	//! headless team-timeline dump (Engine::printAutomaticEndingSummary).
	const std::vector<EndOfGameStat> &getEndOfGameStats(void) const { return endOfGameStats; }
};

// Raw rows remain consecutive owner history; aggregate results can be finalized by workers.
struct TeamStatsSample
{
    Uint32 tick = 0;
    int prestige = 0;
    bool aggregate = false, periodic = false, finalized = false;
    TeamSmoothedStat smoothing;
    TeamStat stat;
    UnitStatistics<std::array<int,4>> eligible;
    GameplayMeasurements measurements;
    // Complete immutable worker result; publication swaps ownership with a reusable old frame.
    std::shared_ptr<GameplayMeasurements> frame;
    const GameplayMeasurements& measurementFrame() const { return frame ? *frame : measurements; }
    std::vector<TeamStats::CoverageBuilding> coverage;
};
TeamStatsSample computeTeamStatsSample(const SimulationSnapshot::Handle&, unsigned team, bool aggregate,
    std::span<const TeamSmoothedStat> window = {}, bool includeCurrent = false);
