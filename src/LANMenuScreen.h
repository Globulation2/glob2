/*
  Copyright (C) 2007 Bradley Arsenault

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
