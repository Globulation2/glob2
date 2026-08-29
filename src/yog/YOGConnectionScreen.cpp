// SPDX-License-Identifier: GPL-3.0-or-later

#include "YOGConnectionScreen.h"

#include <GUIAnimation.h>
#include <GUITextArea.h>
#include <StringTable.h>
#include <Toolkit.h>
#include "YOGClient.h"

using namespace GAGCore;

YOGConnectionScreen::YOGConnectionScreen(std::shared_ptr<YOGClient> client)
	: statusText(nullptr), animation(nullptr), wasConnecting(false), changeTabAgain(true), client(client)
{
}

void YOGConnectionScreen::onTimer(Uint32 tick)
{
	if(wasConnecting && !client->isConnecting())
	{
		if(!client->isConnected())
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_UNABLE_TO_CONNECT]"));
			animation->visible=false;
		}
		wasConnecting = false;
	}

	client->update();
	changeTabAgain=true;
}
