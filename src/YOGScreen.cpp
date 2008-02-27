/*
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

#include <string.h>
#include <stdio.h>
#include "YOGScreen.h"
#include "Engine.h"
#include "GlobalContainer.h"

#include <FormatableString.h>
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUITextArea.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>

#include "ChooseMapScreen.h"
#include "MultiplayerGameScreen.h"
#include "YOGGameListManager.h"

YOGPlayerList::YOGPlayerList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font)
	: List(x, y, w, h, hAlign, vAlign, font)
{
	networkSprite = Toolkit::getSprite("data/gui/yog");
}



YOGPlayerList::~YOGPlayerList()
{
	Toolkit::releaseSprite("data/gui/yog");
}



void YOGPlayerList::addPlayer(const std::string &nick, NetworkType network)
{
	addText(nick);
	networks.push_back(network);
}



void YOGPlayerList::clear(void)
{
	List::clear();
	networks.clear();
}



void YOGPlayerList::drawItem(int x, int y, size_t element)
{
	assert(networkSprite);
	if(element < getCount())
	{
		int xShift = 20;
		int spriteYShift = (textHeight-16) >> 1;
		if (networks[element] == ALL_NETWORK)
			xShift = 0;
		else if (networks[element] == YOG_NETWORK)
			parent->getSurface()->drawSprite(x, y+spriteYShift, networkSprite, 0);
		else if (networks[element] == IRC_NETWORK)
			parent->getSurface()->drawSprite(x, y+spriteYShift, networkSprite, 1);
		parent->getSurface()->drawString(x+xShift, y, fontPtr, (strings[element]).c_str());
	}
}



YOGScreen::YOGScreen(boost::shared_ptr<YOGClient> client)
	: client(client)
{

	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[yog]")));

	addWidget(new TextButton(20, 65, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[create game]"), CREATE_GAME));
	addWidget(new TextButton(20, 15, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), CANCEL, 27));

	gameList=new List(20, 50, 220, 140, ALIGN_FILL, ALIGN_TOP, "standard");
	addWidget(gameList);
	gameInfo=new TextArea(20, 50, 180, 95, ALIGN_RIGHT, ALIGN_TOP, "standard");
	addWidget(gameInfo);
	joinButton=new TextButton(20, 155, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[join]"), JOIN);
	addWidget(joinButton);

	playerList=new YOGPlayerList(20, 210, 180, 120, ALIGN_RIGHT, ALIGN_FILL, "standard");
	addWidget(playerList);

	chatWindow=new TextArea(20, 210, 220, 65, ALIGN_FILL, ALIGN_FILL, "standard", true, "", "data/gui/yog");
	addWidget(chatWindow);
	textInput=new TextInput(20, 20, 220, 25, ALIGN_FILL, ALIGN_BOTTOM, "standard", "", true, 256);
	addWidget(textInput);
	
	lobbyChat.reset(new YOGChatChannel(LOBBY_CHAT_CHANNEL, client, this));

	ircChat.reset(new IRCTextMessageHandler);
	ircChat->addTextMessageListener(this);
	ircChat->startIRC(client->getUsername());
	
	client->setEventListener(this);
	
	client->getYOGGameListManager()->addListener(this);
}



YOGScreen::~YOGScreen()
{
	ircChat->removeTextMessageListener(this);
	ircChat->stopIRC();
	
	client->getYOGGameListManager()->removeListener(this);
}


void YOGScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			endExecute(CANCEL);
		}
		else if (par1==CREATE_GAME)
		{
			hostGame();
		}
		else if (par1==JOIN)
		{
			joinGame();
		}
	}
	else if (action==TEXT_MODIFIED)
	{
		std::string message = textInput->getText();
		int msglen = message.length()-1;
		if( message[msglen] == 9 )
		{
			autoCompleteNick();
		}
	}
	else if (action==TEXT_VALIDATED)
	{
		boost::shared_ptr<YOGMessage> message(new YOGMessage);
		message->setSender(client->getUsername());
		message->setMessage(textInput->getText());
		message->setMessageType(YOGNormalMessage);
		lobbyChat->sendMessage(message);

		ircChat->sendCommand(textInput->getText());
		textInput->setText("");
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		updateGameInfo();
	}
}

void YOGScreen::onTimer(Uint32 tick)
{
	ircChat->update();
	client->update();

	if(ircChat->hasUserListBeenModified())
		updatePlayerList();
}



void YOGScreen::handleYOGEvent(boost::shared_ptr<YOGEvent> event)
{
	//std::cout<<"YOGScreen: recieved event "<<event->format()<<std::endl;
	Uint8 type = event->getEventType();
	if(type == YEConnectionLost)
	{
		endExecute(ConnectionLost);
	}
	else if(type == YEPlayerListUpdated)
	{
		updatePlayerList();
	}
}



void YOGScreen::handleIRCTextMessage(const std::string& message)
{
	chatWindow->addText(message);
	chatWindow->addImage(1);
	chatWindow->addText("\n");
	chatWindow->scrollToBottom();
}



void YOGScreen::recieveTextMessage(boost::shared_ptr<YOGMessage> message)
{
	chatWindow->addText(message->formatForReading());
	chatWindow->addImage(0);
	chatWindow->addText("\n");
	chatWindow->scrollToBottom();
}



void YOGScreen::recieveInternalMessage(const std::string& message)
{
	chatWindow->addText(message);
	chatWindow->addText("\n");
	chatWindow->addImage(-1);
	chatWindow->scrollToBottom();
}



void YOGScreen::gameListUpdated()
{
	updateGameList();
}



void YOGScreen::hostGame()
{
	ChooseMapScreen cms("maps", "map", false, "games", "game", false);
	int rc = cms.execute(globalContainer->gfx, 40);
	if(rc == ChooseMapScreen::OK)
	{
		boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
		client->setMultiplayerGame(game);
		std::string name = FormatableString(Toolkit::getStringTable()->getString("[%0's game]")).arg(client->getUsername());
		game->createNewGame(name);

		game->setMapHeader(cms.getMapHeader());

		MultiplayerGameScreen mgs(game, client, ircChat);
		int rc = mgs.execute(globalContainer->gfx, 40);
		client->setMultiplayerGame(boost::shared_ptr<MultiplayerGame>());
		if(rc == -1)
			endExecute(-1);
		else if(rc == MultiplayerGameScreen::GameRefused)
			recieveInternalMessage("Game was refused by server");
	}
	else if(rc == -1)
		endExecute(-1);
}




void YOGScreen::joinGame()
{
	if(gameList->getSelectionIndex() != -1)
	{
		boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
		client->setMultiplayerGame(game);
		Uint16 id = 0;
		for (std::list<YOGGameInfo>::const_iterator game=client->getYOGGameListManager()->getGameList().begin(); game!=client->getYOGGameListManager()->getGameList().end(); ++game)
		{
			if(gameList->get() == game->getGameName())
			{
				id = game->getGameID();
				break;
			}
		}
		game->joinGame(id);
		MultiplayerGameScreen mgs(game, client, ircChat);
		int rc = mgs.execute(globalContainer->gfx, 40);
		client->setMultiplayerGame(boost::shared_ptr<MultiplayerGame>());
		if(rc == -1)
			endExecute(-1);
		else if(rc == MultiplayerGameScreen::Kicked)
			recieveInternalMessage(Toolkit::getStringTable()->getString("[You where kicked from the game]"));
		else if(rc == MultiplayerGameScreen::GameCancelled)
			recieveInternalMessage(Toolkit::getStringTable()->getString("[The host cancelled the game]"));
		else if(rc == MultiplayerGameScreen::GameRefused)
			if(game->getGameJoinState() == YOGGameHasAlreadyStarted)
				recieveInternalMessage(Toolkit::getStringTable()->getString("[Can't join game, game has started]"));
			else if(game->getGameJoinState() == YOGGameIsFull)
				recieveInternalMessage(Toolkit::getStringTable()->getString("[Can't join game, game is full]"));
			else if(game->getGameJoinState() == YOGGameDoesntExist)
				recieveInternalMessage(Toolkit::getStringTable()->getString("[Can't join game, game doesn't exist]"));
	}
}



void YOGScreen::updateGameList(void)
{
	int i = gameList->getSelectionIndex();
	gameList->clear();
	for (std::list<YOGGameInfo>::const_iterator game=client->getYOGGameListManager()->getGameList().begin(); game!=client->getYOGGameListManager()->getGameList().end(); ++game)
	{
		if(game->getGameState() == YOGGameInfo::GameOpen)
			gameList->addText(game->getGameName());
	}
	gameList->setSelectionIndex(i);

	updateGameInfo();
}



void YOGScreen::updatePlayerList(void)
{

//	boost::shared_ptr<IRC> irc = ircChat->getIRC();
	// update YOG one
	playerList->clear();
	for (std::list<YOGPlayerInfo>::const_iterator player=client->getPlayerList().begin(); player!=client->getPlayerList().end(); ++player)
	{
		std::string listEntry = player->getPlayerName();
		playerList->addPlayer(listEntry, YOGPlayerList::YOG_NETWORK);
	}

	// update irc entries, remove one already on YOG
	for(int i=0; i<ircChat->getUsers().size(); ++i)
	{
		const std::string &user = ircChat->getUsers()[i];
		if (user.compare(0, 5, "[YOG]") != 0)
			playerList->addPlayer(user, YOGPlayerList::IRC_NETWORK);
	}

}



void YOGScreen::updateGameInfo()
{
	if (gameList->getSelectionIndex() != -1)
	{
		std::list<YOGGameInfo>::iterator i=client->getYOGGameListManager()->getGameList().begin();
		std::advance(i, gameList->getSelectionIndex());
		YOGGameInfo info=*i;

		gameInfo->setText("");
		std::string s;
		s += info.getGameName() + "\n";
		gameInfo->addText(s.c_str());
		s = FormatableString(Toolkit::getStringTable()->getString("[Map name: %0]")).arg(info.getMapName()) + "\n";
		gameInfo->addText(s.c_str());
		s = FormatableString(Toolkit::getStringTable()->getString("[number of players: %0 (%1 AI)]")).arg((int)info.getPlayersJoined() + (int)info.getAIJoined()).arg((int)info.getAIJoined()) + "\n";
		gameInfo->addText(s.c_str());
		s = FormatableString(Toolkit::getStringTable()->getString("[number of teams: %0]")).arg((int)info.getNumberOfTeams()) + "\n";
		gameInfo->addText(s.c_str());
		gameInfo->addChar('\n');
	}
	else
	{
		gameInfo->setText("");
	}
}



void YOGScreen::autoCompleteNick()
{
	std::string message = textInput->getText();
	int msglen = message.length()-1;
	std::string foundNick;
	std::string msg;
	std::string beginningOfNick;
	int startlen;
	int found = 0;

	startlen = message.rfind(' ');
	if( startlen == -1 )
	{
		startlen = 0;
	}
	beginningOfNick = message.substr(startlen, msglen);

	if( beginningOfNick.compare("") != 0 )
	{
		for (std::list<YOGPlayerInfo>::const_iterator player=client->getPlayerList().begin(); player!=client->getPlayerList().end(); ++player)
		{
			const std::string &user = (std::string)player->getPlayerName();
			if( user.find(beginningOfNick) == 0 )
			{
				foundNick = user;
				found = 1;
				break;
			}
		}


		if(found == 0)
		{
			for(int i=0; i<ircChat->getUsers().size(); ++i)
			{
				const std::string &user = ircChat->getUsers()[i];
				if( user.find(beginningOfNick) == 0 )
				{
					if (user.compare(0, 5, "[YOG]") != 0)
					{
						foundNick = user;
						found = 1;
						break;
					}
				}
			}
		}

	}
	
	if( found == 1 )
	{
		msg = foundNick;
		msg += ": ";
		textInput->setText(msg);
		textInput->setCursorPos(msg.length());
	}
}

