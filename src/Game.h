// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <iostream>
#include <memory>

#include "Map.h"
#include "SGSL.h"
#include <string>
#include <valarray>
#include "MapHeader.h"
#include "GameHeader.h"
#include "GameObjectives.h"
#include "GameHints.h"
#include "MapScript.h"
#include "BuildingGuiState.h"

namespace GAGCore
{
	class DrawableSurface;
	class InputStream;
	class OutputStream;
}
using namespace GAGCore;
class MapGenerationDescriptor;
class GameGUI;
class MapEdit;

class OrderCreate;
class OrderModifyBuilding;
class OrderModifyExchange;
class OrderModifyFlag;
class OrderModifyClearingFlag;
class OrderModifyMinLevelToFlag;
class OrderMoveFlag;
class OrderAlterateForbidden;
class OrderAlterateGuardArea;
class OrderAlterateClearArea;
class OrderModifySwarm;
class OrderDelete;
class OrderChangePriority;
class OrderCancelDelete;
class OrderConstruction;
class SetAllianceOrder;
class PlayerQuitsGameOrder;
#ifndef YOG_SERVER_ONLY
class GameAnimations;
#endif  // !YOG_SERVER_ONLY

// Minimum value of the prestige-victory threshold.
#define MIN_MAX_PRESTIGE 500
// Each team contributes this much to the prestige-victory threshold.
#define TEAM_MAX_PRESTIGE 150

// === Game-loop sentinels (cross-slice) ===

//! Returned by winner-tracking code to mean "no team has won yet". Used by
//! EngineRun.cpp:301 against winnerTeam.
static constexpr int WINNER_TEAM_NONE = -1;

//! Default `pos` argument to Game::addTeam / removeTeam meaning "append
//! at the end of the team list" (Game.cpp:171; Game_editor.cpp:102, 124).
//! Distinct from any other -1 sentinel — see glossary §2.
static constexpr int TEAM_POS_END = -1;

//! Length of the rolling tick-time profile buffer in Game::ticksGameSum.
//! Indexed by `stepCounter & 31`, not by team id (bug #11). Naming this
//! separately documents the actual meaning — it is unrelated to team count.
static constexpr int TICK_PROFILE_BUF_LEN = 32;

//! Fog-of-war switch cadence: every (mask + 1) ticks, perform the FOW
//! switch when (stepCounter & FOW_SWITCH_TICK_MASK) == FOW_SWITCH_TICK_PHASE.
//! See Game_sync.cpp:175.
static constexpr int FOW_SWITCH_TICK_MASK  = 31;
static constexpr int FOW_SWITCH_TICK_PHASE = 16;

//! Build-project step cadence: every 16 ticks, run buildProjectSyncStep
//! when (stepCounter & MASK) == PHASE. See Game_sync.cpp:194.
static constexpr int BUILD_PROJECT_TICK_MASK  = 15;
static constexpr int BUILD_PROJECT_TICK_PHASE = 1;

//! World-logic step cadence: every 32 ticks, run the world-logic pass when
//! (stepCounter & MASK) == PHASE. See Game_sync.cpp:197.
static constexpr int WORLD_LOGIC_TICK_MASK  = 31;
static constexpr int WORLD_LOGIC_TICK_PHASE = 0;

//! Upper bound of the (team * Building::MAX_COUNT + buildingId) global id
//! space, equal to Team::MAX_COUNT * Building::MAX_COUNT.
//! Asserted in OrderBuilding.cpp:78, 109, 141, 179, 214; OrderModify.cpp:29;
//! UnitSerialization.cpp:235.
static constexpr int BUILDING_GID_MAX = Team::MAX_COUNT * Building::MAX_COUNT;

//! Number of distinct levels a building can reach (0..MAX_BUILDING_LEVELS-1).
//! Used by Game_editor.cpp:91. Distinct from NB_UNIT_LEVELS.
static constexpr int MAX_BUILDING_LEVELS = 6;

//! Cap on how many workers an OrderModifyBuilding may request for one
//! building. See Game_orders.cpp:140.
static constexpr int MAX_BUILDING_WORKER_REQUEST = 20;

//! Sentinel returned by the linear search for a free unit/building slot
//! in Game::addUnit / Game::addBuilding when every slot is occupied.
//! See Game_editor.cpp:203, 234. Distinct from any other -1 sentinel.
static constexpr int SLOT_INDEX_NONE = -1;

