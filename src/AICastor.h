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

#ifndef __AI_CASTOR_H
#define __AI_CASTOR_H

#include <list>
#include <string>

#include "BuildingType.h"
#include "AIImplementation.h"

class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

class AICastor : public AIImplementation
{
public:
	class Project
	{
	public:
		Project(BuildingType::BuildingTypeShortNumber shortTypeNum, const char *suffix);
		Project(BuildingType::BuildingTypeShortNumber shortTypeNum, int amount, Sint32 mainWorkers, const char *suffix);
		void init(const char *suffix);

	public:
		BuildingType::BuildingTypeShortNumber shortTypeNum;
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
			int workers;
			int finalWorkers;
			int upgradeBase;
			
			int newOrder;
			int news;
			int newUpgrade;
		};
		
	public:
		Strategy();
	public:
		bool defined;
		
		Sint32 successWait;
		Sint32 isFreePart;
		
		Build build[BuildingType::NB_BUILDING];
		
		Uint32 warTimeTriger;
		Sint32 warLevelTriger;
		Sint32 warAmountTriger;
		
		Uint32 strikeTimeTriger;
		Sint32 strikeWarPowerTrigerUp;
		Sint32 strikeWarPowerTrigerDown;
		
		Sint32 maxAmountGoal;
		
		Uint8 wheatCareLimit;
	};
	
private:
	void firstInit();
public:
	AICastor(Player *player);
	AICastor(SDL_RWops *stream, Player *player, Sint32 versionMinor);
	void init(Player *player);
	~AICastor();

	Player *player;
	Team *team;
	Game *game;
	Map *map;

	bool load(SDL_RWops *stream, Player *player, Sint32 versionMinor);
	void save(SDL_RWops *stream);
	
	Order *getOrder(void);
	
private:
	void defineStrategy();
	
	Order *controlSwarms();
	Order *expandFood();
	Order *controlFood();
	Order *controlUpgrades();
	Order *controlStrikes();
//	Order *controlBaseDefense();
	
	bool addProject(Project *project);
	void addProjects();
	
	void choosePhase();
	
	Order *continueProject(Project *project);
	
	bool enoughFreeWorkers();
	int getFreeWorkers();
	void computeCanSwim();
	void computeNeedSwim();
	void computeBuildingSum();
	void computeWarLevel();
	
	void computeObstacleUnitMap();
	void computeObstacleBuildingMap();
	void computeSpaceForBuildingMap(int max);
	void computeBuildingNeighbourMap(int dw, int dh);
	
	void computeWorkPowerMap();
	void computeWorkRangeMap();
	void computeWorkAbilityMap();
	void computeHydratationMap();
	void computeNotGrassMap();
	void computeWheatCareMap();
	void computeWheatGrowthMap();
	
	void computeEnemyPowerMap();
	void computeEnemyRangeMap();
	
	Order *findGoodBuilding(Sint32 typeNum, bool food, bool defense, bool critical);
	
	void computeRessourcesCluster();
	
public:
	void updateGlobalGradientNoObstacle(Uint8 *gradient);

	std::list<Project *> projects;
	
	Uint32 timer;
	bool canSwim;
	bool needSwim;
	int buildingSum[BuildingType::NB_BUILDING][2]; // [shortTypeNum][isBuildingSite]
	int buildingLevels[BuildingType::NB_BUILDING][2][4]; // [shortTypeNum][isBuildingSite][level]
	int warLevel; // 0: no war
	int warTimeTrigerLevel;
	int warLevelTrigerLevel;
	int warAmountTrigerLevel;
	
	bool onStrike;
	Uint32 strikeTimeTriger;
	bool strikeTeamSelected;
	int strikeTeam;
	
	bool foodLock;
	bool foodSurplus;
	Uint32 foodLockStats[2];
	bool overWorkers;
	bool starvingWarning;
	Uint32 starvingWarningStats[2];
	int buildsAmount;
	int freeWorkers; // plz use getFreeWorkers() to raise computation trigger.
	
	Uint32 lastFreeWorkersComputed;
	Uint32 lastWheatCareMapComputed;
	Uint32 lastEnemyRangeMapComputed;
	Uint32 lastEnemyPowerMapComputed;
	
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
	Uint8 *wheatCareMap;
	
	Uint8 *goodBuildingMap; // TODO: remove.
	
	Uint8 *enemyPowerMap;
	Uint8 *enemyRangeMap;
	
	Uint16 *ressourcesCluster;
	
private:
	FILE *logFile;
};

#endif

 

