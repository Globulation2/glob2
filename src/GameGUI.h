/*
 * Globulation 2 game support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GAME_GUI_H
#define __GAME_GUI_H

#include "Header.h"
#include "Order.h"
#include "Game.h"
#include <queue>

#define MAX_UNIT_WORKING 20
#define MAX_FLAG_RANGE 10
#define MAX_RATIO_RANGE 16

class GameGUI
{
public:
	GameGUI();
	~GameGUI();
	// this handle mouse, keyboard and window resize inputs
	void step(void);
	// get order from gui, return NullOrder if 
	Order *getOrder(void);
	int getViewportX() { return viewportX; }
	int getViewportY() { return viewportY; }
	
	void draw(void);
	
	void load(SDL_RWops *stream);
	void save(SDL_RWops *stream);

	void processEvent(SDL_Event *event);
	void handleRightClick(void);
	void handleKey(SDLKey key, bool pressed);
	void handleMouseMotion(int mx, int my);
	void handleMapClick(int mx, int my, int button);
	void handleMenuClick(int mx, int my, int button);

public:
	Game game;
	bool isRunning;
	bool showExtendedInformation;
	int localPlayer, localTeam;

private:
	void drawScrollBox(int x, int y, int value, int valueLocal, int act, int max);
	void drawButton(int x, int y, const char *caption);
	void drawTextCenter(int x, int y, const char *caption, int i=-1);
	void checkValidSelection(void);
	void statStep(void);
	
private:
	enum DisplayMode
	{
		BUILDING_AND_FLAG,
		BUILDING_SELECTION_VIEW,
		UNIT_SELECTION_VIEW,
		STAT_VIEW,
	};
	
	enum StatMode
	{
		STAT_TEXT=0,
		STAT_GRAPH=1,
		NB_STAT_MODE=2
	};
	
	DisplayMode displayMode;
	Building *selBuild;
	Unit *selUnit;
	Sint32 selectionUID;
	bool needRedraw;
	int typeToBuild;
	
	TeamStat stats[128];
	int statsPtr;
	StatMode statMode;
	
	SDLBitmapFont *font;
	
	queue<Order *> orderQueue;
	
	int mouseX, mouseY;
	int viewportX, viewportY;
	int viewportSpeedX[8], viewportSpeedY[8];
};

#endif
 
