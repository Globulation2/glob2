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

#include "ChooseMapScreen.h"
#include "Engine.h"
#include <FormatableString.h>
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <GUIButton.h>
#include <GUIList.h>
#include "GUIMessageBox.h"
#include <GUITextArea.h>
#include <GUIText.h>
#include <GUITextInput.h>
#include "MultiplayerGameScreen.h"
#include <stdio.h>
#include <string.h>
#include <StringTable.h>
#include <Toolkit.h>
#include "YOGClientChatChannel.h"
#include "YOGClientCommandManager.h"
#include "YOGClientEvent.h"
#include "YOGClientGameListManager.h"
#include "YOGClient.h"
#include "YOGClientLobbyScreen.h"
#include "YOGClientPlayerListManager.h"
#include "YOGMessage.h"

YOGClientPlayerList::YOGClientPlayerList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font)
	: List(x, y, w, h, hAlign, vAlign, font)
{
	networkSprite = Toolkit::getSprite("data/gui/yog");
}



YOGClientPlayerList::~YOGClientPlayerList()
{
	Toolkit::releaseSprite("data/gui/yog");
}



void YOGClientPlayerList::addPlayer(const std::string &nick, NetworkType network)
{
	addText(nick);
	networks.push_back(network);
}



void YOGClientPlayerList::clear(void)
{
	List::clear();
	networks.clear();
}



void YOGClientPlayerList::drawItem(int x, int y, size_t element)
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



YOGClientLobbyScreen::YOGClientLobbyScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client)
	: TabScreenWindow(parent, Toolkit::getStringTable()->getString("[Lobby]")), client(client)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[yog]")));

	hostButton = new TextButton(20, 65, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[create game]"), CREATE_GAME);
	addWidget(hostButton);
	
	addWidget(new TextButton(20, 15, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), CANCEL, 27));

	gameList=new List(20, 120, 220, 140, ALIGN_FILL, ALIGN_TOP, "standard");
	addWidget(gameList);
	gameInfo=new TextArea(20, 120, 180, 95, ALIGN_RIGHT, ALIGN_TOP, "standard");
	addWidget(gameInfo);
	joinButton=new TextButton(20, 225, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[join]"), JOIN);
	addWidget(joinButton);

	playerList=new YOGClientPlayerList(20, 280, 180, 135, ALIGN_RIGHT, ALIGN_FILL, "standard");
	addWidget(playerList);

	chatWindow=new TextArea(20, 280, 220, 135, ALIGN_FILL, ALIGN_FILL, "standard", true, "", "data/gui/yog");
	addWidget(chatWindow);
	textInput=new TextInput(20, 90, 220, 25, ALIGN_FILL, ALIGN_BOTTOM, "standard", "", true, 256);
	addWidget(textInput);
	
	lobbyChat.reset(new YOGClientChatChannel(LOBBY_CHAT_CHANNEL, client));

	ircChat.reset(new IRCTextMessageHandler);
	ircChat->addTextMessageListener(this);
	ircChat->startIRC(client->getUsername());
	
	client->addEventListener(this);
	client->getGameListManager()->addListener(this);
	client->getPlayerListManager()->addListener(this);
	lobbyChat->addListener(this);
	
	gameScreen=-1;
}



YOGClientLobbyScreen::~YOGClientLobbyScreen()
{
	ircChat->removeTextMessageListener(this);
	ircChat->stopIRC();
	
	lobbyChat->removeListener(this);
	client->removeEventListener(this);
	client->getGameListManager()->removeListener(this);
	client->getPlayerListManager()->removeListener(this);
}


void YOGClientLobbyScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	TabScreenWindow::onAction(source, action, par1, par2);
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			endExecute(CANCEL);
			parent->completeEndExecute(CANCEL);
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
		if(textInput->getText() != "")
		{
			//First test if its a client command like /block
			std::string result = client->getCommandManager()->executeClientCommand(textInput->getText());
			if(!result.empty())
			{
				recieveInternalMessage(result);
				textInput->setText("");
			}
			else
			{
				boost::shared_ptr<YOGMessage> message(new YOGMessage);
				message->setSender(client->getUsername());
				message->setMessage(textInput->getText());
				message->setMessageType(YOGNormalMessage);
				lobbyChat->sendMessage(message);

				ircChat->sendCommand(textInput->getText());
				textInput->setText("");
			}
		}
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		updateGameInfo();
	}
}

