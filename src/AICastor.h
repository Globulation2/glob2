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
		Project(BuildingType::BuildingTypeShortNumber shortTypeNum, const char* debugName)
		{
			this->shortTypeNum=shortTypeNum;
			this->debugName=debugName;
			init();
		}
		Project(BuildingType::BuildingTypeShortNumber shortTypeNum, Sint32 mainWorkers, const char* debugName)
		{
			this->shortTypeNum=shortTypeNum;
			this->debugName=debugName;
			init();
			this->mainWorkers=mainWorkers;
		}
		void init()
		{
			printf("new project(%s)\n", debugName);
			subPhase=0;;
			
			blocking=true;
			critical=false;
			food=false;
			
			mainWorkers=-1;
			foodWorkers=-1;
			otherWorkers=-1;
			
			multipleStart=false;
			waitFinished=false;
			finalWorkers=-1;
			
			finished=false;
			
			timer=(Uint32)-1;
		}
		
		BuildingType::BuildingTypeShortNumber shortTypeNum;
		const char *debugName;
		
		int subPhase;
		
		bool blocking;
		bool critical;
		bool food;
		
		Sint32 mainWorkers;
		Sint32 foodWorkers;
		Sint32 otherWorkers;
		
		bool multipleStart;
		bool waitFinished;
		Sint32 finalWorkers;
		
		bool finished;
		
		Uint32 timer;
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
	Order *controlSwarms(void);
	Order *expandFood(void);
	
	void addProjects(void);
	
	void choosePhase();
	
	Order *continueProject(Project *project);
	
	int getFreeWorkers();
	
	void computeCanSwim();
	void computeNeedPool();
	
	void computeObstacleUnitMap();
	void computeObstacleBuildingMap();
	void computeSpaceForBuildingMap(int max);
	void computeBuildingNeighbourMap(int dw, int dh);
	void computeTwoSpaceNeighbourMap();
	
	void computeWorkPowerMap();
	void computeWorkRangeMap();
	void computeWorkAbilityMap();
	void computeHydratationMap();
	void computeWheatGrowthMap(int dw, int dh);
	
	Order *findGoodBuilding(Sint32 typeNum, bool food, bool critical);
	
	void computeRessourcesCluster();
	
public:
	void updateGlobalGradientNoObstacle(Uint8 *gradient);

	std::list<Project *> projects;
	
	Uint32 timer;
	bool canSwim;
	bool needPool;
	bool onCritical;
	Uint32 lastNeedPoolComputed;
	Uint32 computeNeedPoolTimer;
	Uint32 controlSwarmsTimer;
	Uint32 expandFoodTimer;
	
	bool hydratationMapComputed;
	
public:
	Uint8 *obstacleUnitMap; // where units can go. included in {0, 1}
	Uint8 *obstacleBuildingMap; // where buildings can be built. included in {0, 1}
	Uint8 *spaceForBuildingMap; // where building can be built, of size X*X. included in {0, 1, 2}. More iterations can provide arbitrary size.
	Uint8 *buildingNeighbourMap; // bit 0: bad flag, bits [1, 2]: direct neighbours count, bit 4: zero, bits [5, 7]; far neighbours count.
	
	Uint8 *twoSpaceNeighbourMap; // TODO: remove.
	
	Uint8 *workPowerMap;
	Uint8 *workRangeMap;
	Uint8 *workAbilityMap;
	Uint8 *hydratationMap;
	Uint8 *wheatGrowthMap;
	Uint8 *wheatCareMap;
	
	Uint8 *goodBuildingMap; // TODO: remove.
	
	Uint16 *ressourcesCluster;
	
private:
	FILE *logFile;
};

#endif

 

