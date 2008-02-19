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

#include "LANMenuScreen.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "LANFindScreen.h"
#include "MultiplayerGameScreen.h"
#include "ChooseMapScreen.h"
#include "FormatableString.h"
#include "GUIMessageBox.h"

LANMenuScreen::LANMenuScreen()
{
	addWidget(new TextButton(0,  70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[host]"), HOST));
	addWidget(new TextButton(0,  130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED,  "menu", Toolkit::getStringTable()->getString("[join a game]"), JOIN));
	addWidget(new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED,  "menu", Toolkit::getStringTable()->getString("[goto main menu]"), QUIT, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[lan]")));
}

LANMenuScreen::~LANMenuScreen()
{
	/*delete font;
	delete arch;*/
}

void LANMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if(par1 == JOIN)
		{
			LANFindScreen lanfs;
			int rc = lanfs.execute(globalContainer->gfx, 40);
			if(rc==-1)
				endExecute(-1);
			else
				endExecute(JoinedGame);
		}
		else if(par1 == HOST)
		{
			ChooseMapScreen cms("maps", "map", false, "games", "game", false);
			int rc = cms.execute(globalContainer->gfx, 40);
			if(rc == ChooseMapScreen::OK)
			{
				shared_ptr<YOGClient> client(new YOGClient);
				shared_ptr<YOGGameServer> server(new YOGGameServer(YOGAnonymousLogin, YOGSingleGame));
				if(!server->isListening())
				{
					MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, FormatableString(Toolkit::getStringTable()->getString("[Can't host game, port %0 in use]")).arg(YOG_SERVER_PORT).c_str(), Toolkit::getStringTable()->getString("[ok]"));
				}
				else
				{
					server->enableLANBroadcasting();
					client->attachGameServer(server);
					client->connect("127.0.0.1");
					while(client->getConnectionState() != YOGClient::WaitingForLoginInformation)
						client->update();
					client->attemptLogin(globalContainer->getUsername());
					while(client->getConnectionState() != YOGClient::ClientOnStandby)
						client->update();
			
					boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
					client->setMultiplayerGame(game);
					std::string name = FormatableString(Toolkit::getStringTable()->getString("[%0's game]")).arg(globalContainer->getUsername());
					game->createNewGame(name);
					game->setMapHeader(cms.getMapHeader());

					///Fix this! While this is technically right, the chat channel should be given by the server
					MultiplayerGameScreen mgs(game, client);
					int rc = mgs.execute(globalContainer->gfx, 40);
					client->setMultiplayerGame(boost::shared_ptr<MultiplayerGame>());
					if(rc == -1)
						endExecute(-1);
				}
			}
			endExecute(HostedGame);
		}
		else if(par1 == QUIT)
		{
			endExecute(QuitMenu);
		}
	}
}

void LANMenuScreen::paint(int x, int y, int w, int h)
{
	gfx->drawFilledRect(x, y, w, h, 0, 0, 0);
	//gfxCtx->drawSprite(0, 0, arch, 0);
}

int LANMenuScreen::menu(void)
{
	return LANMenuScreen().execute(globalContainer->gfx, 30);
}
