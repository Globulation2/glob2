/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __MAIN_MENU_SCREEN_H
#define __MAIN_MENU_SCREEN_H

#include "GAG.h"

class MainMenuScreen:public Screen
{
public:
	enum
	{
		CAMPAIN = 1,
		LOAD_GAME = 2,
		CUSTOM = 3,
		MULTIPLAYERS_YOG = 4,
		MULTIPLAYERS_LAN = 5,
		GAME_SETUP = 6,
		EDITOR = 7,
		QUIT = 8
	};
private:
	//Sprite *arch;
public:
	MainMenuScreen();
	virtual ~MainMenuScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
};

#endif
