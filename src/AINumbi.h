// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
	
	std::shared_ptr<Order>getOrder(void);
	
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
	std::shared_ptr<Order>swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors);
	void nextMainBuilding(const int buildingType);
	int nbFreeAround(const int buildingType, int posX, int posY, int width, int height);
	bool parseBuildingType(const int buildingType);
	void squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my);
	bool findNewEmplacement(const int buildingType, int *posX, int *posY);
	std::shared_ptr<Order>mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested);
	std::shared_ptr<Order>adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType);
	std::shared_ptr<Order>checkoutExpands(const int numbers, const int workers);
	std::shared_ptr<Order>mayUpgrade(const int ptrigger, const int ntrigger);
};

#endif

 

