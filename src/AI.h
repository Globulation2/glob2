/*

 * Globulation 2 AI support

 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon

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

 

