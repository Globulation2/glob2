// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <list>
#include <memory>
#include <string>

#include "IntBuildingType.h"
#include "AIImplementation.h"
#include "AICastorTuning.h"

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

	// "Never run yet" for the per-map computation timers. All-ones compares as
	// "in the future" against ">timer+N", so a zero would not do.
	static constexpr Uint32 AI_CASTOR_TIMER_NEVER = static_cast<Uint32>(-1);

	// Project worker counts below zero mean "this project does not override that worker class".
	static constexpr Sint32 AI_CASTOR_WORKERS_UNSET = -1;

	// Initial value for the lower-is-better project priority min-search.
	static constexpr Sint32 AI_CASTOR_PRIORITY_NONE = 0x7FFFFFFF;

	// Initial values for the controlStrikes() max-searches; levels and scores are non-negative.
	static constexpr int AI_CASTOR_LEVEL_NONE = -1;
	static constexpr int AI_CASTOR_SCORE_NONE = -1;

	enum SubPhase : int
	{
		AI_CASTOR_SUBPHASE_BOOT,
		AI_CASTOR_SUBPHASE_FIND_PLACE,
		AI_CASTOR_SUBPHASE_CHECK_SITES,   // do we have enough building sites?
		AI_CASTOR_SUBPHASE_BALANCE_MAIN,  // balance workers across building/food/other
		AI_CASTOR_SUBPHASE_WAIT_FINISHED,
		AI_CASTOR_SUBPHASE_BALANCE_FINAL,
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
		bool food; // place closer to wheat
		bool defense; // place at incoming places
		
		std::string debugStdName;
		const char *debugName;
		
		int subPhase;
		
		Uint32 successWait; // wait a number of successes in the hope of finding a better one a bit later
		bool blocking; // if true, no other project can be added
		bool critical; // if true, place building at any cost
		Sint32 priority; // lower is higher priority
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
	
	bool foodWarning; // true if we are approaching a foodLock
	bool foodLock; // we stop producing any unit until we get more food buildings
	bool foodSurplus; // we have too many food buildings
	Uint32 foodLockStats[2];
	bool overWorkers;
	bool starvingWarning;
	Uint32 starvingWarningStats[2];
	int buildsAmount;
	
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
	Uint8 *spaceForBuildingMap; // where a building of size X*X can be built. included in {0, 1, 2}. More iterations can provide arbitrary size.
	Uint8 *buildingNeighbourMap; // bit 0: bad flag, bits [1, 3]: direct neighbours count, bit 4: zero, bits [5, 7]: far neighbours count.
	
	Uint8 *workPowerMap;
	Uint8 *workRangeMap;
	Uint8 *workAbilityMap;
	Uint8 *hydratationMap;
	Uint8 *notGrassMap;
	Uint8 *wheatGrowthMap;
	Uint8 *oldWheatGradient[4]; // [0] is the most recent
	Uint8 *wheatCareMap[2];
	
	Uint8 *enemyPowerMap;
	Uint8 *enemyRangeMap;
	Uint8 *enemyWarriorsMap;
	
	Uint16 *ressourcesCluster;
};
