// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <list>
#include <string>

#include "IntBuildingType.h"
#include "AIImplementation.h"
#include "AICastorTuning.h"

#include <memory>

struct Case;
class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

class AICastor : public AIImplementation
{
	static const bool verbose = false;
public:
	static const int NB_HARD_BUILDING=8;

	// "Never run yet" sentinel for AICastor's per-map computation timers
	// (lastFreeWorkersComputed, lastWheatGrowthMapComputed, lastEnemy*MapComputed,
	// and Project::timer). Stored as Uint32, compared as ">timer+N" so the
	// all-ones bit pattern always reads as "in the distant future" until first set.
	// C++: Lifecycle.cpp:68, 135-139.
	static constexpr Uint32 AI_CASTOR_TIMER_NEVER = static_cast<Uint32>(-1);

	// "Unset" sentinel for Project worker counts (mainWorkers, foodWorkers,
	// otherWorkers, finalWorkers). A negative value means "this project does
	// not override this worker class"; continueProject only writes worker
	// counts when the field is >= 0.
	// C++: Lifecycle.cpp:58-64.
	static constexpr Sint32 AI_CASTOR_WORKERS_UNSET = -1;

	// "No critical project found" sentinel for the lower-is-better project
	// priority scan in getOrder(). Set to INT32_MAX so the first real
	// project priority always wins the min-search.
	// C++: GetOrder.cpp:143.
	static constexpr Sint32 AI_CASTOR_PRIORITY_NONE = 0x7FFFFFFF;

	// "No enemy building seen yet" sentinel in controlStrikes() while
	// scanning for the highest enemy building level.
	// C++: Control.cpp:391.
	static constexpr int AI_CASTOR_LEVEL_NONE = -1;

	// "No candidate target team yet" sentinel for the controlStrikes()
	// per-team score search. Score is non-negative; -1 forces the first
	// real score to win.
	// C++: Control.cpp:410.
	static constexpr int AI_CASTOR_SCORE_NONE = -1;

	// Project::subPhase state-machine values. The original code uses raw
	// ints 0,1,2,3,5,6 — value 4 is intentionally absent (a retired phase
	// that was never renumbered). Names mirror the comments at each
	// branch in continueProject() (Projects.cpp:258 onward).
	// C++: Projects.cpp:258-499.
	enum SubPhase : int
	{
		AI_CASTOR_SUBPHASE_BOOT           = 0, // initial / boot
		AI_CASTOR_SUBPHASE_FIND_PLACE     = 1, // find good building place
		AI_CASTOR_SUBPHASE_CHECK_SITES    = 2, // do we have enough building sites?
		AI_CASTOR_SUBPHASE_BALANCE_MAIN   = 3, // balance workers across building/food/other
		// value 4 unused — retired phase, intentionally skipped to preserve
		// numeric values of the surviving phases.
		AI_CASTOR_SUBPHASE_WAIT_FINISHED  = 5, // wait for buildings to finish
		AI_CASTOR_SUBPHASE_BALANCE_FINAL  = 6, // balance final workers
	};

	class Project
	{
	public:
		Project(IntBuildingType::Number shortTypeNum, const char *suffix);
		Project(IntBuildingType::Number shortTypeNum, int amount, Sint32 mainWorkers, const char *suffix);
		void init(const char *suffix);

	public:
		IntBuildingType::Number shortTypeNum;
		int amount; // number of buildings wanted
		bool food; // place closer to wheat of further
		bool defense; // place at incpoming places.
		
		std::string debugStdName;
		const char *debugName;
		
		int subPhase;
		
		Uint32 successWait; // wait a number of success in the hope to find a better one just a bit later.
		bool blocking; // if true, no other project can be added..
		bool critical; // if true, place building at any cost.
		Sint32 priority; // the lower is the number, the higher is the priority
		Uint32 triesLeft;
		
		Sint32 mainWorkers;
		Sint32 foodWorkers;
		Sint32 otherWorkers;
		
		bool multipleStart;
		bool waitFinished;
		Sint32 finalWorkers;
		
		bool finished;
		
		Uint32 timer;
	};
	
	class Strategy
	{
	public:
		struct Build
		{
			int baseOrder;
			int base;
			int baseWorkers;
			int baseUpgrade;
			
			int finalWorkers;
			
			int newOrder;
			int news;
			int newWorkers;
			int newUpgrade;
		};
		
	public:
		Strategy();
	public:
		bool defined;
		
		Sint32 successWait;
		Sint32 isFreePart;
		
		Build build[IntBuildingType::NB_BUILDING];
		
		Uint32 warTimeTrigger;
		Sint32 warLevelTrigger;
		Sint32 warAmountTrigger;
		
		Uint32 strikeTimeTrigger;
		Sint32 strikeWarPowerTriggerUp;
		Sint32 strikeWarPowerTriggerDown;
		
