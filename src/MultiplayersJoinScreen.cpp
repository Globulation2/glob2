/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

MultiplayersJoinScreen::MultiplayersJoinScreen()
{
	multiplayersJoin=new MultiplayersJoin(false);

	serverName=new TextInput(20, 170, 280, 30, globalContainer->standardFont, "localhost", true);
	strncpy(multiplayersJoin->serverName, serverName->text, 128);
	addWidget(serverName);

	playerName=new TextInput(20, 270, 280, 30, globalContainer->standardFont, globalContainer->settings.userName, false);
	strncpy(multiplayersJoin->playerName, playerName->text, 128);
	addWidget(playerName);

	serverText=new Text(20, 140, globalContainer->menuFont, globalContainer->texts.getString("[svr hostname]"));
	addWidget(serverText);

	playerText=new Text(20, 240, globalContainer->menuFont, globalContainer->texts.getString("[player name]"));
	addWidget(playerText);
	
	aviableGamesText=new Text(320, 100, globalContainer->menuFont, globalContainer->texts.getString("[aviable lan games]"));
	addWidget(aviableGamesText);
	
	statusText=new Text(20, 390, globalContainer->standardFont, "");
	addWidget(statusText);

	addWidget(new TextButton( 20, 420, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[connect]"), CONNECT, 13));
	addWidget(new TextButton(280, 420, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[goto main menu]"), QUIT, 27));

	lanServers=new List(320, 120, 280, 180, globalContainer->menuFont);
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
		for (it=multiplayersJoin->LANHosts.begin(); it!=multiplayersJoin->LANHosts.end(); ++it)
			lanServers->addText(it->gameName);
		lanServers->commit();
		multiplayersJoin->listHasChanged=false;
	}
	/*
	char **list;
	int length;
	if (multiplayersJoin->getList(&list, &length))
	{
		lanServers->clear();
		for (int i=0; i<length; i++)
		{
			lanServers->addText(list[i]);
			printf("JS::added list[%d]=(%s).\n", i, list[i]);
			delete[] list[i];
		}
		delete[] list;
		lanServers->commit();
	}
	*/
	
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

	if (action==TEXT_MODIFFIED)
	{
		if (source==serverName)
			strncpy(multiplayersJoin->serverName, serverName->text, 128);
		else if (source==playerName)
			strncpy(multiplayersJoin->playerName, playerName->text, 128);
		else
			assert(false);
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CONNECT)
		{
			multiplayersJoin->tryConnection();
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
			for (it=multiplayersJoin->LANHosts.begin(); it!=multiplayersJoin->LANHosts.end(); ++it)
				if (i==par1)
				{
					char s[16];
					Uint32 netHost=SDL_SwapBE32(it->ip);
					snprintf(s, 16, "%d.%d.%d.%d", (netHost>>24)&0xFF, (netHost>>16)&0xFF, (netHost>>8)&0xFF, netHost&0xFF);
					serverName->setText(s);
					strncpy(multiplayersJoin->serverName, serverName->text, 128);
					break;
				}
				else
					i++;
		}
	}

}