//! Full HSV hue range in degrees, used to spread team colours evenly
//! around the colour wheel: hue = (i * TEAM_COLOR_HUE_DEGREES) / numTeams.
//! See Game_editor.cpp:113, 139.
static constexpr float TEAM_COLOR_HUE_DEGREES = 360.0f;

//! Padding (in tiles) added on each side of the rectangle passed to
//! Map::dirtyLocalGradient when a building/flag changes. The width/height
//! of the dirty rect therefore grows by 2 * GRADIENT_DIRTY_BORDER_TILES.
//! See Game_orders.cpp:193, 279, 360, 496.
static constexpr int GRADIENT_DIRTY_BORDER_TILES = 16;

class Game
{
	static const bool verbose = false;
public:
	///Constructor. GUI can be NULL
	Game(GameGUI *gui, MapEdit* edit=NULL);

	///Clears all memory that Game uses
	virtual ~Game();

	///Loads data from a stream
	bool load(GAGCore::InputStream *stream);

	//! Check some available integrity constraints
	bool integrity(void);

	///Saves data to a stream
	void save(GAGCore::OutputStream *stream, bool fileIsAMap, const std::string& name);

	enum FlagForRemoval
	{
		DEL_BUILDING=0x1,
		DEL_GROUND_UNIT=0x2,
		DEL_AIR_UNIT=0x4,
		DEL_UNIT=0x6,
		DEL_FLAG=0x8
	};

	enum DrawOption
	{
		DRAW_HEALTH_FOOD_BAR = 0x1,
		DRAW_PATH_LINE = 0x2,
		DRAW_BUILDING_RECT = 0x4,
		DRAW_AREA = 0x8,
		DRAW_WHOLE_MAP = 0x10,
		DRAW_ACCESSIBILITY = 0x20,
		DRAW_SCRIPT_AREAS = 0x40,
		DRAW_NO_RESSOURCE_GROWTH_AREAS = 0x80,
		DRAW_OVERLAY = 0x100,
	};

	/// This method will prepare the game with this mapHeader
	void setMapHeader(const MapHeader& mapHeader);

	/// This method will prepare the game with the provided gameHeader,
	/// including initiating the Players
	void setGameHeader(const GameHeader& gameHeader, bool saveAI=false);

	/// Executes an Order with respect to the localPlayer of the GUI. All Orders get processed here.
	void executeOrder(std::shared_ptr<Order> order, int localPlayer);

	/// Makes a step for building projects that are waiting for the areas to clear of units.
	void buildProjectSyncStep(Sint32 localTeam);

	/// Check and update winning conditions
	void wonSyncStep(void);

	/// Advanced the map script and checks conditions
	void scriptSyncStep();

	/// Updates total prestige stats
	void prestigeSyncStep();

	/// Advances the Game by one tick, in reference to localTeam being the localTeam. This does all
	/// internal proccessing.
	void syncStep(Sint32 localTeam);

	void dirtyWarFlagGradient();

	// Script interface
	int teamsCount() { return mapHeader.getNumberOfTeams(); }
	int isTeamAlive(int team);
	int unitsCount(int team, int type);
	int buildingsCount(int team, int type, int level);
	int unitsUpgradesCount(int team, int type, int ability, int level);
	

	// Editor stuff
	// add & remove teams, used by the map editor and the random map generator
	void addTeam(int pos=TEAM_POS_END);
	void removeTeam(int pos=TEAM_POS_END);
	//! If a team is uncontrolled (playerMask == 0), remove units and buildings from map
	void clearingUncontrolledTeams(void);
	void regenerateDiscoveryMap(void);

	//void addUnit(int x, int y, int team, int type, int level);
	Unit *addUnit(int x, int y, int team, int type, int level, int delta, int dx, int dy);
	Building *addBuilding(int x, int y, int typeNum, int teamNumber, Sint32 unitWorking = 1, Sint32 unitWorkingFuture = 1);
	//! This remove anything at case(x, y), and return a rect which include every removed things.
	bool removeUnitAndBuildingAndFlags(int x, int y, unsigned flags=DEL_UNIT|DEL_BUILDING|DEL_FLAG);
	bool removeUnitAndBuildingAndFlags(int x, int y, int size, unsigned flags=DEL_UNIT|DEL_BUILDING|DEL_FLAG);
	///A convenience function, returns a pointer to the unit with the guid, or NULL otherwise
	Unit* getUnit(int guid);

