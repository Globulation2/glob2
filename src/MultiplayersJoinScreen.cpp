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
#include "MultiplayersConnectedScreen.h"

//MultiplayersJoinScreen pannel part !!


MultiplayersJoinScreen::MultiplayersJoinScreen()
{
	multiplayersJoin=new MultiplayersJoin();

	serverName=new TextInput(150, 170, 340, 30, globalContainer->standardFont, "localhost", true);
	strncpy(multiplayersJoin->serverName, serverName->text, 128);
	addWidget(serverName);

	playerName=new TextInput(150, 270, 340, 30, globalContainer->standardFont, globalContainer->settings.userName, false);
	strncpy(multiplayersJoin->playerName, playerName->text, 128);
	addWidget(playerName);

	serverText=new Text(150, 140, globalContainer->menuFont, globalContainer->texts.getString("[svr hostname]"));
	addWidget(serverText);

	playerText=new Text(150, 240, globalContainer->menuFont, globalContainer->texts.getString("[player name]"));
	addWidget(playerText);

	addWidget(new TextButton( 80, 350, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[connect]"), CONNECT));
	addWidget(new TextButton(150, 415, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[goto main menu]"), QUIT));

	wasVisible=false;
}

MultiplayersJoinScreen::~MultiplayersJoinScreen()
{
	delete multiplayersJoin;
}

void MultiplayersJoinScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	addUpdateRect();
}


void MultiplayersJoinScreen::onTimer(Uint32 tick)
{
	multiplayersJoin->onTimer(tick);
	
	if (multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 20);
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			dispatchPaint(gfxCtx);
		}
		else if (rv==MultiplayersConnectedScreen::STARTED)
		{
			endExecute(STARTED);
		}
		else
		{
			printf("rv=%d\n", rv);
			assert(false);
		}
		delete multiplayersConnectedScreen;
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
			strncpy(multiplayersJoin->serverName, serverName->text, 128);
		else if (source==playerName)
			strncpy(multiplayersJoin->playerName, playerName->text, 128);
		else
			assert(false);
	}
	else if (action==BUTTON_RELEASED)
	{
		if (par1==CONNECT)
		{
			multiplayersJoin->tryConnection();
		}
		else if (par1==QUIT)
		{
			endExecute(QUIT);
		}
		else
			assert(false);
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
