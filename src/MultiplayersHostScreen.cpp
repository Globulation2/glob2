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

//MultiplayersHostScreen pannel part !!

// This is the screen that add Players to sessionInfo.
// There are two buttons:
// -Start
// -Cancel

#include "MultiplayersHostScreen.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "YOGScreen.h"
//#include "NetConsts.h"

MultiplayersHostScreen::MultiplayersHostScreen(SessionInfo *sessionInfo)
{
	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), START));
	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL));

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	multiplayersHost=new MultiplayersHost(sessionInfo, true);
	multiplayersJoin=NULL;

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[awaiting players]"), 600, 0));
}

MultiplayersHostScreen::~MultiplayersHostScreen()
{
	delete multiplayersHost;
}

void MultiplayersHostScreen::onTimer(Uint32 tick)
{
	multiplayersHost->onTimer(tick);

	multiplayersHost->sessionInfo.draw(gfxCtx);
	addUpdateRect(20, 20, gfxCtx->getW()-40, 200);
	
	if ((multiplayersHost->serverIP.host!=0) && (multiplayersJoin==NULL))
	{
		multiplayersJoin=new MultiplayersJoin();
		strncpy(multiplayersJoin->playerName, "Host Name", 128);//TODO: uses username
		char *s=SDLNet_ResolveIP(&(multiplayersHost->serverIP)) ;//char *SDLNet_ResolveIP(IPaddress *address) 
		if (s)
			strncpy(multiplayersJoin->serverName, s, 128);
		else
		{
			// a home made translation:
			Uint32 ip=SDL_SwapBE32(multiplayersHost->serverIP.host);
			snprintf(multiplayersJoin->serverName, 128, "%d.%d.%d.%d\n", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF));
		}
		
		multiplayersJoin->tryConnection();
	}
	
	if (multiplayersJoin)
		multiplayersJoin->onTimer(tick);

	if ((multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_GAME_START_SENDED)&&(multiplayersHost->startGameTimeCounter<0))
		endExecute(STARTED);
}

void MultiplayersHostScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_RELEASED)
	{
		switch (par1)
		{
		case START :
			multiplayersHost->startGame();
		break;
		case CANCEL :
			multiplayersHost->stopHosting();
			endExecute(par1);
		break;
		}
	}
}

void MultiplayersHostScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}
