// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ChooseMapScreen.h"
#include "FormatableString.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include "MessageScreen.h"
#include "LANSessionScreen.h"
#include <ScreenStack.h>
#include <GUIText.h>
#include "LANFindScreen.h"
#include "LANMenuScreen.h"
#include "MultiplayerGameScreen.h"
#include <StringTable.h>
#include <Toolkit.h>

#include "YOGServer.h"

using std::shared_ptr;

LANMenuScreen::LANMenuScreen(GAGGUI::ScreenStack& screens) : screens(screens)
{
	addWidget(new TextButton(0,  70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[host]"), HOST));
	addWidget(new TextButton(0,  130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED,  "menu", Toolkit::getStringTable()->getString("[join a game]"), JOIN));
	addWidget(new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED,  "menu", Toolkit::getStringTable()->getString("[goto main menu]"), QUIT, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[lan]")));
}

LANMenuScreen::~LANMenuScreen()
{
}

void LANMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if(par1 == JOIN)
		{
            screens.push(std::make_unique<LANFindScreen>(screens),
                [this](GAGGUI::Screen&, int) { endExecute(JoinedGame); });
        }
        else if(par1 == HOST)
        {
            screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false, "games", "game", false),
                [this](GAGGUI::Screen& selection, int result) {
                    if (result != ChooseMapScreen::OK) return;
                    auto client = std::make_shared<YOGClient>();
                    auto server = std::make_shared<YOGServer>(YOGAnonymousLogin, YOGSingleGame);
                    if (!server->isListening()) {
                        screens.push(std::make_unique<MessageScreen>(FormattableString(Toolkit::getStringTable()->getString("[Can't host game, port %0 in use]")).arg(YOG_SERVER_PORT),
                            std::vector<std::string>{Toolkit::getStringTable()->getString("[ok]")}));
                        return;
                    }
                    server->enableLANBroadcasting();
                    client->attachGameServer(server);
                    client->connect("127.0.0.1");
                    screens.push(std::make_unique<LANSessionScreen>(screens, client, globalContainer->settings.getUsername(),
                        static_cast<ChooseMapScreen&>(selection).getMapHeader()),
                        [this](GAGGUI::Screen&, int) { endExecute(HostedGame); });
                });
        }
		else if(par1 == QUIT)
		{
			endExecute(QuitMenu);
		}
	}
}
