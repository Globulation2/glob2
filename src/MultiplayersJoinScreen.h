/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __MULTIPLAYERJOINSCREEN_H
#define __MULTIPLAYERJOINSCREEN_H

#include "SessionConnection.h"
#include "MultiplayersJoin.h"
#include <GUIBase.h>
using namespace GAGGUI;

namespace GAGGUI
{
	class Text;
	class TextInput;
	class List;
}

class MultiplayersJoinScreen:public Screen
{
public:
	enum
	{
		CONNECT = 1,
		QUIT = 5,

		STARTED=11
	};
	MultiplayersJoin *multiplayersJoin;

private:
	//Sprite *arch;
	Text *serverText;
	TextInput *serverName;
	Text *playerText;
	TextInput *playerName;
	Text *statusText;
	Text *aviableGamesText;
	int oldStatus;
	List *lanServers;
	bool wasVisible;

public:
	MultiplayersJoinScreen();
	virtual ~MultiplayersJoinScreen();
	
	void onTimer(Uint32 tick);

	void onSDLEvent(SDL_Event *event);

	void onAction(Widget *source, Action action, int par1, int par2);
};

#endif
