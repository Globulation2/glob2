/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GAME_GUI_H
#define __GAME_GUI_H

#include "Header.h"
#include "Order.h"
#include "Game.h"
#include <queue>

class TeamStats;
class OverlayScreen;
class InGameTextInput;

//! max unit working at a building
#define MAX_UNIT_WORKING 20
//! range of ratio for swarm
#define MAX_RATIO_RANGE 16

//! The Game Graphic User Interface
/*!
	Handle all user input during game, draw & handle menu.
*/
class GameGUI
{
public:
	Game game;
	bool paused;
	bool isRunning;
	//! true if user close the glob2 window.
	bool exitGlobCompletely;
	//! if this is not empty, then Engine should load the map with this filename.
	char toLoadGameFileName[SessionGame::MAP_NAME_MAX_SIZE+5];
	//bool showExtendedInformation;
	bool drawHealthFoodBar, drawPathLines;
	int localPlayer, localTeamNo;
	int viewportX, viewportY;

public:
	GameGUI();
	~GameGUI();

	void init();
	void adjustInitialViewport();
	//! Handle mouse, keyboard and window resize inputs, and stats
	void step(void);
	//! Get order from gui, return NullOrder if
	Order *getOrder(void);
	//! Return position on x
	int getViewportX() { return viewportX; }
	//! Return position on y
	int getViewportY() { return viewportY; }

	void drawAll(int team);
	void executeOrder(Order *order);

	//!
	bool loadBase(const SessionInfo *initial);
	//!
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream, const char *name);

	void processEvent(SDL_Event *event);

	// Engine has to call this every "real" steps. (or game steps)
	void synchroneStep(void);
	//! return the local team of the player who is running glob2
	Team *getLocalTeam(void) { return localTeam; }

	std::vector<int> buildingsChoice;
	std::vector<int> flagsChoice;

private:
	bool processGameMenu(SDL_Event *event);
	void handleRightClick(void);
	void handleKey(SDLKey key, bool pressed);
	void handleKeyAlways(void);
	void handleMouseMotion(int mx, int my, int button);
	void handleMapClick(int mx, int my, int button);
	void handleMenuClick(int mx, int my, int button);
	void handleActivation(Uint8 state, Uint8 gain);
	void coordinateFromMxMY(int mx, int my, int *cx, int *cy, bool useviewport=true);

	// Drawing support functions
	void drawScrollBox(int x, int y, int value, int valueLocal, int act, int max);
	void drawButton(int x, int y, const char *caption, bool doLanguageLookup=true);
	void drawBlueButton(int x, int y, const char *caption, bool doLanguageLookup=true);
	void drawRedButton(int x, int y, const char *caption, bool doLanguageLookup=true);
	void drawTextCenter(int x, int y, const char *caption, int i=-1);
	void drawValueAlignedRight(int y, int v);
	void drawCosts(int ressources[BASIC_COUNT], Font *font);

	void iterateSelection(void);
	void centerViewportOnSelection(void);
	void drawOverlayInfos(void);

	//! Draw the panel
	void drawPanel(void);
	//! Draw the buttons associated to the panel
	void drawPanelButtons(int pos);
	//! Draw a choice of buildings or flags
	void drawChoice(int pos, std::vector<int> &types);
	//! Draw the infos from a unit
	void drawUnitInfos(void);
	//! Draw the infos and actions from a building
	void drawBuildingInfos(void);

	//! Draw the menu during game
	void drawInGameMenu(void);
	//! Draw the message input field
	void drawInGameTextInput(void);

	void moveFlag(int mx, int my, bool drop);
	//! Of viewport have moved and a flag is selected, update it's position
	void flagSelectedStep(void);
	//! on each step, check if we have won or lost
	void checkWonConditions(void);

	friend class InGameAlliance8Screen;

	//! Display mode
	enum DisplayMode
	{
		BUILDING_VIEW=0,
		FLAG_VIEW,
		STAT_TEXT_VIEW,
		STAT_GRAPH_VIEW,
		NB_VIEWS,
	} displayMode;

	//! Selection mode
	enum SelectionMode
	{
		NO_SELECTION=0,
		BUILDING_SELECTION,
		UNIT_SELECTION,
		TOOL_SELECTION,
	} selectionMode;
	union
	{
		Building* building;
		Unit* unit;
		unsigned build;
	} selection;

	void setSelection(SelectionMode newSelMode, void* newSelection=NULL);
	void setSelection(SelectionMode newSelMode, unsigned newSelection);
	void clearSelection(void) { setSelection(NO_SELECTION); }
	void checkSelection(void);

	//! True if the mouse's button way never relased since selection.
	bool selectionPushed;
	//! True if the mouse's button way never relased since click im minimap.
	bool miniMapPushed;
	//! True if we try to put a mark in the minimap
	bool putMark;
	//! True if we are panning
	bool panPushed;
	//! Coordinate of mouse when began panning
	int panMouseX, panMouseY;
	//! Coordinate of viewport when began panning
	int panViewX, panViewY;

	bool showUnitWorkingToBuilding;

	TeamStats *teamStats;
	Team *localTeam;

	Uint32 chatMask;

	std::list<Order *> orderQueue;

	int mouseX, mouseY;
	//! for mouse motion
	int viewportSpeedX, viewportSpeedY;

	// menu related functions
	enum
	{
		IGM_NONE=0,
		IGM_MAIN,
		IGM_LOAD,
		IGM_SAVE,
		IGM_OPTION,
		IGM_ALLIANCE8,
		IGM_END_OF_GAME
	} inGameMenu;
	OverlayScreen *gameMenuScreen;

	bool hasEndOfGameDialogBeenShown;

	// On screen message handling
	// --------------------------

	struct Message
	{
		enum { MAX_MESSAGE_SIZE = 64 };
		enum { MAX_DISPLAYED_MESSAGE_SIZE = MAX_MESSAGE_SIZE+BasePlayer::MAX_NAME_LENGTH+4 }; // avoid network overflow
		enum { DEFAULT_MESSAGE_SHOW_TICKS = 180 };

		// since when it is shown
		int showTicks;
		char text[MAX_DISPLAYED_MESSAGE_SIZE];
		// color
		Uint8 r, g, b, a;
	};
	
	std::list<Message> messagesList;

	//! add a message to the window message list
	void addMessage(Uint8 r, Uint8 g, Uint8 b, const char *msgText, ...);
	
	// Typing stuff :
	InGameTextInput *typingInputScreen;
	int typingInputScreenPos;
	int typingInputScreenInc;
	
	// Minimap marking handling
	// ------------------------
	
	struct Mark
	{
		enum { DEFAULT_MARK_SHOW_TICKS = 40 };

		// since when it is shown
		int showTicks;
		// position
		int x, y;
		// color
		Uint8 r, g, b;
	};

	std::list<Mark> markList;

	//! add a minimap mark
	void addMark(MapMarkOrder *mmo);
};

#endif

