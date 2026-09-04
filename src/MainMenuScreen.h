// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __MAIN_MENU_SCREEN_H
#define __MAIN_MENU_SCREEN_H

#include "Glob2Screen.h"

class MainMenuScreen:public Glob2Screen
{
public:
	enum
	{
		CAMPAIGN,
		TUTORIAL,
		LOAD_GAME,
		CUSTOM,
		MULTIPLAYERS_YOG,
		MULTIPLAYERS_LAN,
		GAME_SETUP,
		EDITOR,
		CREDITS,
		QUIT,
	};
	
public:
	MainMenuScreen();
	virtual ~MainMenuScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	static int menu(void);
};

#endif
