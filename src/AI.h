/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

/*
What's in AI ?
AI represents teh behaviour of an artificial intelligence player.
The main methode is Order *getOrder() which return the order to be used by the AI's team.
*/

#include "BuildingType.h"

class Player;
class Order;

/*
Howto make a new AI ?
If you want to build a new way AI behave, you have to:
Add a new Strategy to the below enum.
Add a new if in the getOrder methode.
Add a new case in the load and save methodes.
Fill these methodes correctly.

Warning:
You have to understand Order's mechanism.
Never uses rand() but allways syncRand().
(because the AI need to behave exactly the same on every computers.)
Be sure to return at least a *NullOrder, not NULL.

Idea:
You can access usefull data this way:
player
player->team
player->team->game
player->team->game->map

Fairness:
AI don't have restriction access to hidden part of the map.
You have to check it yourself, please do it.
Please don't uses too much CPU too.
Test games with a lot of AI that for.

Gameplay:
Player and AI may play together, in the same team.
Think if your AI is able to play with another human player ?
*/

class AI
{
public:

	AI(Player *player);

	AI(SDL_RWops *stream, Player *player);
	~AI();
	
	void init(Player *player);
	
	Player *player;
	enum Strategy
	{
		NONE=0,
		SEVEN_PHASES,
		NB_STRATEGY
	};
	Strategy strategy;
	Order *getOrder(void);

	void save(SDL_RWops *stream);
	bool load(SDL_RWops *stream);
	
	// SEVEN_PHASES variables
	int timer;
	int phase;
	int attackPhase;
	int phaseTime;
	int critticalWarriors;
	int critticalTime;
	int attackTimer;
	int mainBuilding[BuildingType::NB_BUILDING];
	int estimateFood(int x, int y);
	int countUnits(void);
	int countUnits(const int medicalState);
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
	Order *mayUpgrade(const int ptrigger, const int ntrigger);
};



#endif

 

