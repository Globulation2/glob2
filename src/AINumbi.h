/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __AI_NUMBI_H
#define __AI_NUMBI_H

#include "BuildingType.h"
#include "AIImplementation.h"

class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

class AINumbi : public AIImplementation
{
public:
	AINumbi(Player *player);
	AINumbi(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AINumbi();

	Player *player;
	Team *team;
	Game *game;
	Map *map;
	
	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	Order *getOrder(void);
	
private:
	int timer;
	int phase;
	int attackPhase;
	int phaseTime;
	int critticalWarriors;
	int critticalTime;
	int attackTimer;
	int mainBuilding[15]; //BuildingType::NB_BUILDING=15 with lover versions
	void init(Player *player);
	int estimateFood(Building *building);
	int countUnits(void);
	int countUnits(const int medicalState);
	Order *swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors);
	void nextMainBuilding(const int buildingType);
	int nbFreeAround(const int buildingType, int posX, int posY, int width, int height);
	bool parseBuildingType(const int buildingType);
	void squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my);
	bool findNewEmplacement(const int buildingType, int *posX, int *posY);
	Order *mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested);
	Order *adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType);
	Order *checkoutExpands(const int numbers, const int workers);
	Order *mayUpgrade(const int ptrigger, const int ntrigger);
};

#endif

 

