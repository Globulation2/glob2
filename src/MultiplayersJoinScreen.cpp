/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "MultiplayersJoinScreen.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "MultiplayersConnectedScreen.h"
#include "Utilities.h"

MultiplayersJoinScreen::MultiplayersJoinScreen()
{
	multiplayersJoin=new MultiplayersJoin(false);

	serverName=new TextInput(20, 170, 280, 30, "standard", "localhost", true);
	strncpy(multiplayersJoin->serverName, serverName->getText(), 256);
	multiplayersJoin->serverName[255]=0;
	addWidget(serverName);

	playerName=new TextInput(20, 270, 280, 30, "standard", globalContainer->userName, false, 32);
	strncpy(multiplayersJoin->playerName, playerName->getText(), 32);
	multiplayersJoin->playerName[31]=0;
	addWidget(playerName);

	serverText=new Text(20, 140, "menu", globalContainer->texts.getString("[svr hostname]"));
	addWidget(serverText);

	playerText=new Text(20, 240, "menu", globalContainer->texts.getString("[player name]"));
	addWidget(playerText);

	aviableGamesText=new Text(320, 90, "menu", globalContainer->texts.getString("[aviable lan games]"));
	addWidget(aviableGamesText);

	statusText=new Text(20, 390, "standard", "");
	addWidget(statusText);

	addWidget(new TextButton( 20, 420, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[connect]"), CONNECT, 13));
	addWidget(new TextButton(280, 420, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[goto main menu]"), QUIT, 27));

	lanServers=new List(320, 120, 280, 180, "menu");
	addWidget(lanServers);

	wasVisible=false;

	oldStatus=MultiplayersJoin::WS_BAD; //We wants redraw at first show.
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
	
	if (multiplayersJoin->waitingState!=oldStatus)
	{
		char *s=multiplayersJoin->getStatusString();
		statusText->setText(s);
		delete[] s;
		oldStatus=multiplayersJoin->waitingState;
	}

	if (multiplayersJoin->listHasChanged)
	{
		lanServers->clear();
		std::list<MultiplayersJoin::LANHost>::iterator it;
		for (it=multiplayersJoin->lanHosts.begin(); it!=multiplayersJoin->lanHosts.end(); ++it)
			lanServers->addText(it->gameName);
		lanServers->commit();
		multiplayersJoin->listHasChanged=false;
	}
	
	if (multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 40);
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			dispatchPaint(gfxCtx);
		}
		else if (rv==MultiplayersConnectedScreen::DISCONNECTED)
		{
			dispatchPaint(gfxCtx);
		}
		else if (rv==MultiplayersConnectedScreen::STARTED)
		{
			endExecute(STARTED);
		}
		else if (rv==-1)
		{
			multiplayersJoin->quitThisGame();
			endExecute(-1);
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

	if (action==TEXT_MODIFIED)
	{
		if (source==serverName)
		{
			strncpy(multiplayersJoin->serverName, serverName->getText(), 256);
			multiplayersJoin->serverName[255]=0;
		}
		else if (source==playerName)
		{
			strncpy(multiplayersJoin->playerName, playerName->getText(), 32);
			multiplayersJoin->playerName[31]=0;
		}
		else
			assert(false);
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CONNECT)
		{
			multiplayersJoin->tryConnection(false);
		}
		else if (par1==QUIT)
		{
			multiplayersJoin->quitThisGame();
			endExecute(QUIT);
		}
		else if (par1==-1)
		{
			multiplayersJoin->quitThisGame();
			endExecute(-1);
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
	else if (action==LIST_ELEMENT_SELECTED)
	{
		if (source==lanServers)
		{
			std::list<MultiplayersJoin::LANHost>::iterator it;
			int i=0;
			for (it=multiplayersJoin->lanHosts.begin(); it!=multiplayersJoin->lanHosts.end(); ++it)
				if (i==par1)
				{
					serverName->setText(Utilities::stringIP(it->ip.host));
					strncpy(multiplayersJoin->serverName, serverName->getText(), 256);
					multiplayersJoin->serverName[255]=0;
					break;
				}
				else
					i++;
		}
	}

}
