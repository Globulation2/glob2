// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ChooseMapScreen.h"
#include "FormatableString.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include "GUIMessageBox.h"
#include <GUIText.h>
#include "LANFindScreen.h"
#include "LANMenuScreen.h"
#include "MultiplayerGameScreen.h"
#include <StringTable.h>
#include <Toolkit.h>
#include "YOGClientBringup.h"
#include "YOGServer.h"

using std::shared_ptr;

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
				shared_ptr<YOGServer> server(new YOGServer(YOGAnonymousLogin, YOGSingleGame));
				if(!server->isListening())
				{
					MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, FormatableString(Toolkit::getStringTable()->getString("[Can't host game, port %0 in use]")).arg(YOG_SERVER_PORT).c_str(), Toolkit::getStringTable()->getString("[ok]"));
					endExecute(QuitMenu);
				}
				else
				{
					server->enableLANBroadcasting();
					client->attachGameServer(server);
					client->connect("127.0.0.1");
					if(LANBringup::waitForConnectionState(*client, YOGClient::WaitingForLoginInformation) != LANBringup::Result::Reached)
					{
						MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Can't connect, can't find host]"), Toolkit::getStringTable()->getString("[ok]"));
						endExecute(QuitMenu);
						return;
					}
					client->attemptLogin(globalContainer->settings.getUsername());
					if(LANBringup::waitForConnectionState(*client, YOGClient::ClientOnStandby) != LANBringup::Result::Reached)
					{
						MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Can't connect, can't find host]"), Toolkit::getStringTable()->getString("[ok]"));
						endExecute(QuitMenu);
						return;
					}

					std::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
					client->setMultiplayerGame(game);
					std::string name = FormatableString(Toolkit::getStringTable()->getString("[%0's game]")).arg(globalContainer->settings.getUsername());
					game->createNewGame(name);
					game->setMapHeader(cms.getMapHeader());

					///Fix this! While this is technically right, the chat channel should be given by the server
					Glob2TabScreen screen(true);
					MultiplayerGameScreen* mgs = new MultiplayerGameScreen(&screen, game, client);
					int rc = screen.execute(globalContainer->gfx, 40);
					client->setMultiplayerGame(std::shared_ptr<MultiplayerGame>());
					if(rc == -1)
						endExecute(-1);
					else
						endExecute(HostedGame);
					delete mgs;
				}
			}
			else if(rc == -1)
			{
				endExecute(-1);
			}
		}
		else if(par1 == QUIT)
		{
			endExecute(QuitMenu);
		}
	}
}

int LANMenuScreen::menu(void)
{
	return LANMenuScreen().execute(globalContainer->gfx, 30);
}
