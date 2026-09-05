// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GUIBase.h>


using namespace GAGGUI;
namespace GAGGUI
{
	class OnOffButton;
	class TriButton;
	class Selector;
	class Text;
}
class GameGUI;
class GameHeader;

class InGameMainScreen:public OverlayScreen
{
public:
	enum
	{
		LOAD_GAME = 0,
		SAVE_GAME = 1,
		OPTIONS = 2,
		RETURN_GAME = 5,
		QUIT_GAME = 6
	};
public:
	InGameMainScreen(bool isReplay = false);
	virtual ~InGameMainScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};

class InGameEndOfGameScreen:public OverlayScreen
{
public:
	enum
	{
		QUIT = 0,
		CONTINUE = 1,
		WATCH_AGAIN = 2
	};
public:
	InGameEndOfGameScreen(std::string title, bool canContinue);
	virtual ~InGameEndOfGameScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};

class GameGUI;

class InGameAllianceScreen:public OverlayScreen
{
public:
	enum
	{
		OK = 0,
		ALLIED = 32,
		NORMAL_VISION = 64,
		FOOD_VISION = 96,
		MARKET_VISION = 128,
		CHAT= 160
	};

public:
	Text *texts[16];
	OnOffButton *alliance[16];
	OnOffButton *normalVision[16];
	OnOffButton *foodVision[16];
	OnOffButton *marketVision[16];
	OnOffButton *chat[16];
	GameGUI *gameGUI;

public:
	InGameAllianceScreen(GameGUI *gameGUI);
	virtual ~InGameAllianceScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	int countNumberPlayersForLocalTeam(GameHeader& gameHeader, int localteam);
	Uint32 getAlliedMask(void);
	Uint32 getEnemyMask(void);
	Uint32 getExchangeVisionMask(void);
	Uint32 getFoodVisionMask(void);
	Uint32 getOtherVisionMask(void);
	Uint32 getChatMask(void);

protected:
	void setCorrectValueForPlayer(int i);
};

class InGameOptionScreen:public OverlayScreen
{
public:
	enum
	{
		OK = 0,
		MUTE = 1,
	};

public:
	Selector *musicVol;
	Selector *voiceVol;
	OnOffButton* mute;
	Text *musicVolText;
	Text *voiceVolText;
public:
	InGameOptionScreen(GameGUI *gameGUI);
	~InGameOptionScreen();
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};


///This screen shows the current objectives of the mission, a mission briefing, and
///hints as the mission goes along
class InGameObjectivesScreen:public OverlayScreen
{
public:
	enum
	{
		OBJECTIVES = 1,
		BRIEFING = 2,
		HINTS = 3,
		OK = 4,
	};
	enum
	{
		FIRST_TAB = OBJECTIVES,
		TAB_COUNT = 3,
	};
public:
	//If show briefing is enabled, then the briefing tab will be shown rather than the objectives tab
	InGameObjectivesScreen(GameGUI* gui, bool showBriefing);
	virtual ~InGameObjectivesScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);

	Text* objectives;
	Text* briefing;
	Text* hints;
	///The widgets belonging to each tab, indexed by tab id minus FIRST_TAB
	std::vector<Widget*> tabWidgets[TAB_COUNT];
	///Returns the widgets belonging to the given tab (OBJECTIVES, BRIEFING or HINTS)
	std::vector<Widget*>& widgetsForTab(int tab);
	///Makes the given tab's widgets visible and hides those of the other tabs
	void showTab(int tab);
};