	bool checkRoomForBuilding(int mousePosX, int mousePosY, const BuildingType *bt, int *buildingPosX, int *buildingPosY, int teamNumber, bool checkFow=true);
	bool checkRoomForBuilding(int x, int y, const BuildingType *bt, int teamNumber, bool checkFow=true);
	bool checkHardRoomForBuilding(int coordX, int coordY, const BuildingType *bt, int *mapX, int *mapY);
	bool checkHardRoomForBuilding(int x, int y, const BuildingType *bt);

	void drawUnit(int x, int y, Uint16 gid, int viewportX, int viewportY, int screenW, int screenH, int localTeam, Uint32 drawOptions);
	/// `buildingGuiState` (optional) provides per-flag pending positions so
	/// drag-targets render before the move-flag order has executed. The map
	/// editor passes nullptr — it mutates buildings directly without an
	/// orderQueue, so there is no pending shadow to consult.
	void drawMap(int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX, int viewportY, int teamSelected, Uint32 drawOptions = 0, std::set<Building*> *visibleBuildings = 0, const BuildingGuiStateMap* buildingGuiState = nullptr);

	///Sets the mask respresenting which players the game is waiting on
	void setWaitingOnMask(Uint32 mask);

	///This dumps all data in text form to the given file
	void dumpAllData(const std::string& file);
private:
	enum BarOrientation
	{
		LEFT_TO_RIGHT,
		RIGHT_TO_LEFT,
		TOP_TO_BOTTOM,
		BOTTOM_TO_TOP
	};

public:
	struct BuildProject
	{
		int posX;
		int posY;
		int teamNumber;
		int typeNum;
		int unitWorking;
		int unitWorkingFuture;
	};

private:
	///Initiates Game
	void init(GameGUI *gui, MapEdit* edit);

	///Clears existing game information, deleting the teams and players, in preperation of a new game.
	void clearGame();

	/// Look up a Building by its global ID. Returns nullptr if the slot is empty.
	/// Collapses the gid → team-index → building-index → pointer decode that
	/// would otherwise appear inline at every executeOrder caller.
	Building* lookupBuilding(Uint16 gid) const;

	/// Per-order-type executors. The dispatcher executeOrder() downcasts the
	/// shared_ptr<Order> to its concrete type and calls the matching helper.
	/// Helpers do NOT re-check team aliveness — the dispatcher gates that.
	void executeCreate(const OrderCreate& order, int localPlayer);
	void executeModifyBuilding(const OrderModifyBuilding& order, int localPlayer);
	void executeModifyExchange(const OrderModifyExchange& order, int localPlayer);
	void executeModifyFlag(const OrderModifyFlag& order, int localPlayer);
	void executeModifyClearingFlag(const OrderModifyClearingFlag& order, int localPlayer);
	/// Sets minLevelToFlag and flushes currently-assigned units by toggling
	/// maxUnitWorking through zero so the building releases them on update().
	void executeModifyMinLevelToFlag(const OrderModifyMinLevelToFlag& order, int localPlayer);
	void executeMoveFlag(const OrderMoveFlag& order, int localPlayer);
	void executeAlterateForbidden(const OrderAlterateForbidden& order, int localPlayer);
	void executeAlterateGuardArea(const OrderAlterateGuardArea& order, int localPlayer);
	void executeAlterateClearArea(const OrderAlterateClearArea& order, int localPlayer);
	void executeModifySwarm(const OrderModifySwarm& order, int localPlayer);
	/// Delete-building. Bypasses the team-alive gate: dead-team buildings
	/// can still be torn down.
	void executeDelete(const OrderDelete& order);
	void executeChangePriority(const OrderChangePriority& order);
	void executeCancelDelete(const OrderCancelDelete& order);
	void executeConstruction(const OrderConstruction& order);
	void executeCancelConstruction(const OrderConstruction& order);
	void executeSetAlliance(const SetAllianceOrder& order);
	/// Marks the leaving player's team dead only if no other player still
	/// controls that team; either way, the leaving player slot becomes AI::NONE.
	void executePlayerQuitGame(const PlayerQuitsGameOrder& order);

public:
	bool anyPlayerWaited;
	Uint32 maskAwayPlayer;

public:

private:
	/// Return whether there is no overlap between any buildings
	bool checkBuildingsDoNotOverlapAndHealMissing();
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth=2)
	{
		drawPointBar(x, y, orientation, maxLength, actLength, 0, r, g, b, r, g, b, barWidth);
	}

	///draws a point bar. This can be health, hunger, fill level, etc. Point bars can have 2 sections of actLength and secondActLength, followed by black until maxLength. r/g/b is for the first section, r2/g2/b2 for the second
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth=2);
	///draws an HP bar coloured green/yellow/red against the 0.6 / 0.3 hpRatio thresholds
	void drawHealthBar(int x, int y, int maxLength, int actLength, float hpRatio);
	///draws a building resource bar (food, bullets, ...) auto-shrinking to fit within (height*32)-10 pixels
	void drawBuildingResourceBar(int x, int y, BuildingType* type, int maxValue, int currentValue, Uint8 r, Uint8 g, Uint8 b);
	///draws the overlay representing water
	void drawMapWater(int sw, int sh, int viewportX, int viewportY, int time);
	///draws the terrain tiles of sand and gras
	void drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	///draws the resources like algues, wheat or fruit trees
	void drawMapRessources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	///draws the ground units. up till now those are workers and warriors
	void drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	///draws debug information. switched in the code.
	void drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings, const BuildingGuiStateMap* buildingGuiState);
	void drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawMapArea(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, Map * map, bool (Map::*mapIs)(int, int) const, int areaAnimationTick, AreaType areaType);
	void drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY);
	void drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawUnitPathLines(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	void drawUnitPathLine(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, Unit* unit);
	void drawUnitOffScreen(int sx, int sy, int sw, int sh, int viewportX, int viewportY, Unit* unit, Uint32 drawOptions);
	bool isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y);
