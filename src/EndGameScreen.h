/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charri?re
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

#ifndef __END_GAME_SCREEN_H
#define __END_GAME_SCREEN_H

#include "GAG.h"
#include "GameGUI.h"

//! Widget to display stats at end of game
class EndGameStat: public Widget
{
public:
	//! Constructor, takes position and initial map name
	EndGameStat(int x, int y, Game *game);
	//! Destructor
	virtual ~EndGameStat() { }
	//! First paint call
	virtual void paint(void);
	//! Set the type of stats (units, buildings, prestige) to draw
	void setStatType(EndOfGameStat::Type type);
	
protected:
	//! internal paint routine
	void repaint(void);
	//! position of widget on screen
	int x, y;
	//! the type of the stat beeing drawn
	EndOfGameStat::Type type;
	//! Pointer to game, used for drawing
	Game *game;
};

class EndGameScreen:public Screen
{
protected:
	GameGUI *gui;
	EndGameStat *statWidget;
	
public:
	EndGameScreen(GameGUI *gui);
	virtual ~EndGameScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};

#endif