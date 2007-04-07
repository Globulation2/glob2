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

#include "MultiplayersJoinScreen.h"
#include "MultiplayersConnectedScreen.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>

MultiplayersJoinScreen::MultiplayersJoinScreen()
{
	multiplayersJoin=new MultiplayersJoin(false);

	serverName=new TextInput(20, 170, 280, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "localhost", true);
	strncpy(multiplayersJoin->serverName, serverName->getText().c_str(), 256);
	multiplayersJoin->serverName[255]=0;
	addWidget(serverName);

	playerName=new TextInput(20, 270, 280, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->getUsername(), false, 32);
	strncpy(multiplayersJoin->playerName, playerName->getText().c_str(), 32);
	multiplayersJoin->playerName[31]=0;
	addWidget(playerName);

	serverText=new Text(20, 145, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[svr hostname]"));
	addWidget(serverText);

	playerText=new Text(20, 245, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[player name]"));
	addWidget(playerText);

	availableGamesText=new Text(340, 55, ALIGN_SCREEN_CENTERED, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[available lan games]"));
	addWidget(availableGamesText);

	statusText=new Text(20, 390, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "");
	addWidget(statusText);

	addWidget(new TextButton( 10, 20, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[connect]"), CONNECT, 13));
	addWidget(new TextButton(330, 20, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), QUIT, 27));
	
	lanServers=new List(340, 80, 280, 100, ALIGN_SCREEN_CENTERED, ALIGN_FILL, "standard");
	addWidget(lanServers);
	
	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[join a game]")));

	wasVisible=false;

	oldStatus=MultiplayersJoin::WS_BAD; //We want redraw at first show.
}

MultiplayersJoinScreen::~MultiplayersJoinScreen()
{
	delete multiplayersJoin;
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
		multiplayersJoin->listHasChanged=false;
	}
	
	if (multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 40);
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			// do nothing
		}
		else if (rv==MultiplayersConnectedScreen::DISCONNECTED)
		{
			// do nothing
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
			strncpy(multiplayersJoin->serverName, serverName->getText().c_str(), 256);
			multiplayersJoin->serverName[255]=0;
		}
		else if (source==playerName)
		{
			strncpy(multiplayersJoin->playerName, playerName->getText().c_str(), 32);
			multiplayersJoin->playerName[31]=0;
		}
		else
			assert(false);
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CONNECT)
		{
			globalContainer->settings.tempVarPrestige = 3000;
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
			serverName->deactivate();
		if (source!=playerName)
			playerName->deactivate();
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
					strncpy(multiplayersJoin->serverName, serverName->getText().c_str(), 256);
					multiplayersJoin->serverName[255]=0;
					break;
				}
				else
					i++;
		}
	}

}
