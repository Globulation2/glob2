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

#ifndef __TEAM_H
#define __TEAM_H

#include "GAG.h"
#include "Unit.h"
#include "Race.h"
#include "Building.h"
#include <list>
#include "Order.h"
#include "TeamStat.h"

class BaseTeam: public Order
{
public:
	enum TeamType
	{
		T_AI,
		T_HUMAN
	};

	BaseTeam();
	virtual ~BaseTeam(void) { }

	TeamType type;
	Sint32 teamNumber;
	Sint32 numberOfPlayer;
	Uint8 colorR, colorG, colorB, colorPAD;
	Uint32 playersMask;
	Race race;
	
public:
	bool disableRecursiveDestruction;
	
private:
	char data[16];

public:
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	Sint32 checkSum();
};

class Game;

class Team:public BaseTeam
{
public:
	enum EventType
	{
		UNIT_UNDER_ATTACK_EVENT=0,
		BUILDING_UNDER_ATTACK_EVENT=1,
		BUILDING_FINISHED_EVENT=2,
		EVENT_TYPE_SIZE=3
	};
public:
	Team(Game *game);
	Team(SDL_RWops *stream, Game *game);

	virtual ~Team(void);

	void setBaseTeam(const BaseTeam *initial, bool overwriteAfterbase);
	bool load(SDL_RWops *stream, BuildingsTypes *buildingstypes);
	void save(SDL_RWops *stream);
	
	//! Used by MapRandomGenerator to fill correctly the list usually filled by load(stream).
	void createLists(void);

	//! Do a step for each unit, building and bullet in team.
	void step(void);

	//! The team is now under attack or a building is finished, push event
	void setEvent(int posX, int posY, EventType newEvent) { if (eventCooldown[newEvent]==0)  { isEvent[newEvent]=true; eventPosX=posX; eventPosY=posY; } eventCooldown[newEvent]=50; }
	//! was an event last tick
	bool wasEvent(EventType type) { bool isEv=isEvent[type]; isEvent[type]=false; return isEv; }
	//! return event position
	void getEventPos(int *posX, int *posY) { *posX=eventPosX; *posY=eventPosY; }

	void setCorrectMasks(void);
	void setCorrectColor(Uint8 r, Uint8 g, Uint8 b);
	void setCorrectColor(float value);

	//! Called when unit wanna work to building. Distance is balanced with user's number of requested unit
	Building *findNearestJob(int x, int y, Abilities ability, int actLevel);
	Building *findBestConstruction(Unit *unit);
	//! Called when unit wanna be attracted to building. Distance is balanced with user's number of requested unit
	Building *findNearestAttract(int x, int y, Abilities ability);
	//! Called when unit wanna work to fill building with food. Distance is balanced with user's number of requested unit
	Building *findNearestFillableFood(int x, int y);

	//! Called when unit want upgrade a certain ability
	Building *findNearestUpgrade(int x, int y, Abilities ability, int actLevel);
	//! Called when unit needs heal
	Building *findNearestHeal(int x, int y);
	//! Called when unit is hungry
	Building *findNearestFood(int x, int y);

	//! Return the maximum build level (need at least 1 unit of this level)
	int maxBuildLevel(void);

	//! Compute team checksum
	Sint32 checkSum();

private:
	void init(void);

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

private:
	//! was an event last tick
	bool isEvent[EVENT_TYPE_SIZE];
	//! prevent event overflow
	int eventCooldown[EVENT_TYPE_SIZE];
	//! event position
	int eventPosX, eventPosY;

public:
	bool isAlive;
	//! called by game, set to true if all others team has won or if script has forced
	bool hasWon;
	//! the stat for this team. It is computed every step, so it is always updated.
	TeamStat latestStat;
};

#endif