public:
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector=NULL, std::vector<Uint32> *checkSumsVectorForBuildings=NULL, std::vector<Uint32> *checkSumsVectorForUnits=NULL, bool heavy=false);

	/// Sets the alliances from the GameHeader alliance teams
	void setAlliances(void);

public:
	///This is a static header for a map. It remains the same in between games on the same map.
	MapHeader mapHeader;
	///This is a game header. It contains all the settings for a particular game, from AI's to Alliances to victory conditions.
	GameHeader gameHeader;

	Team * teams[Team::MAX_COUNT];
	Player * players[Team::MAX_COUNT];
	Map map;
	MapScriptSGSL sgslScript; ///< SGSL script
	MapScript mapscript; ///< new script, currently USL
	GameObjectives objectives;
	GameHints gameHints;
	std::string missionBriefing;
	GameGUI *gui;
	MapEdit *edit;
#ifndef YOG_SERVER_ONLY
	//! Render-side container for bullet explosions and unit death
	//! animations. Always non-null in non-server builds; the runNoX
	//! gate is internal to GameAnimations. See
	//! src/render/GameAnimations.h.
	std::unique_ptr<GameAnimations> animations;
#endif  // !YOG_SERVER_ONLY
	std::list<BuildProject> buildProjects;
	///Stores alpha values to be passed to the drawing system. kept here so it isn't re-allocated
	///every frame
	std::valarray<unsigned char> overlayAlphas;

public:
	int mouseX, mouseY;
	Unit *mouseUnit;
	Unit *selectedUnit;
	Building *selectedBuilding;

	Uint32 stepCounter;
	int totalPrestige;
	int prestigeToReach;
	bool totalPrestigeReached;
	bool isGameEnded;
	///This is the IntBuildingType of a building type to be highlighted. All buildings of this type will be drawn
	///With an arrow pointed at them. This is primarily for tutorials and is linked through the script system
	///This is a mask, where 1<<typenum is the buildings to be highlighted
	Uint32 highlightBuildingType;
	///Similar to above, but for units
	Uint32 highlightUnitType;


	Team *getTeamWithMostPrestige(void);
	bool isPrestigeWinCondition(void);

public:
	bool oldMakeIslandsMap(MapGenerationDescriptor &descriptor);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	bool generateMap(MapGenerationDescriptor &descriptor);

protected:
	int ticksGameSum[TICK_PROFILE_BUF_LEN];
};

