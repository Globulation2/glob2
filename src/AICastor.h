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
	void choosePhase();
	
	Order *phaseBasic(BuildingType::BuildingTypeShortNumber shortTypeNum, bool food, Sint32 mainWorkers, Sint32 foodWorkers, Sint32 otherWorkers, bool multipleStart);
	//Order *phaseAlpha(); // get any food building
	//Order *phaseBeta(); // get any swarm
	//Order *phaseGamma(); // get some of the best food building aviable
	
	
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
	
	Order *findGoodBuilding(Sint32 typeNum, bool food, int higherQuality);
	Order *findBestBuilding(Sint32 typeNum, bool food);
	
	void computeRessourcesCluster();
	
public:
	void updateGlobalGradientNoObstacle(Uint8 *gradient);
	
private:
	enum PhaseType
	{
		P_NONE=0,
		P_ALPHA=1,
		P_BETA=2,
		P_GAMMA=3,
		P_END
	};
	
	PhaseType phase;
	int subPhase;
	int scheduler;
	
	Uint32 timer;
	bool canSwim;
	bool needPool;
	Uint32 lastNeedPoolComputed;
	
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
	
	Uint8 *goodBuildingMap; // TODO: remove.
	
	Uint16 *ressourcesCluster;
	
private:
	FILE *logFile;
};

#endif

 

