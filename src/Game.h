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

#ifndef __GAME_H
#define __GAME_H

#include "Map.h"
#include "Session.h"
#include "SGSL.h"
#include <string>

namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;
class MapGenerationDescriptor;
class GameGUI;
class BuilgingType;

class Game
{
	static const bool verbose = false;
public:
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
		DRAW_STARVING_OVERLAY = 0x100,
		DRAW_DAMAGED_OVERLAY = 0x200,
		DRAW_DEFENSE_OVERLAY = 0x400,
	};

	enum MinimapDrawOption
	{
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

public:
	Game(GameGUI *gui);
	virtual ~Game();

private:
	enum BarOrientation
	{
		LEFT_TO_RIGHT,
		RIGHT_TO_LEFT,
		TOP_TO_BOTTOM,
		BOTTOM_TO_TOP
	};
	
	void init(GameGUI *gui);
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth=2);
	
	//! return true if all human are allied together, flase otherwise
	bool isHumanAllAllied(void);
	

public:
	bool anyPlayerWaited;
	int anyPlayerWaitedTimeFor;
	Uint32 maskAwayPlayer;

public:

	//! This methode will overide currents values of *this by the values of *initial.
	void setBase(const SessionInfo *initial);
	void executeOrder(Order *order, int localPlayer);
	bool load(GAGCore::InputStream *stream);
	void save(GAGCore::OutputStream *stream, bool fileIsAMap, const char *name);

	void buildProjectSyncStep(Sint32 localTeam);
	//! look for each team if it has won or not
	void wonSyncStep(void);
	//! call script.step(), then check conditions and updates internal variables if needed
	void scriptSyncStep();
	//! before any game logic can be executed, we have to clear the event queu from the last tick
	void clearEventsStep(void);
	//! called by gui, execute a step for this game. The gui parameter is for the script
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
	void drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected, Uint32 drawOptions = 0);
private:

	inline void drawMapWater(int sw, int sh, int viewportX, int viewportY, int time);
	inline void drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapRessources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY);
	inline void drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	inline void drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions);
	static float interpolateValues(float a, float b, float x);
public:
	void drawMiniMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions = 0);
	void renderMiniMap(int teamSelected, const bool useMapDiscovered=false, int step=0, int stepCount=1);
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector=NULL, std::vector<Uint32> *checkSumsVectorForBuildings=NULL, std::vector<Uint32> *checkSumsVectorForUnits=NULL);
	
	//! ally or disally AI following human alliances
	void setAIAlliance(void);
	
public:
	SessionGame session;
	Team *teams[32];
	Player *players[32];
	Map map;
	DrawableSurface *minimap;
	Mapscript script;
	std::string campaignText;
	GameGUI *gui;
	std::list<BuildProject> buildProjects;

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
	
	Team *getTeamWithMostPrestige(void);
	
public:
	bool oldMakeIslandsMap(MapGenerationDescriptor &descriptor);
	bool makeRandomMap(MapGenerationDescriptor &descriptor);
	bool generateMap(MapGenerationDescriptor &descriptor);
	
protected:
	FILE *logFile;
	int ticksGameSum[32];
};

//! extract the user-visible name from a glob2 map filename, return empty string if filename is an invalid glob2 map
std::string glob2FilenameToName(const char *filename);
//! create the filename from the directory, end user-visible name and extension. directory and extension must be given without the / and the .
std::string glob2NameToFilename(const char *dir, const char *name, const char *extension=NULL);


#endif
