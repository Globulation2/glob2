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

#ifndef __GAME_GUI_DIALOG_H
#define __GAME_GUI_DIALOG_H

#include "GAG.h"
#include "Player.h"

class InGameMainScreen:public OverlayScreen
{
public:
	enum
	{
		LOAD_GAME = 0,
		SAVE_GAME = 1,
		OPTIONS = 2,
		ALLIANCES = 3,
		RETURN_GAME = 4,
		QUIT_GAME = 5
	};
public:
	InGameMainScreen();
	virtual ~InGameMainScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	//virtual void paint(int x, int y, int w, int h);
};

class GameGUI;

class InGameAlliance8Screen:public OverlayScreen
{
public:
	enum
	{
		OK = 0,
		ALLIED = 32,
		VISION = 64,
		CHAT= 96
	};

public:
	OnOffButton *allied[8];
	OnOffButton *vision[8];
	OnOffButton *chat[8];
	GameGUI *gameGUI;

public:
	InGameAlliance8Screen(GameGUI *gameGUI);
	virtual ~InGameAlliance8Screen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	Uint32 getAllianceMask(void);
	Uint32 getVisionMask(void);
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
		SPEED = 32,
		LATENCY = 64
	};

public:
	OnOffButton *speed[3];
	OnOffButton *latency[3];
	GameGUI *gameGUI;

public:
	InGameOptionScreen(GameGUI *gameGUI);
	virtual ~InGameOptionScreen() { }
	virtual void onAction(Widget *source, Action action, int par1, int par2);
};


#endif
