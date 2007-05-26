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

#ifndef __TEAM_H
#define __TEAM_H

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_rwops.h>
#else
#include <Types.h>
#endif

#include <list>
#include <algorithm>
#include <queue>

#include "Race.h"
#include "TeamStat.h"
#include "GameEvent.h"

#include <boost/shared_ptr.hpp>

class Building;
class BuildingsTypes;
class Map;
class Unit;

class BaseTeam
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
	Sint32 teamNumber; // index of the current team in the game::teams[] array.
	Sint32 numberOfPlayer; // number of controling players
	Uint8 colorR, colorG, colorB, colorPAD;
	Uint32 playersMask;
	Race race;
	
public:
	bool disableRecursiveDestruction;
	
private:
	Uint8 data[16];

public:
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	Uint8 getOrderType();
	Uint8 *getData();
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength();
	Uint32 checkSum();
};

class Game;

class Team:public BaseTeam
{
	static const bool verbose = false;
public:
	Team(Game *game);
	Team(GAGCore::InputStream *stream, Game *game, Sint32 versionMinor);

	virtual ~Team(void);

	void setBaseTeam(const BaseTeam *initial);
	bool load(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	//! Used by MapRandomGenerator to fill correctly the list usually filled by load(stream).
	void createLists(void);

	//! Used to clear all call lists,
	void clearLists(void);
	//! Used to clear all buildings and units of this team on the map
	void clearMap(void);
	void clearMem(void);

	//! Check some available integrity constraints
	void integrity(void);
	
	//! remove the building from all lists not realated to the upgrade/destroying systems
	void removeFromAbilitiesLists(Building *building);
	//! add the building from all lists not realated to the upgrade/destroying systems
	void addToStaticAbilitiesLists(Building *building);
	
	//! Do a step for each unit, building and bullet in team.
	void syncStep(void);
	//! Check if there is still players controlling this team, if not, it is dead
	void checkControllingPlayers(void);

	///Push a new game event into the queue
	void pushGameEvent(boost::shared_ptr<GameEvent> event);
	
	///Return the top-most event from the queue and remove it
	boost::shared_ptr<GameEvent> getEvent();
	
	///This returns whether an event of the given type had occurred on the last tick
	bool wasRecentEvent(GameEventType type);
	
	///Updates the list of events. This automatically clears events that get too old,
	///and decrements the cooldown timers for each event type
	void updateEvents();

	void setCorrectMasks(void);
	void setCorrectColor(Uint8 r, Uint8 g, Uint8 b);
	void setCorrectColor(float value);
	inline static Uint32 teamNumberToMask(int team) { return 1<<team; }
	
	void update();
	bool openMarket();
	
	Building *findNearestHeal(Unit *unit);
	Building *findNearestFood(Unit *unit);
	Building *findBestUpgrade(Unit *unit);

	///This function decides whether the lhs building is a higher priority
	///than the rhs building and returns true.
	static bool prioritize_building(Building* lhs, Building* rhs);
	///This function adds the given building to the needing-unit call lists
	void add_building_needing_work(Building* b);
	///This function removes the given building from the needing-unit call lists
	void remove_building_needing_work(Building* b);
	///This function updates all of the buildings in order of highest priority to lowest
	void updateAllBuildingTasks();

	//! Return the maximum build level (need at least 1 unit of this level)
	int maxBuildLevel(void);

	// Pathfinding related methods:
	void computeForbiddenArea();
	void dirtyGlobalGradient();
	void dirtyWarFlagGradient();
	
	//! Compute team checksum
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector=NULL, std::vector<Uint32> *checkSumsVectorForBuildings=NULL, std::vector<Uint32> *checkSumsVectorForUnits=NULL);
	
	//! Return the name of the first player in the team
	const char *getFirstPlayerName(void) const;
	
private:
	void init(void);

public:
	// game is the basic (structural) pointer. Map is used for direct access.
	Game *game;
	Map *map;
	
	Unit *myUnits[1024];
	
	Building *myBuildings[1024]; //That's right, you can't build two walls all the way across a 512x512 map.

	///This stores the buildings that need units. They are sorted based on priority.
	std::vector<Building*> buildingsNeedingUnits;

	// thoses where the 4 "call-lists" (lists of flags or buildings for units to work on/in) :
	std::list<Building *> upgrade[NB_ABILITY]; //to upgrade the units' abilities.
	
	// The list of building which have one specific ability.
	std::list<Building *> canFeedUnit; // The buildings with not enough food are not in this list.
	std::list<Building *> canHealUnit;
	std::list<Building *> canExchange;

	// The lists of building which needs specials updates:
	std::list<Building *> buildingsWaitingForDestruction;
	std::list<Building *> buildingsToBeDestroyed;
	std::list<Building *> buildingsTryToBuildingSiteRoom;

	// The lists of building which needs specials steps() to be called.
	std::list<Building *> swarms; // Have to increments "productionTimeout" every step, and maybe produce an unit.
	std::list<Building *> turrets; // Waiting for "shootingCooldown" or "enemy unit" events to shoot.
	std::list<Building *> clearingFlags; // Have to update the clearing gradient time to time, and request worker if there is something to clear.

	std::list<Building *> virtualBuildings;

	Uint32 allies; // Who do I trust and don't fire on. mask
	Uint32 enemies; // Who I don't trust and fire on. mask
	Uint32 sharedVisionExchange; // Who does I share the vision of Exchange building to. mask
	Uint32 sharedVisionFood; // Who does I share the vision of Food building to. mask
	Uint32 sharedVisionOther; // Who does I share the vision to. mask
	Uint32 me; // Who am I. mask.

	Sint32 startPosX, startPosY;
	Sint32 startPosSet; // {0=unset, 1=any unit, 2=any building, 3=swarm building}
	Sint32 prestige;
	
	// Number of unit lost due to conversion
	Sint32 unitConversionLost;
	// Number of unit gained due to conversion
	Sint32 unitConversionGained;

	/// The amount of ressources the team has globally, for markets
	Sint32 teamRessources[MAX_NB_RESSOURCES];

private:
	///Queue of game events
	std::queue<boost::shared_ptr<GameEvent> > events;
	///These timers indicate the cooldown for a particular event type,
	///This keeps too many events from being pumped at once. If the
	///timer isn't at 0 when a new event is recieved, the new event
	///is ignored.
	Uint8 eventCooldownTimers[GESize];
	
	
public:
	//! If you try to build buildings in the ennemy territory, you will be prevented to build any new buildings for a given time.
	//! This is the time left you can't build for. time in ticks.
	int noMoreBuildingSitesCountdown;
	static const int noMoreBuildingSitesCountdownMax=200; // We set 5s as default
	bool isAlive;
	//! called by game, set to true if all others team has lost or if script has forced
	bool hasWon;
	//! the stat for this team. It is computed every step, so it is always updated.
	// TeamStat latestStat; this has been moved to *stats.getLatestStat();
	TeamStats stats;

protected:
	FILE *logFile;
};

#endif

