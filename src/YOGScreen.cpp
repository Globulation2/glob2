/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#define IRC_CHAN "#glob2"
#define IRC_SERVER "irc.globulation2.org"

IRC* ircPtr = NULL;

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
		
	executionMode=0;
	
	irc.connect(IRC_SERVER, 6667, globalContainer->getUsername());
	irc.joinChannel(IRC_CHAN);
	irc.setChatChannel(IRC_CHAN);
	ircPtr = & irc;
}



YOGScreen::~YOGScreen()
{
	irc.disconnect();
	ircPtr = NULL;
}


void YOGScreen::updateGameList(void)
{
	///only update if the list has changed
	if(client->hasGameListChanged())
	{
		gameList->clear();
		for (std::list<YOGGameInfo>::const_iterator game=client->getGameList().begin(); game!=client->getGameList().end(); ++game)
			gameList->addText(game->getGameName());
	}
}

void YOGScreen::updatePlayerList(void)
{
	///only update if the list has changed
	if(client->hasPlayerListChanged() || irc.isChannelUserBeenModified())
	{
		// update YOG one
		playerList->clear();
		for (std::list<YOGPlayerInfo>::const_iterator player=client->getPlayerList().begin(); player!=client->getPlayerList().end(); ++player)
		{
			std::string listEntry = player->getPlayerName();
			playerList->addPlayer(listEntry, YOGPlayerList::YOG_NETWORK);
		}
		// update irc entries, remove one already on YOG
		if (irc.initChannelUserListing(IRC_CHAN))
		{
			while (irc.isMoreChannelUser())
			{
				const std::string &user = irc.getNextChannelUser();
				if (user.compare(0, 5, "[YOG]") != 0)
					playerList->addPlayer(user, YOGPlayerList::IRC_NETWORK);
			}
		}
	}
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
		else if (par1==-1)
		{
			executionMode=-1;
		}
		else
			assert(false);
	}
	
	else if (action==TEXT_MODIFIED)
	{
		std::string message = textInput->getText();
		int msglen = message.length()-1;
		if( message[msglen] == 9 )
		{
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

				if(irc.initChannelUserListing(IRC_CHAN) && found == 0)
				{
					while (irc.isMoreChannelUser())
					{
						const std::string &user = irc.getNextChannelUser();
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
	}
	else if (action==TEXT_VALIDATED)
	{
		boost::shared_ptr<YOGMessage> message(new YOGMessage);
		message->setSender(client->getUsername());
		message->setMessage(textInput->getText());
		message->setMessageType(YOGNormalMessage);
		client->sendMessage(message);
		irc.sendCommand(textInput->getText());
		textInput->setText("");
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		updateGameInfo();
	}
}

void YOGScreen::onTimer(Uint32 tick)
{
	// IRC
	irc.step();
	
	while (irc.isChatMessage())
	{
		chatWindow->addText("<");
		chatWindow->addText(irc.getChatMessageSource());
		chatWindow->addText("> ");
		chatWindow->addText(irc.getChatMessage());
		chatWindow->addImage(1);
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();
		irc.freeChatMessage();
	}
	
	while (irc.isInfoMessage())
	{
		chatWindow->addText(irc.getInfoMessageSource());
		
		switch (irc.getInfoMessageType())
		{
			case IRC::IRC_MSG_JOIN:
			chatWindow->addText(" has joined irc channel ");
			break;
			
			case IRC::IRC_MSG_PART:
			chatWindow->addText(" has left irc channel ");
			break;
			
			case IRC::IRC_MSG_QUIT:
			chatWindow->addText(" has quitted irc, reason");
			break;
			
			case IRC::IRC_MSG_MODE:
			chatWindow->addText(" has set mode of ");
			break;
			
			case IRC::IRC_MSG_NOTICE:
			if (irc.getInfoMessageSource()[0])
				chatWindow->addText(" noticed ");
			else
				chatWindow->addText("Notice ");
			break;
			
			default:
			chatWindow->addText(" has sent an unhandled IRC Info Message:");
			break;
		}
		
		if (irc.getInfoMessageDiffusion() != "")
		{
			chatWindow->addText(irc.getInfoMessageDiffusion());
		}
		
		if (irc.getInfoMessageText() != "")
		{
			chatWindow->addText(" : " );
			chatWindow->addText(irc.getInfoMessageText());
		}
		
		chatWindow->addImage(1);
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();
		irc.freeInfoMessage();
	}
	
	// YOG and IRC
	client->update();
	updateGameList();
	updatePlayerList();
	
	//yog->step(); this yog->step() is allready done in multiplayersJoin instance.
	boost::shared_ptr<YOGMessage> message = client->getMessage();
	while (message)
	{
		switch(message->getMessageType())
		{
		case YOGNormalMessage:
			chatWindow->addText("<");
			chatWindow->addText(message->getSender());
			chatWindow->addText("> ");
			chatWindow->addText(message->getMessage());
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YOGPrivateMessage:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[from:]"));
			chatWindow->addText(message->getSender());
			chatWindow->addText("> ");
			chatWindow->addText(message->getMessage());
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YOGAdministratorMessage:
			chatWindow->addText("[");
			chatWindow->addText(message->getSender());
			chatWindow->addText("] ");
			chatWindow->addText(message->getMessage());
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		default:
			assert(false);
		break;
		}
		message = client->getMessage();
	}
	
/*	
	if (yog->connectionLost)
	{
		endExecute(CANCEL);
	}
*/
}



void YOGScreen::hostGame()
{
	ChooseMapScreen cms("maps", "map", false);
	int rc = cms.execute(globalContainer->gfx, 40);
	if(rc == ChooseMapScreen::OK)
	{
		boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
		client->setMultiplayerGame(game);
		game->createNewGame("New Game");
		game->setMapHeader(cms.getMapHeader());
		MultiplayerGameScreen mgs(game, &irc);
		mgs.execute(globalContainer->gfx, 40);
	}
/*
	Engine engine;
	// host game and wait for players
	int rc=engine.initMutiplayerHost(true);
	// execute game
	if (rc==Engine::EE_NO_ERROR)
	{
		irc.leaveChannel(IRC_CHAN);
		client->gameHasStarted();
		if (engine.run()==-1)
			executionMode=-1;
		client->gameHasFinished();
		irc.joinChannel(IRC_CHAN);
	}
	else if (rc==-1)
		executionMode=-1;
	// redraw all stuff
	updateGameList();
	updatePlayerList();
	client->removeGame();
*/
}




void YOGScreen::joinGame()
{
	if(gameList->getSelectionIndex() != -1)
	{
		boost::shared_ptr<MultiplayerGame> game(new MultiplayerGame(client));
		client->setMultiplayerGame(game);
		Uint16 id = 0;
		for (std::list<YOGGameInfo>::const_iterator game=client->getGameList().begin(); game!=client->getGameList().end(); ++game)
		{
			if(gameList->get() == game->getGameName())
			{
				id = game->getGameID();
				break;
			}
		}
		game->joinGame(id);
		MultiplayerGameScreen mgs(game, &irc);
		mgs.execute(globalContainer->gfx, 40);
	}
}



void YOGScreen::updateGameInfo()
{
/*
	if (gameList->getSelectionIndex())
	{
		YOGGameInfo info=*std::advance(client->getGameList().begin(), gameList->getSelectionIndex());
		std::string s;
		s += info.getGameName();
		gameInfo->setText(s.c_str());
		gameInfo->addChar('\n');
	}
	else
	{
		gameInfo->setText("");
	}
*/
}

