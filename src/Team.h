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

#ifndef __TEAM_H
#define __TEAM_H

#include "Header.h"
#include "Race.h"
#include <list>
#include "Order.h"
#include "TeamStat.h"

class Building;
class Map;
class Unit;

class BaseTeam: public Order
{
public:
	enum TeamType
	{
		T_HUMAN,
		T_AI,
		// Note : T_AI + n is AI type n
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
	Uint8 data[16];

public:
	bool load(SDL_RWops *stream, Sint32 versionMinor);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	Uint8 *getData();
	bool setData(const Uint8 *data, int dataLength);
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
		BUILDING_UNDER_ATTACK_EVENT,
		BUILDING_FINISHED_EVENT,
		UNIT_CONVERTED_LOST,
		UNIT_CONVERTED_ACQUIERED,
		EVENT_TYPE_SIZE
	};
public:
	Team(Game *game);
	Team(SDL_RWops *stream, Game *game, Sint32 versionMinor);

	virtual ~Team(void);

	void setBaseTeam(const BaseTeam *initial, bool overwriteAfterbase);
	bool load(SDL_RWops *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor);
	void update();
	void save(SDL_RWops *stream);
	
	//! Used by MapRandomGenerator to fill correctly the list usually filled by load(stream).
	void createLists(void);

	//! Used to clear all call lists,
	void clearLists(void);
	//! Used to clear all buildings and units of this team on the map
	void clearMap(void);
	//! Used to delete buildings
	void clearMem(void);

	//! Check some available integrity constraints
	void integrity(void);
	
	//! Do a step for each unit, building and bullet in team.
	void syncStep(void);

	//! The team is now under attack or a building is finished, push event
	void setEvent(int posX, int posY, EventType newEvent, Sint32 id) { if (eventCooldown[newEvent]==0)  { isEvent[newEvent]=true; eventPosX=posX; eventPosY=posY; eventId=id; } eventCooldown[newEvent]=50; }
	//! was an event last tick
	bool wasEvent(EventType type) { bool isEv=isEvent[type]; isEvent[type]=false; return isEv; }
	//! return event position
	void getEventPos(int *posX, int *posY) { *posX=eventPosX; *posY=eventPosY; }
	//! return event id (int associated with the event)
	Sint32 getEventId(void) { return eventId; }

	void setCorrectMasks(void);
	void setCorrectColor(Uint8 r, Uint8 g, Uint8 b);
	void setCorrectColor(float value);
	inline static Uint32 teamNumberToMask(int team) { return 1<<team; }
	
	bool openMarket();
	
	//! Called when unit is hungry
	Building *findNearestFood(Unit *unit);
	//! Called when unit needs heal
	Building *findNearestHeal(Unit *unit);
	
	//! The 3 next methodes are called by an Unit, in order to find the best work for her.
	Building *findBestFoodable(Unit *unit);
	Building *findBestFillable(Unit *unit);
	Building *findBestZonable(Unit *unit);

	Building *findBestUpgrade(Unit *unit);

	//! Return the maximum build level (need at least 1 unit of this level)
	int maxBuildLevel(void);

	// Pathfinding related methods:
	void computeForbiddenArea();
	void dirtyGlobalGradient();
	
	//! Compute team checksum
	Sint32 checkSum(std::list<Uint32> *checkSumsList=NULL);
	
	//! Return the name of the first player in the team
	const char *getFirstPlayerName(void);
	
private:
	void init(void);

public:
	// game is the basic (structural) pointer. Map is used for direct access.
	Game *game;
	Map *map;
	
	Unit *myUnits[1024];
	
	Building *myBuildings[1024];
	
	// thoses where the 4 "call-lists" :
	std::list<Building *> foodable;
	std::list<Building *> fillable;
	std::list<Building *> zonable[NB_UNIT_TYPE];
	std::list<Building *> upgrade[NB_ABILITY];
	
	// The list of building which have one specific ability.
	std::list<Building *> canFeedUnit; // The buildings with not enough food are not in this list.
	std::list<Building *> canHealUnit;
	std::list<Building *> canExchange;
	
	// The lists of building with new subscribed unit:
	std::list<Building *> subscribeForInside;
	std::list<Building *> subscribeToBringRessources;
	std::list<Building *> subscribeForFlaging;

	// The lists of building which needs specials updates:
	std::list<Building *> buildingsWaitingForDestruction;
	std::list<Building *> buildingsToBeDestroyed;
	std::list<Building *> buildingsTryToBuildingSiteRoom;

	// The lists of building which needs specials steps() to be called.
	std::list<Building *> swarms;
	std::list<Building *> turrets;

	std::list<Building *> virtualBuildings;

	Uint32 allies; // Who do I thrust and don't fire on. mask
	Uint32 enemies; // Who I don't thrust and fire on. mask
	Uint32 sharedVisionExchange; // Who does I share the vision of Exchange building to. mask
	Uint32 sharedVisionFood; // Who does I share the vision of Food building to. mask
	Uint32 sharedVisionOther; // Who does I share the vision to. mask
	Uint32 me; // Who am I. mask.

	int startPosX, startPosY;
	int prestige;

	// TODO : use a subtil way to allocate UID
	//Sint32 newUnitUID zzz

private:
	//! was an event last tick
	bool isEvent[EVENT_TYPE_SIZE];
	//! prevent event overflow
	int eventCooldown[EVENT_TYPE_SIZE];
	//! event position
	int eventPosX, eventPosY;
	//! event id
	Sint32 eventId;

public:
	//! If you try to build buildings in the ennemy territory, you will be prevented to build any new buildings for a given time.
	//! This is the time left you can't build for. time in ticks.
	int noMoreBuildingSitesCountdown;
	static const int noMoreBuildingSitesCountdownMax=200; // We set 5s as default
	bool isAlive;
	//! called by game, set to true if all others team has won or if script has forced
	bool hasWon;
	//! the stat for this team. It is computed every step, so it is always updated.
	// TeamStat latestStat; this has been moved to *stats.getLatestStat();
	TeamStats stats;

protected:
	FILE *logFile;
};

#endif

