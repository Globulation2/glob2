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
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUITextArea.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>

// TODO: is it anyway to do this cleaner ?
// NOTE : I have removed the -ansi flag that prevented strcasecmp and snprintf to link

YOGScreen::YOGScreen()
{
	multiplayersJoin=new MultiplayersJoin(true);

	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[yog]")));

	addWidget(new TextButton(20, 65, 180, 25, ALIGN_RIGHT, ALIGN_BOTTOM, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[create game]"), CREATE_GAME));
	addWidget(new TextButton(20, 20, 180, 25, ALIGN_RIGHT, ALIGN_BOTTOM, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[quit]"), CANCEL, 27));

	gameList=new List(20, 50, 220, 140, ALIGN_FILL, ALIGN_TOP, "standard");
	addWidget(gameList);
	gameInfo=new TextArea(20, 50, 180, 95, ALIGN_RIGHT, ALIGN_TOP, "standard");
	addWidget(gameInfo);
	joinButton=new TextButton(20, 165, 180, 25, ALIGN_RIGHT, ALIGN_TOP, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[join]"), JOIN);
	addWidget(joinButton);

	playerList=new List(20, 210, 180, 110, ALIGN_RIGHT, ALIGN_FILL, "standard");
	addWidget(playerList);

	chatWindow=new TextArea(20, 210, 220, 65, ALIGN_FILL, ALIGN_FILL, "standard");
	addWidget(chatWindow);
	textInput=new TextInput(20, 20, 220, 25, ALIGN_FILL, ALIGN_BOTTOM, "standard", "", true, 256);
	addWidget(textInput);

	selectedGameInfo=NULL;
	
	executionMode=0;
}

YOGScreen::~YOGScreen()
{
	delete multiplayersJoin;
	if (selectedGameInfo)
		delete selectedGameInfo;
}


void YOGScreen::updateGameList(void)
{
	gameList->clear();
	for (std::list<YOG::GameInfo>::iterator game=yog->games.begin(); game!=yog->games.end(); ++game)
		gameList->addText(game->name);
	gameList->commit();
}

void YOGScreen::updatePlayerList(void)
{
	playerList->clear();
	for (std::list<YOG::Client>::iterator client=yog->clients.begin(); client!=yog->clients.end(); ++client)
	{
		if (client->playing)
		{
			if (client->away)
			{
				char s[32+4];
				snprintf(s, 32+4, "([%s])", client->userName);
				playerList->addText(s);
			}
			else
			{
				char s[32+2];
				snprintf(s, 32+2, "(%s)", client->userName);
				playerList->addText(s);
			}
		}
		else if (client->away)
		{
			char s[32+2];
			snprintf(s, 32+2, "[%s]", client->userName);
			playerList->addText(s);
		}
		else
			playerList->addText(client->userName);
	}
	playerList->commit();
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
				yog->gameStarted();
				if (engine.run()==-1)
					executionMode=-1;
				yog->gameEnded();
			}
			else if (rc==-1)
				executionMode=-1;
			// redraw all stuff
			if (yog->newGameList(true))
				updateGameList();
			if (yog->newPlayerList(true))
				updatePlayerList();
			dispatchPaint(gfxCtx);
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
	else if (action==TEXT_VALIDATED)
	{
		yog->sendMessage(textInput->getText());
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

void YOGScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	/*if (y<40)
	{
		const char *text= Toolkit::getStringTable()->getString("[yog]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 10, globalContainer->menuFont, "%s", text);
	}*/
	addUpdateRect();
}

void YOGScreen::onTimer(Uint32 tick)
{
	if (yog->newGameList(true))
		updateGameList();	
	if (yog->newPlayerList(true))
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
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_PRIVATE_MESSAGE:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[from:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_PRIVATE_RECEIPT:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[to:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_PRIVATE_RECEIPT_BUT_AWAY:
			chatWindow->addText("<");
			chatWindow->addText(Toolkit::getStringTable()->getString("[away:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_ADMIN_MESSAGE:
			chatWindow->addText("[");
			chatWindow->addText(m->userName);
			chatWindow->addText("] ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YCMT_EVENT_MESSAGE:
			chatWindow->addText(m->text);
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
		printf("YOGScreen::joining because state=%d.\n", multiplayersJoin->waitingState);
		yog->joinGame();
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 40);
		yog->unjoinGame();
		printf("YOGScreen::rv=%d\n", rv);
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			printf("YOGScreen::yog game finished DISCONNECT returned.\n");
		}
		else if (rv==MultiplayersConnectedScreen::DISCONNECTED)
		{
			printf("YOGScreen::unable to join DISCONNECTED returned.\n");
		}
		else if (rv==MultiplayersConnectedScreen::STARTED)
		{
			Engine engine;
			engine.startMultiplayer(multiplayersJoin);
			yog->gameStarted();
			int rc=engine.run();
			yog->gameEnded();
			delete multiplayersJoin;
			multiplayersJoin=new MultiplayersJoin(true);
			assert(multiplayersJoin);
			if (rc==-1)
				executionMode=-1;
			printf("YOGScreen::startMultiplayer() in join ended (rc=%d).\n", rc);
		}
		else if (rv==-1)
		{
			executionMode=-1;
		}
		else
		{
			printf("YOGScreen::critical rv=%d\n", rv);
			assert(false);
		}
		if (yog->newGameList(true))
			updateGameList();
		if (yog->newPlayerList(true))
			updatePlayerList();
		dispatchPaint(gfxCtx);
		delete multiplayersConnectedScreen;
	}
	
	if (yog->selectedGameinfoUpdated(true))
	{
		YOG::GameInfo *yogGameInfo=yog->getSelectedGameInfo();
		if (yogGameInfo)
		{
			printf("selectedGameinfoUpdated (%s)\n", yogGameInfo->mapName);
			char s[128];
			sprintf(s, Toolkit::getStringTable()->getString("[Map name: %s]"), yogGameInfo->mapName);
			gameInfo->setText(s);
			gameInfo->addChar('\n');
			
			if (yogGameInfo->numberOfPlayer==1)
			{
				sprintf(s, "%s", Toolkit::getStringTable()->getString("[one player]")),
				gameInfo->addText(s);
			}
			else
			{
				sprintf(s, Toolkit::getStringTable()->getString("[number of players: %d]"), yogGameInfo->numberOfPlayer);
				gameInfo->addText(s);
			}
			gameInfo->addChar('\n');
			
			sprintf(s, Toolkit::getStringTable()->getString("[number of teams: %d]"), yogGameInfo->numberOfTeam);
			gameInfo->addText(s);
			gameInfo->addChar('\n');
			
			if (yogGameInfo->mapGenerationMethode==MapGenerationDescriptor::eNONE)
				sprintf(s, "%s\n", Toolkit::getStringTable()->getString("[handmade map]"));
			else
				sprintf(s, "%s\n", Toolkit::getStringTable()->getString("[mapGenerationDescriptor Methodes]", yogGameInfo->mapGenerationMethode));
			gameInfo->addText(s);
			
			//TODO: display info about yogGameInfo->fileIsAMap
		}
		else
		{
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
