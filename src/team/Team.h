// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL_rwops.h>

#include <climits>
#include <list>
#include <algorithm>
#include <queue>

#include "Race.h"
#include "TeamStat.h"
#include "GameEvent.h"

#include <memory>
#include <optional>

#include "BaseTeam.h"
#include "WinningConditions.h"

class Building;
class BuildingsTypes;
class Map;
class Unit;

class Game;

class Team:public BaseTeam
{
public:
	//! In-memory cap on simultaneous teams and players. The engine has never
	//! been tested above this — see `docs/replay-verification.md`.
	static const int MAX_COUNT=12;

	//! Slot count of the GameHeader player/ally arrays. Only the first
	//! MAX_COUNT are populated; the rest are padding that keeps the file
	//! format byte-identical. Changing it requires a format version bump.
	static const int MAX_COUNT_ON_DISK=32;

	//! "No candidate found yet" score in findBestUpgrade; every real score is lower.
	static constexpr Sint32 UPGRADE_SCORE_NONE = INT32_MAX;

	//! HSV saturation/value for default team colours derived from a hue.
	static constexpr float TEAM_COLOR_SATURATION = 0.8f;
	static constexpr float TEAM_COLOR_VALUE = 0.9f;
	static constexpr float COLOR_CHANNEL_MAX = 255.0f;

	//! Where startPosX/Y came from. A source only overwrites a lower one.
	enum StartPosSource : Sint32
	{
		START_POS_UNSET = 0,
		START_POS_FROM_UNIT = 1,
		START_POS_FROM_BUILDING = 2,
		START_POS_FROM_SWARM = 3,
	};

	Team(Game *game);
	Team(GAGCore::InputStream *stream, Game *game, Sint32 versionMinor);

	virtual ~Team(void);

	void setBaseTeam(const BaseTeam *initial);
	bool load(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);

	//! Rebuild the per-building lists from myBuildings (map generators, in place of load()).
	void createLists(void);
	void clearLists(void);
	//! Remove every unit and building of this team from the map grid.
	void clearMap(void);
	//! Delete every unit and building of this team.
	void clearMem(void);

	bool integrity(void);

	//! Building lists indexed by what the building offers (canUpgrade, canFeedUnit, ...).
	void removeFromAbilitiesLists(Building *building);
	void addToStaticAbilitiesLists(Building *building);

	//! Step every unit, building and bullet of the team.
	void syncStep(void);
	//! A team with no controlling player left is dead.
	void checkControllingPlayers(void);

	void pushGameEvent(GameEvent event);
	//! Pop the oldest event, if any.
	std::optional<GameEvent> getEvent();
	//! Whether an event of this type occurred on the last tick.
	bool wasRecentEvent(GameEventType type);
	//! Drop stale events and tick down the per-type cooldowns.
	void updateEvents();

	void setCorrectMasks(void);
	void setCorrectColor(const GAGCore::Color& color);
	void setCorrectColor(float value);
	/// Bit for `team` in team-mask bitfields (allies, sharedVision*, Case::forbidden, ...).
	/// Masks have MAX_COUNT_ON_DISK bits; `1<<31` on a signed int would be UB.
	inline static Uint32 teamNumberToMask(int team) { return Uint32(1)<<team; }

	void update();
	//! Whether any other team shares its market vision with us.
	bool openMarket();

	Building *findNearestHeal(Unit *unit);
	Building *findNearestFood(Unit *unit);
	Building *findBestUpgrade(Unit *unit);

	//! Strict ordering of buildings competing for units: true if lhs should be served first.
	static bool buildingHasHigherPriority(Building* lhs, Building* rhs);
	void addBuildingNeedingWork(Building* b, Sint32 priority);
	void removeBuildingNeedingWork(Building* b, Sint32 priority);
	//! Update every building in buildingsNeedingUnits, highest priority first.
	void updateAllBuildingTasks();

	//! Highest build level any unit of the team has.
	int maxBuildLevel(void);

	// Pathfinding
	void computeForbiddenArea();
	void dirtyGlobalGradient();
	void dirtyWarFlagGradient();

	Uint32 checkSum(std::vector<Uint32> *checkSumsVector=NULL, std::vector<Uint32> *checkSumsVectorForBuildings=NULL, std::vector<Uint32> *checkSumsVectorForUnits=NULL);

	//! Name of the first human/AI player on this team, or "" if none controls it.
	//! Locale-agnostic; UI wanting the localized "[Uncontrolled]" placeholder
	//! must use displayPlayerName() (gui/TeamDisplay.h).
	std::string getFirstPlayerName(void) const;

	//! Evaluate all win conditions and update hasWon, hasLost and winCondition.
	void checkWinConditions();

private:
	void init(void);

public:
	Game *game;
	Map *map;

	Unit **myUnits;

	Building **myBuildings;

	//! Buildings that want units, keyed by priority, highest first.
	std::map<int, std::vector<Building*>, std::greater<int> > buildingsNeedingUnits;

	// Buildings offering a service to units, by service.
	std::list<Building *> canUpgrade[NB_ABILITY];
	std::list<Building *> canFeedUnit; // excludes buildings that are out of food
	std::list<Building *> canHealUnit;
	std::list<Building *> canExchange;

	// Buildings in a transitional state.
	std::list<Building *> buildingsWaitingForDestruction;
	std::list<Building *> buildingsToBeDestroyed;
	std::list<Building *> buildingsTryToBuildingSiteRoom;

	// Buildings that need their own step() every tick.
	std::list<Building *> swarms; // unit production
	std::list<Building *> turrets; // shooting
	std::list<Building *> clearingFlags; // clearing-gradient refresh and worker requests

	std::list<Building *> virtualBuildings;

	// Team masks (see teamNumberToMask).
	Uint32 allies; // teams we never fire on
	Uint32 enemies; // teams we fire on
	Uint32 sharedVisionExchange; // teams that see our markets
	Uint32 sharedVisionFood; // teams that see our food buildings
	Uint32 sharedVisionOther; // teams that see everything else of ours
	Uint32 me; // this team's own bit

	Sint32 startPosX, startPosY;
	Sint32 startPosSet; // a StartPosSource
	Sint32 prestige;

	Sint32 unitConversionLost;
	Sint32 unitConversionGained;

	/// Team-wide resource totals, for markets.
	Sint32 teamRessources[MAX_NB_RESSOURCES];

private:
	std::queue<GameEvent> events;
	/// While the timer for a type is non-zero, new events of that type are dropped.
	Uint8 eventCooldownTimers[GESize];

public:
	Race race;
	//! Ticks left during which the team may not place building sites,
	//! after trying to build in enemy territory.
	int noMoreBuildingSitesCountdown;
	static const int noMoreBuildingSitesCountdownMax=200; // 5 s at 40 ticks/s
	bool isAlive;
	bool hasWon;
	bool hasLost;
	/// Condition that made the team win or lose.
	WinningConditionType winCondition;

	//! Recomputed every step.
	TeamStats stats;
};
