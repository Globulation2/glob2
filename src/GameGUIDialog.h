/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __GAME_GUI_DIALOG_H
#define __GAME_GUI_DIALOG_H

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
		ALLIANCES = 3,
		OBJECTIVES = 4,
		RETURN_GAME = 5,
		QUIT_GAME = 6
	};
public:
	InGameMainScreen(bool showAlliance);
	virtual ~InGameMainScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};

class InGameEndOfGameScreen:public OverlayScreen
{
public:
	enum
	{
		QUIT = 0,
		CONTINUE = 1
	};
public:
	InGameEndOfGameScreen(const char *title, bool canContinue);
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
public:
	//If show briefing is enabled, then the briefing tab will be shown rather than the objectives tab
	InGameObjectivesScreen(GameGUI* gui, bool showBriefing);
	virtual ~InGameObjectivesScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	
	Text* objectives;
	Text* briefing;
	Text* hints;
	std::vector<Widget*> objectivesWidgets;
	std::vector<Widget*> briefingWidgets;
	std::vector<Widget*> hintsWidgets;
};


#endif
