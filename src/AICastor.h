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
	
	Order *phaseAlpha();

	void computeCanSwim();
	
	void computeObstacleUnitMap();
	void computeObstacleBuildingMap();
	void computeSpaceForBuildingMap(int max);
	void computeBuildingNeighbourMap();
	void computeTwoSpaceNeighbourMap();
	
	void computeWorkPowerMap();
	void computeWorkRangeMap();
	void computeWorkAbilityMap();
	void computeHydratationMap();
	void computeWheatGrowthMap();
	
	Order *findGoodFoodBuilding();
	Order *findBestFoodBuilding();
	
	void computeRessourcesCluster();
	
public:
	void updateGlobalGradientNoObstacle(Uint8 *gradient);
	
private:
	enum PhaseType
	{
		P_NONE=0,
		P_ALPHA=1,
		P_END
	};
	
	PhaseType phase;
	int subPhase;
	int scheduler;
	
	Uint32 timer;
	bool hydratationMapComputed;
	
public:
	bool canSwim;
	
	Uint8 *obstacleUnitMap; // where units can go. included in {0, 1}
	Uint8 *obstacleBuildingMap; // where buildings can be built. included in {0, 1}
	Uint8 *spaceForBuildingMap; // where building can be built, of size X*X. included in {0, 1, 2}. More iterations can provide arbitrary size.
	Uint8 *buildingNeighbourMap; // where you can build with exactly one neighbour. Bit 0 means bad place. Bit 1 to 7 is the sum of the neighbours.
	Uint8 *twoSpaceNeighbourMap; // where you can build at a distance of 2 of another obstacle for building.
	
	Uint8 *workPowerMap;
	Uint8 *workRangeMap;
	Uint8 *workAbilityMap;
	Uint8 *hydratationMap;
	Uint8 *wheatGrowthMap;
	
	Uint8 *goodFoodBuildingMap; // TODO: remove.
	
	Uint16 *ressourcesCluster;
	
private:
	FILE *logFile;
};

#endif

 

