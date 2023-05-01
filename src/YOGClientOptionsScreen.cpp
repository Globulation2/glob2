/*
  Copyright (C) 2008 Bradley Arsenault

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

#include <algorithm>
#include <GUIButton.h>
#include <GUIList.h>
#include "GUITabScreen.h"
#include <GUIText.h>
#include <GUITextInput.h>
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGClientBlockedList.h"
#include "YOGClient.h"
#include "YOGClientOptionsScreen.h"

using namespace GAGCore;

YOGClientOptionsScreen::YOGClientOptionsScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client)
	: TabScreenWindow(parent, Toolkit::getStringTable()->getString("[Options]")), client(client)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Options]")));
	blockedPlayers = new List(50, 200, 150, 200, ALIGN_LEFT, ALIGN_TOP, "standard");
	blockedPlayersText = new Text(50, 180, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Blocked Players]"));
	removeBlockedPlayer = new TextButton(230, 200, 100, 40, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Remove]"), REMOVE_BLOCKED_PLAYER);
	addBlockedPlayerText = new TextInput(230, 250, 100, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
	addBlockedPlayer = new TextButton(230, 285, 100, 40, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Add]"), ADD_BLOCKED_PLAYER);
	addWidget(new TextButton(20, 15, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), QUIT, 27));

	
	addWidget(blockedPlayers);
	addWidget(blockedPlayersText);
	addWidget(removeBlockedPlayer);
	addWidget(addBlockedPlayerText);
	addWidget(addBlockedPlayer);
}



void YOGClientOptionsScreen::onActivated()
{
	updateBlockedPlayerList();
}



void YOGClientOptionsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	TabScreenWindow::onAction(source, action, par1, par2);
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==QUIT)
		{
			endExecute(QUIT);
			parent->completeEndExecute(QUIT);
		}
		if(par1 == REMOVE_BLOCKED_PLAYER)
		{
			updateBlockedPlayerRemove();
		}
		if(par1 == ADD_BLOCKED_PLAYER)
		{
			updateBlockedPlayerAdd();
		}
	}
	else if(action == TEXT_VALIDATED)
	{
		if(source == addBlockedPlayerText)
		{
			updateBlockedPlayerAdd();
		}
	}
}



void YOGClientOptionsScreen::updateBlockedPlayerList()
{
	int n = blockedPlayers->getSelectionIndex();
	blockedPlayers->clear();
	const std::set<std::string>& blocked =  client->getBlockedList()->getBlockedPlayers();
	for(std::set<std::string>::const_iterator i = blocked.begin(); i!=blocked.end(); ++i)
	{
		blockedPlayers->addText(*i);
	}
	
	blockedPlayers->setSelectionIndex(std::min(int(blocked.size())-1, n));
}


void YOGClientOptionsScreen::updateBlockedPlayerAdd()
{
	std::string name = addBlockedPlayerText->getText();
	if(!name.empty() && !client->getBlockedList()->isPlayerBlocked(name))
	{
		client->getBlockedList()->addBlockedPlayer(name);
		blockedPlayers->addText(name);
		client->getBlockedList()->save();
	}
	addBlockedPlayerText->setText("");
}


void YOGClientOptionsScreen::updateBlockedPlayerRemove()
{
	if(blockedPlayers->getSelectionIndex()!=-1)
	{
		std::string name = blockedPlayers->get();
		client->getBlockedList()->removeBlockedPlayer(name);
		int n = blockedPlayers->getSelectionIndex();
		blockedPlayers->removeText(blockedPlayers->getSelectionIndex());
		blockedPlayers->setSelectionIndex(std::min(int(blockedPlayers->getCount())-1, n));
		client->getBlockedList()->save();
	}
}

