// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __LAN_FIND_SCREEN_H
#define __LAN_FIND_SCREEN_H

#include "Glob2Screen.h"
#include "NetBroadcastListener.h"

namespace GAGGUI
{
	class Text;
	class TextInput;
	class List;
}

class LANFindScreen : public Glob2Screen
{
public:
	///Construct a LANFindScreen
	LANFindScreen();
	virtual ~LANFindScreen();
	
	void onTimer(Uint32 tick);

	void onSDLEvent(SDL_Event *event);

	void onAction(Widget *source, Action action, int par1, int par2);

	enum
	{
		CONNECT = 1,
		QUIT = 5,

		STARTED=11
	};

private:
	//Sprite *arch;
	Text *serverText;
	TextInput *serverName;
	Text *playerText;
	TextInput *playerName;
	Text *statusText;
	Text *availableGamesText;

	List *lanServers;
	bool wasVisible;
	
	NetBroadcastListener listener;
};

#endif
