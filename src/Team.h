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

#ifndef __TEAM_H
#define __TEAM_H

#include "GAG.h"
#include "Unit.h"
#include "Race.h"
#include "Building.h"
#include <list>
#include "Order.h"

class BaseTeam: public Order
{
public:
	enum TeamType
	{
		T_AI,
		T_HUMAM
	};

	BaseTeam();
	virtual ~BaseTeam(void) { }

	TeamType type;
	Sint32 teamNumber;
	Sint32 numberOfPlayer;
	Uint8 colorR, colorG, colorB, colorPAD;
	Uint32 playersMask;
	Race race;
	
private:
	char data[16];

public:
	void load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	Sint32 checkSum();
};

class Game;

struct TeamStat
{
	int totalUnit;
	int isFree;
	int numberPerType[UnitType::NB_UNIT_TYPE];
	int needFood;
	int needHeal;
	int needNothing;
	int upgradeState[NB_ABILITY][4];

	int totalFood;
	int totalFoodCapacity;
	int totalUnitFoodable;
	int totalUnitFooded;
};

class Team:public BaseTeam
{
public:
	Team(Game *game);
	Team(SDL_RWops *stream, Game *game);

	virtual ~Team(void);

	void setBaseTeam(const BaseTeam *initial);
	void load(SDL_RWops *stream, BuildingsTypes *buildingstypes);
	void save(SDL_RWops *stream);
	
	void step(void);
	
	void setCorrectMasks(void);
	void setCorrectColor(Uint8 r, Uint8 g, Uint8 b);
	void setCorrectColor(float value);

	void computeStat(TeamStat *stats);
	
	Building *findNearestUpgrade(int x, int y, Abilities ability, int actLevel);
	Building *findNearestJob(int x, int y, Abilities ability, int actLevel);
	Building *findNearestAttract(int x, int y, Abilities ability);
	Building *findNearestFillableFood(int x, int y);
	
	Building *findNearestHeal(int x, int y);
	Building *findNearestFood(int x, int y);
	
	Sint32 checkSum();

private:
	void init(void);
	void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v );

public:
	Game *game;
	Unit *myUnits[1024];

	Building *myBuildings[512];

	Bullet *myBullets[256];

	// thoses are the Call Lists :
	std::list<Building *> upgrade[NB_ABILITY];
	std::list<Building *> job[NB_ABILITY];
	std::list<Building *> attract[NB_ABILITY];
	std::list<Building *> canFeedUnit;
	std::list<Building *> canHealUnit;
	std::list<Building *> subscribeForInsideStep;
	std::list<Building *> subscribeForWorkingStep;

	std::list<int> buildingsToBeDestroyed;
	std::list<Building *> buildingsToBeUpgraded;

	std::list<Building *> swarms;
	std::list<Building *> turrets;

	std::list<Building *> virtualBuildings;

	Uint32 allies, enemies, sharedVision, me; //mask

	int startPosX, startPosY;

	// TODO : use a subtil way to allocate UID
	//Sint32 newUnitUID zzz

public:
	int freeUnits;
	bool isAlive;
};

#endif
 
