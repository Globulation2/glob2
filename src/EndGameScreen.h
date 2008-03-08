/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  Copyright (C) 2006 Bradley Arsenault

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

#ifndef __END_GAME_SCREEN_H
#define __END_GAME_SCREEN_H

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
	virtual ~EndGameStat() { }
	//! Set the type of stats (units, buildings, prestige) to draw
	void setStatType(EndOfGameStat::Type type);
	//! Enables / disables a particular team
	void setEnabledState(int teamNum, bool isEnabled);
	//! paint routine
	virtual void paint(void);

protected:
	//! Returns the value at the given point, by interpolating
	double getValue(double position, int team, int type);

	//! the type of the stat beeing drawn
	EndOfGameStat::Type type;
	//! Pointer to game, used for drawing
	Game *game;
	//! List of true/false values for each team's enabled status
	bool isTeamEnabled[32];
	//! This moves the circle indicating the score at the current mouse position.
	virtual void onSDLMouseMotion(SDL_Event* event);
	int mouse_x;
	int mouse_y;
};

struct TeamEntry
{
	std::string name;
	int teamNum;
	int endVal[EndOfGameStat::TYPE_NB_STATS];
	GAGCore::Color color;
};

class EndGameScreen : public Glob2Screen
{
protected:
	std::vector<TeamEntry> teams;
	std::vector<Text *> names;
	std::vector<OnOffButton *> team_enabled_buttons;
	EndGameStat *statWidget;
	Text* graphLabel;

protected:
	//! resort players
	void sortAndSet(EndOfGameStat::Type type);
	
public:
	EndGameScreen(GameGUI *gui);
	virtual ~EndGameScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};

#endif