void YOGClientLobbyScreen::onTimer(Uint32 tick)
{
	if(gameScreen != -1)
	{
		int rc = parent->getReturnCode(gameScreen);
		if(rc!=-1)
		{
			boost::shared_ptr<MultiplayerGame> game(client->getMultiplayerGame());
			if(rc == MultiplayerGameScreen::Kicked)
				recieveInternalMessage(Toolkit::getStringTable()->getString("[You where kicked from the game]"));
			else if(rc == MultiplayerGameScreen::GameCancelled)
				recieveInternalMessage(Toolkit::getStringTable()->getString("[The host has cancelled the game]"));
			else if(rc == MultiplayerGameScreen::GameRefused)
			{
				if(game->getGameJoinState() == YOGServerGameHasAlreadyStarted)
					recieveInternalMessage(Toolkit::getStringTable()->getString("[Can't join game, game has started]"));
				else if(game->getGameJoinState() == YOGServerGameIsFull)
					recieveInternalMessage(Toolkit::getStringTable()->getString("[Can't join game, game is full]"));
				else if(game->getGameJoinState() == YOGServerGameDoesntExist)
					recieveInternalMessage(Toolkit::getStringTable()->getString("[Can't join game, game doesn't exist]"));
				else if(game->getGameCreationState() == YOGCreateRefusalUnknown)
					recieveInternalMessage("Game was refused by server");
			}
			client->setMultiplayerGame(boost::shared_ptr<MultiplayerGame>());
			gameScreen=-1;
			updateButtonVisibility();
		}
	}

	TabScreenWindow::onTimer(tick);

	ircChat->update();
	client->update();

	if(ircChat->hasUserListBeenModified())
		updatePlayerList();
}



void YOGClientLobbyScreen::handleYOGClientEvent(boost::shared_ptr<YOGClientEvent> event)
{
	//std::cout<<"YOGClientLobbyScreen: recieved event "<<event->format()<<std::endl;
	Uint8 type = event->getEventType();
	if(type == YEConnectionLost)
	{
		GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[YESTS_CONNECTION_LOST]"), Toolkit::getStringTable()->getString("[ok]"));
		parent->completeEndExecute(ConnectionLost);
	}
	else if(type == YEPlayerBanned)
	{
		GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Your username was banned]"), Toolkit::getStringTable()->getString("[ok]"));
	}
	else if(type == YEIPBanned)
	{
		GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Your IP address was temporarily banned]"), Toolkit::getStringTable()->getString("[ok]"));
	}
}



void YOGClientLobbyScreen::handleIRCTextMessage(const std::string& message)
{
	chatWindow->addText(message);
	chatWindow->addImage(1);
	chatWindow->addText("\n");
	chatWindow->scrollToBottom();
}



void YOGClientLobbyScreen::recieveTextMessage(boost::shared_ptr<YOGMessage> message)
{
	chatWindow->addText(message->formatForReading());
	chatWindow->addImage(0);
	chatWindow->addText("\n");
	chatWindow->scrollToBottom();
}



void YOGClientLobbyScreen::recieveInternalMessage(const std::string& message)
{
	chatWindow->addText(message);
	chatWindow->addText("\n");
	chatWindow->addImage(-1);
	for(int c=0; c<message.size(); ++c)
		if(message[c] == '\n')
			chatWindow->addImage(-1);
	chatWindow->scrollToBottom();
}



void YOGClientLobbyScreen::gameListUpdated()
{
	updateGameList();
}



void YOGClientLobbyScreen::playerListUpdated()
{
	updatePlayerList();
}



void YOGClientLobbyScreen::hostGame()
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
		MultiplayerGameScreen* mgs = new MultiplayerGameScreen(parent, game, client, ircChat);
		gameScreen = mgs->getTabNumber();
		updateButtonVisibility();
		parent->activateGroup(gameScreen);
	}
	else if(rc == -1)
		endExecute(-1);
}




