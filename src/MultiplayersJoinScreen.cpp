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

#include "MultiplayersJoinScreen.h"
#include "GlobalContainer.h"
#include "GAG.h"

//MultiplayersJoinScreen pannel part !!

// this is the screen where you choose between :
// -Cancel

MultiplayersJoinScreen::MultiplayersJoinScreen()
{
	serverName=new TextInput(150, 170, 340, 30, globalContainer->standardFont, "localhost", true);
	strncpy(multiplayersJoin.serverName, serverName->text, 128);
	addWidget(serverName);

	playerName=new TextInput(150, 270, 340, 30, globalContainer->standardFont, globalContainer->settings.userName, false);
	strncpy(multiplayersJoin.playerName, playerName->text, 128);
	addWidget(playerName);

	serverText=new Text(150, 140, globalContainer->menuFont, globalContainer->texts.getString("[svr hostname]"));
	addWidget(serverText);

	playerText=new Text(150, 240, globalContainer->menuFont, globalContainer->texts.getString("[player name]"));
	addWidget(playerText);

	addWidget(new TextButton( 80, 350, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[connect]"), CONNECT));
	addWidget(new TextButton(360, 350, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[disconnect]"), DISCONNECT));
	addWidget(new TextButton(150, 415, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[goto main menu]"), QUIT));

	globalContainer->gfx->setClipRect();
}

MultiplayersJoinScreen::~MultiplayersJoinScreen()
{

}

void MultiplayersJoinScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);

	if (multiplayersJoin.waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		multiplayersJoin.sessionInfo.draw(gfxCtx);
		//addUpdateRect(20, 20, gfxCtx->getW()-40, 200);
	}
	else
	{
		//gfxCtx->drawString(150, 140, globalContainer->menuFont, globalContainer->texts.getString("[svr hostname]"));
		//gfxCtx->drawString(150, 240, globalContainer->menuFont, globalContainer->texts.getString("[player name]"));
	}
	addUpdateRect();
}


void MultiplayersJoinScreen::onTimer(Uint32 tick)
{
	static bool wasVisible=false;
	// TODO : call SessionInfo.draw()
	multiplayersJoin.onTimer(tick);

	if (multiplayersJoin.waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		if (wasVisible)
		{
			serverText->visible=false;
			serverName->visible=false;
			playerText->visible=false;
			playerName->visible=false;
			dispatchPaint(gfxCtx);
			wasVisible=false;
		}
	}
	else
	{
		if (!wasVisible)
		{
			serverText->visible=true;
			serverName->visible=true;
			playerText->visible=true;
			playerName->visible=true;
			dispatchPaint(gfxCtx);
			wasVisible=true;
		}
	}
	
	if (multiplayersJoin.waitingState==MultiplayersJoin::WS_SERVER_START_GAME)
	{
		if (multiplayersJoin.startGameTimeCounter<0)
		{
			printf("JoinScreen::Lets quit this screen and start game!\n");
			endExecute(STARTED);
		}
	}
}

void MultiplayersJoinScreen::onSDLEvent(SDL_Event *event)
{

}


void MultiplayersJoinScreen::onAction(Widget *source, Action action, int par1, int par2)
{

	if (action==TEXT_MODIFFIED)
	{
		if (source==serverName)
			strncpy(multiplayersJoin.serverName, serverName->text, 128);
		else if (source==playerName)
			strncpy(multiplayersJoin.playerName, playerName->text, 128);
		else
			assert(false);
	}
	else if (action==BUTTON_RELEASED)
	{
		if (par1==CONNECT)
		{
			if (!multiplayersJoin.tryConnection())
			{
				multiplayersJoin.waitingState=MultiplayersJoin::WS_TYPING_SERVER_NAME;
			}
		}
		else if (par1==DISCONNECT)
		{
			multiplayersJoin.quitThisGame();
		}
		else
		{
			endExecute(par1);
		}
	}
	else if (action==TEXT_ACTIVATED)
	{
		// we desactivate others texts inputs:
		if (source!=serverName)
			serverName->activated=false;
		if (source!=playerName)
			playerName->activated=false;
	}


}
