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

#include <string.h>
#include <stdio.h>
#include "YOGScreen.h"
#include "MultiplayersConnectedScreen.h"
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

#define IRC_CHAN "#glob2"
#define IRC_SERVER "irc.globulation2.org"

// TODO: is it anyway to do this cleaner ?
IRC *ircPtr = NULL;

YOGPlayerList::YOGPlayerList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font) :
	List(x, y, w, h, hAlign, vAlign, font)
{
	networkSprite = Toolkit::getSprite("data/gui/yog");
}

YOGPlayerList::~YOGPlayerList()
{
	Toolkit::releaseSprite("data/gui/yog");
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

YOGScreen::YOGScreen()
{
	multiplayersJoin=new MultiplayersJoin(true);

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

	selectedGameInfo=NULL;
	
	executionMode=0;
	
	irc.connect(IRC_SERVER, 6667, yog->userName);
	irc.joinChannel(IRC_CHAN);
	irc.setChatChannel(IRC_CHAN);
	ircPtr = &irc;
}

YOGScreen::~YOGScreen()
{
	irc.disconnect();
	ircPtr = NULL;
	delete multiplayersJoin;
	if (selectedGameInfo)
		delete selectedGameInfo;
}
/* Should we disconnect from IRC while playing ?
YOGScreen::ircConnect(void)
{
	irc.connect(IRC_SERVER, 6667, yog->userName);
	irc.joinChannel(IRC_CHAN);
	irc.setChatChannel(IRC_CHAN);
}

YOGScreen::ircDisconnect(void)
{
	irc.disconnect();
}
*/

void YOGScreen::updateGameList(void)
{
	gameList->clear();
	for (std::list<YOG::GameInfo>::iterator game=yog->games.begin(); game!=yog->games.end(); ++game)
		gameList->addText(game->name);
}

void YOGScreen::updatePlayerList(void)
{
	// update YOG one
	playerList->clear();
	for (std::list<YOG::Client>::iterator client=yog->clients.begin(); client!=yog->clients.end(); ++client)
	{
		std::string listEntry;
		if (client->playing)
		{
			if (client->away)
			{
				listEntry = std::string("([") + client->userName + "])";
			}
			else
			{
				
				listEntry = std::string("(") + client->userName + ")";
			}
		}
		else if (client->away)
		{
			listEntry = std::string("[") + client->userName + "]";
		}
		else
			listEntry = client->userName;
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

void YOGScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			multiplayersJoin->quitThisGame();
			executionMode=CANCEL;
		}
		else if (par1==CREATE_GAME)
		{
			multiplayersJoin->quitThisGame();
			Engine engine;
			// host game and wait for players
			int rc=engine.initMutiplayerHost(true);
			// execute game
			if (rc==Engine::EE_NO_ERROR)
			{
				irc.leaveChannel(IRC_CHAN);
				yog->gameStarted();
				if (engine.run()==-1)
					executionMode=-1;
				yog->gameEnded();
				irc.joinChannel(IRC_CHAN);
			}
			else if (rc==-1)
				executionMode=-1;
			// redraw all stuff
			if (yog->newGameList(true))
				updateGameList();
			if (yog->newPlayerList(true))
				updatePlayerList();
			yog->unshareGame();
		}
		else if (par1==JOIN)
		{
			assert(source==joinButton);
			if (yog->isSelectedGame)
			{
				selectedGameInfo=new YOG::GameInfo(*yog->getSelectedGameInfo());
				multiplayersJoin->tryConnection(selectedGameInfo);
			}
		}
		else if (par1==-1)
		{
			multiplayersJoin->quitThisGame();
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
				for (std::list<YOG::Client>::iterator client=yog->clients.begin(); client!=yog->clients.end(); ++client)
				{
					const std::string &user = (std::string)client->userName;
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
		yog->sendMessage(textInput->getText());
		irc.sendCommand(textInput->getText());
		textInput->setText("");
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		//printf("YOG : LIST_ELEMENT_SELECTED\n");
		if (!yog->newGameList(false))
		{
			std::list<YOG::GameInfo>::iterator game;
			int i=0;
			for (game=yog->games.begin(); game!=yog->games.end(); ++game)
				if (i==par1)
				{
					//printf("i=%d\n", i);
					yog->selectGame(game->uid);
					assert(game!=yog->games.end());
					break;
				}
				else
					i++;
			
		}
		else
			;//TODO: a better communication system between YOG and YOGScreen!
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
	if (yog->newGameList(true))
		updateGameList();
	if (yog->newPlayerList(true) || irc.isChannelUserBeenModified())
		updatePlayerList();
	
	//yog->step(); this yog->step() is allready done in multiplayersJoin instance.
	while (yog->receivedMessages.size()>0)
	{
		std::list<YOG::Message>::iterator m=yog->receivedMessages.begin();
		switch(m->messageType)//set the text color
		{
		case YCMT_MESSAGE:
			chatWindow->addText("<");
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_PRIVATE_MESSAGE:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[from:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_PRIVATE_RECEIPT:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[to:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_PRIVATE_RECEIPT_BUT_AWAY:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[away:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_ADMIN_MESSAGE:
			chatWindow->addText("[");
			chatWindow->addText(m->userName);
			chatWindow->addText("] ");
			chatWindow->addText(m->text);
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_EVENT_MESSAGE:
			chatWindow->addText(m->text);
			chatWindow->addImage(0);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		default:
			assert(false);
		break;
		}
		
		yog->receivedMessages.erase(m);
	}
	
	multiplayersJoin->onTimer(tick);
	if ((executionMode==-1) || (executionMode==CANCEL))
	{
		assert(yog);
		if (yog->unjoiningConfirmed || yog->connectionLost)
			endExecute(executionMode);
	}
	else if ((multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO) && (yog->unjoining==false))
	{
		if (verbose)
			printf("YOGScreen::joining because state=%d.\n", multiplayersJoin->waitingState);
		yog->joinGame();
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 40);
		yog->unjoinGame(false);
		if (verbose)
			printf("YOGScreen::rv=%d\n", rv);
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			if (verbose)
				printf("YOGScreen::yog game finished DISCONNECT returned.\n");
		}
		else if (rv==MultiplayersConnectedScreen::DISCONNECTED)
		{
			if (verbose)
				printf("YOGScreen::unable to join DISCONNECTED returned.\n");
		}
		else if (rv==MultiplayersConnectedScreen::STARTED)
		{
			Engine engine;
			engine.startMultiplayer(multiplayersJoin);
			irc.leaveChannel(IRC_CHAN);
			yog->gameStarted();
			int rc=engine.run();
			yog->gameEnded();
			irc.joinChannel(IRC_CHAN);
			delete multiplayersJoin;
			multiplayersJoin=new MultiplayersJoin(true);
			assert(multiplayersJoin);
			if (rc==-1)
				executionMode=-1;
			if (verbose)
				printf("YOGScreen::startMultiplayer() in join ended (rc=%d).\n", rc);
		}
		else if (rv==-1)
		{
			executionMode=-1;
		}
		else
		{
			if (verbose)
				printf("YOGScreen::critical rv=%d\n", rv);
			assert(false);
		}
		if (yog->newGameList(true))
			updateGameList();
		if (yog->newPlayerList(true))
			updatePlayerList();
		delete multiplayersConnectedScreen;
	}
	
	if (yog->selectedGameinfoUpdated(true))
	{
		YOG::GameInfo *yogGameInfo=yog->getSelectedGameInfo();
		if (yogGameInfo)
		{
			if (verbose)
				printf("selectedGameinfoUpdated (%s)\n", yogGameInfo->mapName);
			std::string s;
			s = FormatableString(Toolkit::getStringTable()->getString("[Map name: %0]")).arg(yogGameInfo->mapName);
			gameInfo->setText(s.c_str());
			gameInfo->addChar('\n');
			
			if (yogGameInfo->numberOfPlayer==1)
			{
				gameInfo->addText(Toolkit::getStringTable()->getString("[one player]"));
			}
			else
			{
				s = FormatableString(Toolkit::getStringTable()->getString("[number of players: %0]")).arg(yogGameInfo->numberOfPlayer);
				gameInfo->addText(s);
			}
			gameInfo->addChar('\n');
			
			s = FormatableString(Toolkit::getStringTable()->getString("[number of teams: %0]")).arg(yogGameInfo->numberOfTeam);
			gameInfo->addText(s);
			gameInfo->addChar('\n');
			
			if (yogGameInfo->mapGenerationMethode==MapGenerationDescriptor::eNONE)
				s = FormatableString("%0\n").arg(Toolkit::getStringTable()->getString("[handmade map]"));
			else
				s = FormatableString("%0\n").arg(Toolkit::getStringTable()->getString("[mapGenerationDescriptor Methodes]", yogGameInfo->mapGenerationMethode));
			gameInfo->addText(s);
			
			//TODO: display info about yogGameInfo->fileIsAMap
		}
		else
		{
			if (verbose)
				printf("selectedGameinfoUpdated cleaned\n");
			gameInfo->setText("");
		}
	}
	
	if (yog->connectionLost)
	{
		multiplayersJoin->quitThisGame();
		endExecute(CANCEL);
	}
	
}