void YOGClientLobbyScreen::joinGame()
{
	if(gameList->getSelectionIndex() != -1)
	{
		boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
		client->setMultiplayerGame(game);
		Uint16 id = 0;
		for (std::list<YOGGameInfo>::const_iterator game=client->getGameListManager()->getGameList().begin(); game!=client->getGameListManager()->getGameList().end(); ++game)
		{
			if(gameList->get() == game->getGameName())
			{
				id = game->getGameID();
				break;
			}
		}
		game->joinGame(id);
		MultiplayerGameScreen* mgs = new MultiplayerGameScreen(parent, game, client, ircChat);
		gameScreen = mgs->getTabNumber();
		updateButtonVisibility();
		parent->activateGroup(gameScreen);

	}
}



void YOGClientLobbyScreen::updateGameList(void)
{
	int i = gameList->getSelectionIndex();
	gameList->clear();
	for (std::list<YOGGameInfo>::const_iterator game=client->getGameListManager()->getGameList().begin(); game!=client->getGameListManager()->getGameList().end(); ++game)
	{
		if(game->getGameState() == YOGGameInfo::GameOpen)
			gameList->addText(game->getGameName());
	}
	gameList->setSelectionIndex(i);

	updateGameInfo();
}



void YOGClientLobbyScreen::updatePlayerList(void)
{

//	boost::shared_ptr<IRC> irc = ircChat->getIRC();
	// update YOG one
	playerList->clear();
	for (std::list<YOGPlayerSessionInfo>::const_iterator player=client->getPlayerListManager()->getPlayerList().begin(); player!=client->getPlayerListManager()->getPlayerList().end(); ++player)
	{
		std::string listEntry = player->getPlayerName();
		playerList->addPlayer(listEntry, YOGClientPlayerList::YOG_NETWORK);
	}

	// update irc entries, remove one already on YOG
	for(unsigned i=0; i<ircChat->getUsers().size(); ++i)
	{
		const std::string &user = ircChat->getUsers()[i];
		if (user.compare(0, 5, "[YOG]") != 0)
			playerList->addPlayer(user, YOGClientPlayerList::IRC_NETWORK);
	}

}



void YOGClientLobbyScreen::updateGameInfo()
{
	if (gameList->getSelectionIndex() != -1)
	{
		for (std::list<YOGGameInfo>::const_iterator game=client->getGameListManager()->getGameList().begin(); game!=client->getGameListManager()->getGameList().end(); ++game)
		{
			if(gameList->get() == game->getGameName())
			{
				gameInfo->setText("");
				std::string s;
				s += game->getGameName() + "\n";
				gameInfo->addText(s.c_str());
				s = FormatableString(Toolkit::getStringTable()->getString("[Map name: %0]")).arg(game->getMapName()) + "\n";
				gameInfo->addText(s.c_str());
				s = FormatableString(Toolkit::getStringTable()->getString("[number of players: %0 (%1 AI)]")).arg((int)game->getPlayersJoined() + (int)game->getAIJoined()).arg((int)game->getAIJoined()) + "\n";
				gameInfo->addText(s.c_str());
				s = FormatableString(Toolkit::getStringTable()->getString("[number of teams: %0]")).arg((int)game->getNumberOfTeams()) + "\n";
				gameInfo->addText(s.c_str());
				gameInfo->addChar('\n');
			}
		}
	}
	else
	{
		gameInfo->setText("");
	}
}



void YOGClientLobbyScreen::autoCompleteNick()
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
		for (std::list<YOGPlayerSessionInfo>::const_iterator player=client->getPlayerListManager()->getPlayerList().begin(); player!=client->getPlayerListManager()->getPlayerList().end(); ++player)
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
			for(unsigned i=0; i<ircChat->getUsers().size(); ++i)
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


void YOGClientLobbyScreen::updateButtonVisibility()
{
	if(gameScreen != -1)
	{
		joinButton->visible=false;
		hostButton->visible=false;
	}
	else
	{
		joinButton->visible=true;
		hostButton->visible=true;
	}
}


void YOGClientLobbyScreen::onActivated()
{
	updateButtonVisibility();
}