		Sint32 maxAmountGoal;
	};
	
private:
	void firstInit();
public:
	AICastor(Player *player);
	~AICastor();

	Player *player;
	Team *team;
	Game *game;
	Map *map;

	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	std::shared_ptr<Order>getOrder(void);
	
private:
	void init(Player *player);
	void defineStrategy();
	
	std::shared_ptr<Order>controlSwarms();
	std::shared_ptr<Order>expandFood();
	std::shared_ptr<Order>controlFood();
	std::shared_ptr<Order>controlUpgrades();
	std::shared_ptr<Order>controlStrikes();
//	std::shared_ptr<Order>controlBaseDefense();
	
	bool addProject(Project *project);
	void addProjects();
	
	void choosePhase();
	
	std::shared_ptr<Order>continueProject(Project *project);
	
	bool enoughFreeWorkers();
	void computeCanSwim();
	void computeNeedSwim();
	void computeBuildingSum();
	void computeWarLevel();
	
	void computeObstacleUnitMap();
	void computeObstacleBuildingMap();
	void computeSpaceForBuildingMap(int max);
	void computeBuildingNeighbourMap(int dw, int dh);
	void computeBuildingNeighbourMapOfBuilding(int bx, int by, int bw, int bh, int dw, int dh);
	
	void computeWorkPowerMap();
	void computeWorkRangeMap();
	void computeWorkAbilityMap();
	void computeHydratationMap();
	void computeNotGrassMap();
	void computeWheatCareMap();
	void computeWheatGrowthMap();
	
	void computeEnemyPowerMap();
	void computeEnemyRangeMap();
	void computeEnemyWarriorsMap();

	std::shared_ptr<Order>findGoodBuilding(Sint32 typeNum, bool food, bool defense, bool critical);
	
	void computeRessourcesCluster();
	
public:
	void updateGlobalGradientNoObstacle(Uint8 *gradient);
	void updateGlobalGradient(Uint8 *gradient);
	
	std::list<Project *> projects;
	
	Uint32 timer;
	bool canSwim;
	bool needSwim;
	int buildingSum[IntBuildingType::NB_BUILDING][2]; // [shortTypeNum][isBuildingSite]
	int buildingLevels[IntBuildingType::NB_BUILDING][2][4]; // [shortTypeNum][isBuildingSite][level]
	int warLevel; // 0: no war
	int warTimeTriggerLevel;
	int warLevelTriggerLevel;
	int warAmountTriggerLevel;
	
	bool onStrike;
	Uint32 strikeTimeTrigger;
	bool strikeTeamSelected;
	int strikeTeam;
	
	bool foodWarning; // true if wwe are aproaching a foodLock
	bool foodLock; // we stop producing any unit until we get more food buildings
	bool foodSurplus; // we have too many food buildings
	Uint32 foodLockStats[2];
	bool overWorkers;
	bool starvingWarning;
	Uint32 starvingWarningStats[2];
	int buildsAmount;
	int freeWorkers; // plz use getFreeWorkers() to raise computation trigger.
	
	Uint32 lastFreeWorkersComputed;
	Uint32 lastWheatGrowthMapComputed;
	Uint32 lastEnemyRangeMapComputed;
	Uint32 lastEnemyPowerMapComputed;
	Uint32 lastEnemyWarriorsMapComputed;
	
	Uint32 computeNeedSwimTimer;
	Uint32 controlSwarmsTimer;
	Uint32 expandFoodTimer;
	Uint32 controlFoodTimer;
	Uint32 controlUpgradeTimer;
	Uint32 controlUpgradeDelay;
	Uint32 controlStrikesTimer;
	
	Strategy strategy;
	
	int computeBoot;
	
public:
	Uint8 *obstacleUnitMap; // where units can go. included in {0, 1}
	Uint8 *obstacleBuildingMap; // where buildings can be built. included in {0, 1}
	Uint8 *spaceForBuildingMap; // where building can be built, of size X*X. included in {0, 1, 2}. More iterations can provide arbitrary size.
	Uint8 *buildingNeighbourMap; // bit 0: bad flag, bits [1, 3]: direct neighbours count, bit 4: zero, bits [5, 7]; far neighbours count.
	
	Uint8 *workPowerMap;
	Uint8 *workRangeMap;
	Uint8 *workAbilityMap;
	Uint8 *hydratationMap;
	Uint8 *notGrassMap;
	Uint8 *wheatGrowthMap;
	Uint8 *oldWheatGradient[4];  // [0] is the most recent
	Uint8 *wheatCareMap[2];
	
	Uint8 *goodBuildingMap; // TODO: remove.
	
	Uint8 *enemyPowerMap;
	Uint8 *enemyRangeMap;
	Uint8 *enemyWarriorsMap;
	
	Uint16 *ressourcesCluster;
};


 

