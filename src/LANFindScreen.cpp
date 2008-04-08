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

#include "LANFindScreen.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIMessageBox.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>
#include "MultiplayerGameScreen.h"
#include "YOGClientGameListManager.h"

using namespace GAGGUI;

LANFindScreen::LANFindScreen()
{
	serverName=new TextInput(20, 170, 280, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "localhost", true);
	addWidget(serverName);

	playerName=new TextInput(20, 270, 280, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->getUsername(), false, 32);
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
}

LANFindScreen::~LANFindScreen()
{
}


void LANFindScreen::onTimer(Uint32 tick)
{
	listener.update();
	
	int s = lanServers->getSelectionIndex();

	lanServers->clear();
	const std::vector<LANGameInformation>& games = listener.getLANGames();
	for(unsigned i=0; i<games.size(); ++i)
	{
		lanServers->addText(games[i].getGameInformation().getGameName());
	}
	
	lanServers->setSelectionIndex(std::min(s, int(games.size()-1)));
}

void LANFindScreen::onSDLEvent(SDL_Event *event)
{

}


void LANFindScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CONNECT)
		{
			shared_ptr<YOGClient> client(new YOGClient);
			client->connect(serverName->getText());
			while(client->isConnecting())
			{
				client->update();
				SDL_Delay(50);
			}
			
			if(!client->isConnected())
			{
				MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Can't connect, can't find host]"), Toolkit::getStringTable()->getString("[ok]"));
				return;
			}
			while(client->getConnectionState() != YOGClient::WaitingForLoginInformation)
				client->update();
			client->attemptLogin(playerName->getText());
			while(client->getConnectionState() != YOGClient::ClientOnStandby)
				client->update();
				
			boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
			client->setMultiplayerGame(game);

			while (client->getGameListManager()->getGameList().size() == 0)
    			client->update();

			if((*client->getGameListManager()->getGameList().begin()).getGameState()==YOGGameInfo::GameRunning)
			{
				MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Can't join game, game has started]"), Toolkit::getStringTable()->getString("[ok]"));
				return;
			}

			game->joinGame((*client->getGameListManager()->getGameList().begin()).getGameID());

			Glob2TabScreen screen;
			MultiplayerGameScreen* mgs = new MultiplayerGameScreen(&screen, game, client);
			
			
			listener.disableListening();
			int rc = screen.execute(globalContainer->gfx, 40);
			listener.enableListening();
			client->setMultiplayerGame(boost::shared_ptr<MultiplayerGame>());
			if(rc == -1)
				endExecute(-1);
		}
		else if (par1==QUIT)
		{
			endExecute(QUIT);
		}
		else if (par1==-1)
		{
			endExecute(-1);
		}
		else
			assert(false);
	}
	else if (action==TEXT_ACTIVATED)
	{
		// we deactivate others texts inputs:
		if (source!=serverName)
			serverName->deactivate();
		if (source!=playerName)
			playerName->deactivate();
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		if (source==lanServers)
		{
			int s = lanServers->getSelectionIndex();
			serverName->setText(listener.getIPAddress(s));
		}
	}

}
