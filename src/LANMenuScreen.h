// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __LAN_MENU_SCREEN_H
#define __LAN_MENU_SCREEN_H

#include "Glob2Screen.h"

class LANMenuScreen : public Glob2Screen
{
public:

	///Constructs a LAN menu screen
	LANMenuScreen();
	virtual ~LANMenuScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
	
	enum
	{
		HostedGame,
		JoinedGame,
		QuitMenu
	};
	
	
public:

	enum
	{
		HOST = 1,
		JOIN = 4,
		QUIT = 5
	};
};

#endif
