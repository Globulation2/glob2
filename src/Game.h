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

#ifndef __GAME_H
#define __GAME_H

#include "GAG.h"
#include "Session.h"
#include "SGSL.h"

#define BULLET_IMGID 52

class MapGenerationDescriptor;

class Game
{
public:
	enum FlagForRemoval
	{
		DEL_BUILDING=0x1,
		DEL_UNIT=0x2
	};

public:
	Game();
	//Game(const SessionInfo *initial);
	//bool loadBase(const SessionInfo *initial);
	virtual ~Game();

private:
	enum BarOrientation
	{
		LEFT_TO_RIGHT,
		RIGHT_TO_LEFT,
		TOP_TO_BOTTOM,
		BOTTOM_TO_TOP
	};
	
	void init(void);
	void drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth=2);
public:
	bool anyPlayerWaited;
	Uint32 maskAwayPlayer;
public:

	void setBase(const SessionInfo *initial);
	void executeOrder(Order *order, int localPlayer);
	bool load(SDL_RWops *stream);
    void save(SDL_RWops *stream, bool fileIsAMap, char *name);

	//! look for each team if it has won or not
	void wonStep(void);
	//! call script.step(), then check conditions and updates internal variables if needed
	void scriptStep(void);

	void step(Sint32 localTeam);

	// Editor stuff
	// add & remove teams, used by the map editor
	void addTeam(void);
	void removeTeam(void);
	void regenerateDiscoveryMap(void);

	//void addUnit(int x, int y, int team, int type, int level);
	Unit *addUnit(int x, int y, int team, int type, int level, int delta, int dx, int dy);
	Building *addBuilding(int x, int y, int team, int typeNum);
	//! This remove anything at case(x, y), and return a rect which include every removed things.
	bool removeUnitAndBuilding(int x, int y, SDL_Rect* r, int flags);
	bool removeUnitAndBuilding(int x, int y, int size, SDL_Rect* r, int flags=DEL_UNIT|DEL_BUILDING);

	bool checkRoomForBuilding(int coordX, int coordY, int typeNum, int *mapX, int *mapY, Sint32 team);
	bool checkRoomForBuilding(int x, int y, int typeNum, Sint32 team);
	bool checkHardRoomForBuilding(int coordX, int coordY, int typeNum, int *mapX, int *mapY, Sint32 team);
	bool checkHardRoomForBuilding(int x, int y, int typeNum, Sint32 team);

	void drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected, bool drawHealthFoodBar=false, bool drawPathLines=false, bool drawBuildingRects=true, const bool useMapDiscovered=false);
	void drawMiniMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected=-1);
	void renderMiniMap(int teamSelected, bool showUnitsAndBuildings=false);
	Sint32 checkSum();
	
public:
	SessionGame session;
	Team *teams[32];
	Player *players[32];
	Map map;
	DrawableSurface *minimap;
	Mapscript script;
	
	/* <leto> I moved this to globalContainer.
	static BuildingsTypes buildingsTypes;
	*/

public:
	int mouseX, mouseY;
	Unit *mouseUnit;
	Unit *selectedUnit;
	Building *selectedBuilding;
	
	Sint32 stepCounter;
public:
	void makeIslandsMap(MapGenerationDescriptor &descriptor);
	void makeRandomMap(MapGenerationDescriptor &descriptor);
	void generateMap(MapGenerationDescriptor &descriptor);
};

#endif 
