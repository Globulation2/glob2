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
#include "GameGUIDialog.h"
#include <queue>
class TeamStats;

#define MAX_UNIT_WORKING 20
#define MAX_EXPLO_FLAG_RANGE 20
#define MAX_WAR_FLAG_RANGE 8
#define MAX_RATIO_RANGE 16
#define MAX_CLEARING_FLAG_RANGE 14

//! The screen that contains the text input while typing message in game
class InGameTextInput:public OverlayScreen
{
protected:
	//! the text input widget
	TextInput *textInput;

public:
	//! InGameTextInput constructor
	InGameTextInput();
	//! InGameTextInput destructor
	virtual ~InGameTextInput() { }
	//! React on action from any widget (but there is only one anyway)
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	//! Return the text typed
	const char *getText(void) const { return textInput->getText(); }
	//! Set the text
	void setText(const char *text) const { textInput->setText(text); }
};

//! The Game Graphic User Interface
/*!
	Handle all user input during game, draw & handle menu.
*/
class GameGUI
{
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

	void draw(void);
	void drawAll(int team);
	void executeOrder(Order *order);

	bool loadBase(const SessionInfo *initial);
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream, char *name);

	void processEvent(SDL_Event *event);

private:
	bool processGameMenu(SDL_Event *event);
	void handleRightClick(void);
	void handleKey(SDL_keysym keySym, bool pressed);
	void handleMouseMotion(int mx, int my, int button);
	void handleMapClick(int mx, int my, int button);
	void handleMenuClick(int mx, int my, int button);
	void handleActivation(Uint8 state, Uint8 gain);
	void coordinateFromMxMY(int mx, int my, int *cx, int *cy, bool useviewport=true);
	void drawScrollBox(int x, int y, int value, int valueLocal, int act, int max);
	void drawButton(int x, int y, const char *caption, bool doLanguageLookup=true);
	void drawRedButton(int x, int y, const char *caption, bool doLanguageLookup=true);
	void drawTextCenter(int x, int y, const char *caption, int i=-1);
	void checkValidSelection(void);
public:
	// Engine has to call this every "real" steps. (or game steps)
	void synchroneStep(void);
private:
	void iterateSelection(void);
	void centerViewportOnSelection(void);
	void drawOverlayInfos(void);
	//! Draw the menu during game
	void drawInGameMenu(void);
	//! Draw the message input field
	void drawInGameTextInput(void);
	void moveFlag(int mx, int my);
	//! Of viewport have moved and a flag is selected, update it's position
	void flagSelectedStep(void);

public:
	Game game;
	bool paused;
	bool isRunning;
	//! true if user close the glob2 window.
	bool exitGlobCompletely;
	//! if this is not emptih, then Engine should load the map with this filename.
	char toLoadGameFileName[Map::MAP_NAME_MAX_SIZE+5];
	//bool showExtendedInformation;
	bool drawHealthFoodBar, drawPathLines;
	int localPlayer, localTeamNo;
	int viewportX, viewportY;

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
	//! True if the mouse's button way never relased since selection.
	bool selectionPushed;
	//! True if the mouse's button way never relased since click im minimap.
	bool miniMapPushed;
	//! True if we try to put a mark in the minimap
	bool putMark;
	Unit *selUnit;
	Sint32 selectionUID;
	bool needRedraw;
	int typeToBuild;
	bool showUnitWorkingToBuilding;

	TeamStats *teamStats;
	Team *localTeam;
	StatMode statMode;

	Uint32 chatMask;

	std::list<Order *> orderQueue;

	int mouseX, mouseY;
	int viewportSpeedX[9], viewportSpeedY[9];
	
	// menu related functions
	enum
	{
		IGM_NONE=0,
		IGM_MAIN,
		IGM_LOAD,
		IGM_SAVE,
		IGM_OPTION,
		IGM_ALLIANCE8
	} inGameMenu;
	OverlayScreen *gameMenuScreen;

private :

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

