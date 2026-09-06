// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "GameGUI.h"
#include "Glob2Screen.h"

namespace GAGGUI
{
	class Text;
	class OnOffButton;
}

//! Widget to display stats at end of game
class EndGameStat: public RectangularWidget
{
public:
	//! Constructor, takes position and initial map name
	EndGameStat(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Game *game);
	//! Destructor
	virtual ~EndGameStat();
	//! Set the type of stats (units, buildings, prestige) to draw
	void setStatType(EndOfGameStat::Type type);
	//! Enables / disables a particular team
	void setEnabledState(int teamNum, bool isEnabled);
	//! paint routine
	virtual void paint(void);

protected:
	//! Returns the value at the given point, by interpolating
	double getValue(double position, int team, int type);

	//! Returns the text for a particular time from seconds
	std::string getTimeText(int seconds);
	
	//! Returns the text for the right-scale
	std::string getRightScaleText(int value, int digits);
	
	/// Get the label of the end game stat
	std::string getStatLabel();

	//! the type of the stat beeing drawn
	EndOfGameStat::Type type;
	//! Pointer to game, used for drawing
	Game *game;
	//! List of true/false values for each team's enabled status
	bool * isTeamEnabled;
	//! This moves the circle indicating the score at the current mouse position.
	virtual void onSDLMouseMotion(SDL_Event* event);
	int mouse_x;
	int mouse_y;
};

struct TeamEntry
{
	int teamNum;
	int endVal[EndOfGameStat::TYPE_NB_STATS];
	GAGCore::Color color;
	std::string name;
};

class EndGameScreen : public Glob2Screen
{
public:
	//! Return values passed by the screen's buttons to onAction
	enum ButtonId
	{
		//! stat selector buttons use their EndOfGameStat::Type as id
		STAT_BUTTON_FIRST = 0,
		//! per-team toggle buttons use TEAM_TOGGLE_FIRST + row index
		TEAM_TOGGLE_FIRST = EndOfGameStat::TYPE_NB_STATS,
		QUIT = 38,
		SAVE_REPLAY = 39
	};

protected:
	std::vector<Text*> names;
	std::vector<TeamEntry> teams;
	std::vector<OnOffButton *> team_enabled_buttons;
	EndGameStat *statWidget;
	Text* graphLabel;

protected:
	//! resort players
	void sortAndSet(EndOfGameStat::Type type);

	//! Translated short name of a stat type, used for its selector button and the graph label
	static std::string statTypeName(EndOfGameStat::Type type);
	
	//! pointer to the game, necessary for correctly saving replays
	Game *game;
	
public:
	EndGameScreen(GameGUI *gui);
	virtual ~EndGameScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);

private:
	void saveReplay(const char *dir, const char *ext);
};

