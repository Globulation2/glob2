/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __GAME_H
#define __GAME_H

#include "Map.h"
#include "SGSL.h"
#include <string>
#include <valarray>
#include "MapHeader.h"
#include "GameHeader.h"
#include "GameObjectives.h"
#include "GameHints.h"

namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;
class MapGenerationDescriptor;
class GameGUI;
class BuilgingType;
class MapEdit;

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
	void integrity(void);

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
	void executeOrder(boost::shared_ptr<Order> order, int localPlayer);

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

	// Editor stuff
	// add & remove teams, used by the map editor and the random map generator
	void addTeam(int pos=-1);
	void removeTeam(int pos=-1);
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
	void drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected, Uint32 drawOptions = 0, std::set<Building*> *visibleBuildings = 0);

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
	
	struct BuildProject
	{
		int posX;
		int posY;
		int teamNumber;
		int typeNum;
		int unitWorking;
		int unitWorkingFuture;
	};
	
	///Initiates Game
	void init(GameGUI *gui, MapEdit* edit);

	///Clears existing game information, deleting the teams and players, in preperation of a new game.
	void clearGame();

public:
	bool anyPlayerWaited;
	int anyPlayerWaitedTimeFor;
	Uint32 maskAwayPlayer;

public:

private:
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth=2)
	{
		drawPointBar(x, y, orientation, maxLength, actLength, 0, r, g, b, r, g, b, barWidth);
	}

	//Point bars can have 2 sections of actLength and secondActLength, followed by black until maxLength. r/g/b is for the first section, r2/g2/b2 for the second
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth=2);
	inline void drawMapWater(int sw, int sh, int viewportX, int viewportY, int time);
	inline void drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapRessources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings);
	inline void drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY);
	inline void drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawUnitPathLines(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawUnitPathLine(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, Unit* unit);
	static float interpolateValues(float a, float b, float x);
	inline bool isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y);
public:
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector=NULL, std::vector<Uint32> *checkSumsVectorForBuildings=NULL, std::vector<Uint32> *checkSumsVectorForUnits=NULL, bool heavy=false);
	
	/// Sets the alliances from the GameHeader alliance teams
	void setAlliances(void);
	
public:
	///This is a static header for a map. It remains the same in between games on the same map.
	MapHeader mapHeader;
	///This is a game header. It contains all the settings for a particular game, from AI's to Alliances to victory conditions.
	GameHeader gameHeader;

	Team *teams[32];
	Player *players[32];
	Map map;
	Mapscript script;
	GameObjectives objectives;
	GameHints gameHints;
	std::string missionBriefing;
	GameGUI *gui;
	MapEdit *edit;
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
	///This is the IntBuildingType of a building type to be hilighted. All buildings of this type will be drawn
	///With an arrow pointed at them. This is primarily for tutorials and is linked through the script system
	///This is a mask, where 1<<typenum is the buildings to be hilighted
	Uint32 hilightBuildingType;
	///Similar to above, but for units
	Uint32 hilightUnitType;
	
	
	Team *getTeamWithMostPrestige(void);
	bool isPrestigeWinCondition(void);
	
public:
	bool oldMakeIslandsMap(MapGenerationDescriptor &descriptor);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	bool generateMap(MapGenerationDescriptor &descriptor);

protected:
	FILE *logFile;
	int ticksGameSum[32];
};

#endif
