/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/


#ifndef __AI_H
#define __AI_H

#include "GAG.h"
#include "Order.h"

class Player;

class AI

{

public:

	AI(Player *player);

	AI(SDL_RWops *stream, Player *player);
	void init(Player *player);
	
	Player *player;
	int timer;
	int phase;
	int attackPhase;
	int phaseTime;
	int critticalWarriors;
	int critticalTime;
	int attackTimer;
	int mainBuilding[BuildingType::NB_BUILDING];
	enum Strategy
	{
		NONE=0,
		SEVEN_PHASES,
		NB_STRATEGY
	};
	Strategy strategy;
	// get order from AI, return NullOrder if

	int estimateFood(int x, int y);
	int countUnits(void);
	Order *swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors);
	void nextMainBuilding(const int buildingType);
	bool checkUIDRoomForBuilding(int px, int py, int width, int height);
	int nbFreeAround(const int buildingType, int posX, int posY, int width, int height);
	bool parseBuildingType(const int buildingType);
	void squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my);
	bool findNewEmplacement(const int buildingType, int *posX, int *posY);
	Order *mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested);
	Order *adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType);
	Order *checkoutExpands(const int numbers, const int workers);
	
	Order *getOrder(void);


	void save(SDL_RWops *stream);

	void load(SDL_RWops *stream);

};



#endif

 

