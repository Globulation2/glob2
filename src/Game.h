/*
 * Globulation 2 game support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GAME_H
#define __GAME_H

#include "GAG.h"
#include "Session.h"

#define BULLET_IMGID 52

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
	Game(const SessionInfo *initial);
	void loadBase(const SessionInfo *initial);
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
	void executeOrder(Order *order); // we need to free order at end
	bool load(SDL_RWops *stream); // load a saved game
    void save(SDL_RWops *stream);

	void step(Sint32 localTeam);

	// Editor stuff
	// add & remove teams, used by the map editor
	void addTeam(void);
	void removeTeam(void);
	void regenerateDiscoveryMap(void);

	//void addUnit(int x, int y, int team, int type, int level);
	Unit *addUnit(int x, int y, int team, int type, int level, int delta, int dx, int dy);
	Building *addBuilding(int x, int y, int team, int typeNum);
	bool removeUnitAndBuilding(int x, int y, SDL_Rect* r, int flags);
	bool removeUnitAndBuilding(int x, int y, int size, SDL_Rect* r, int flags=DEL_UNIT|DEL_BUILDING);

	bool checkRoomForBuilding(int coordX, int coordY, int typeNum, int *mapX, int *mapY, Sint32 team);
	bool checkRoomForBuilding(int x, int y, int typeNum, Sint32 team);
	
	void drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected, bool drawHealthFoodBar=false, bool useMapDiscovered=false);
	void drawMiniMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY);
	void renderMiniMap(int teamSelected);

	
	Sint32 checkSum();
	
public:
	SessionGame session;
	Team *teams[32];
	Player *players[32];
	Map map;
	SDL_Surface *minimap;
	
	static BuildingsTypes buildingsTypes;

public:
	int mouseX, mouseY;
	Unit *mouseUnit;
	Unit *selectedUnit;
	Building *selectedBuilding;
	
	Sint32 stepCounter;
};

#endif 
