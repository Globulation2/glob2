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

MultiplayersHostScreen::MultiplayersHostScreen(SessionInfo *sessionInfo, bool shareOnYOG)
{
	addWidget(new TextButton(420, 420, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), START));
	addWidget(new TextButton(180, 420, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL));

	multiplayersHost=new MultiplayersHost(sessionInfo, shareOnYOG);
	multiplayersJoin=NULL;

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[awaiting players]"), 600, 0));

	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		color[i]=new ColorButton(22, 62+i*20, 16, 16, COLOR_BUTTONS+i);
		color[i]->visible=false;
		addWidget(color[i]);
	}
}

MultiplayersHostScreen::~MultiplayersHostScreen()
{
	delete multiplayersHost;
}

void MultiplayersHostScreen::onTimer(Uint32 tick)
{
	multiplayersHost->onTimer(tick);

	multiplayersHost->sessionInfo.draw(gfxCtx);
	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		if (multiplayersHost->sessionInfo.players[i].netState>BasePlayer::PNS_BAD)
		{
			color[i]->paint(gfxCtx);
			color[i]->visible=true;
		}
		else
			color[i]->visible=false;
	}
	addUpdateRect(20, 20, gfxCtx->getW()-40, 400);
	
	if ((multiplayersHost->serverIP.host!=0) && (multiplayersJoin==NULL))
	{
		multiplayersJoin=new MultiplayersJoin();
		strncpy(multiplayersJoin->playerName, globalContainer->settings.userName, 128);
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
		
	if (multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_PLAYING_COUNTER)
		if (multiplayersHost->startGameTimeCounter%20==0)
			dispatchPaint(gfxCtx);

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
		case -1:
			multiplayersHost->stopHosting();
			endExecute(par1);
		break;
		default:
		{
			if ((par1>=COLOR_BUTTONS)&&(par1<COLOR_BUTTONS+MAX_NUMBER_OF_PLAYERS))
				multiplayersHost->switchPlayerTeam(par1-COLOR_BUTTONS);
		}
		break;
		}
	}
}

void MultiplayersHostScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_PLAYING_COUNTER)
	{
		char s[256];
		snprintf(s, 256, "%s%d", globalContainer->texts.getString("[STARTING GAME ...]"), multiplayersHost->startGameTimeCounter/20);		
		int h=globalContainer->menuFont->getStringHeight(s);
		printf("s=%s.\n", s);
		gfxCtx->drawString(20, 460-h, globalContainer->menuFont, s);
		addUpdateRect(20, 460-h, gfxCtx->getW()-40, h);
	}
}
