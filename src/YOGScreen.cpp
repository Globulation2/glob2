/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <string.h>
#include <stdio.h>
#include "YOGScreen.h"
#include "GlobalContainer.h"
#include "MultiplayersConnectedScreen.h"
#include "Engine.h"

// TODO: is it anyway to do this cleaner ?
// NOTE : I have removed the -ansi flag that prevented strcasecmp and snprintf to link
// win32 uses thoses define :
// NOTE angel > WIN32 use _stricmp and not _strcasecmp sorry...
#ifdef WIN32
#	define strcasecmp _stricmp
#	define snprintf _snprintf
#endif

YOGScreen::YOGScreen()
{
	multiplayersJoin=new MultiplayersJoin(true);

	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[create game]"), CREATE_GAME));
	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit]"), CANCEL, 27));
	
	gameList=new List(20, 40, 400, 180, globalContainer->standardFont);
	addWidget(gameList);
	playerList=new List(440, 40, 180, 300, globalContainer->standardFont);
	addWidget(playerList);
	
	chatWindow=new TextArea(20, 240, 400, 175, globalContainer->standardFont);
	addWidget(chatWindow);
	textInput=new TextInput(20, 435, 400, 25, globalContainer->standardFont, "", true);
	addWidget(textInput);

	selectedGameInfo=NULL;
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
	for (std::list<YOG::GameInfo>::iterator game=globalContainer->yog->games.begin(); game!=globalContainer->yog->games.end(); ++game)
		gameList->addText(game->name);
}

void YOGScreen::updatePlayerList(void)
{
	playerList->clear();
	for (std::list<YOG::Client>::iterator client=globalContainer->yog->clients.begin(); client!=globalContainer->yog->clients.end(); ++client)
		playerList->addText(client->userName);
}

void YOGScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			multiplayersJoin->quitThisGame();
			endExecute(CANCEL);
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
				globalContainer->yog->gameStarted();
				if (engine.run()==-1)
					endExecute(EXIT);
					//run=false;
				globalContainer->yog->gameEnded();
			}
			else if (rc==-1)
				endExecute(-1);
			// redraw all stuff
			if (globalContainer->yog->newGameList(true))
			{
				updateGameList();
				gameList->commit();
			}
			if (globalContainer->yog->newPlayerList(true))
			{
				updatePlayerList();
				playerList->commit();
			}
			dispatchPaint(gfxCtx);
			globalContainer->yog->unshareGame(); // zzz Don't we stop sharing game when it start ?
		}
		else if (par1==-1)
		{
			multiplayersJoin->quitThisGame();
			endExecute(-1);
		}
		else
			assert(false);

	}
	else if (action==TEXT_VALIDATED)
	{
		globalContainer->yog->sendMessage(textInput->text);
		textInput->setText("");
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		printf("YOG : LIST_ELEMENT_SELECTED\n");
		if (!globalContainer->yog->newGameList(false))
		{
			std::list<YOG::GameInfo>::iterator game;
			int i=0;
			for (game=globalContainer->yog->games.begin(); game!=globalContainer->yog->games.end(); ++game)
				if (i==par1)
					break;
				else
					i++;
			printf("i=%d\n", i);
			if (game!=globalContainer->yog->games.end())
			{
				selectedGameInfo=new YOG::GameInfo(*game);
				multiplayersJoin->tryConnection(selectedGameInfo);
			}
		}
		else
			;//TODO: a better communication system between YOG and YOGScreen!
	}
}

void YOGScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (y<40)
	{
		char *text= globalContainer->texts.getString("[yog]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 10, globalContainer->menuFont, text);
	}
	addUpdateRect();
}

void YOGScreen::onTimer(Uint32 tick)
{
	if (globalContainer->yog->newGameList(true))
	{
		updateGameList();
		gameList->commit();
	}
	
	if (globalContainer->yog->newPlayerList(true))
	{
		updatePlayerList();
		playerList->commit();
	}
	
	globalContainer->yog->step();
	while (globalContainer->yog->receivedMessages.size()>0)
	{
		std::list<YOG::Message>::iterator m=globalContainer->yog->receivedMessages.begin();
		switch(m->messageType)//set the text color
		{
		case YMT_MESSAGE:
			chatWindow->addText("<");
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YMT_PRIVATE_MESSAGE:
			chatWindow->addText("<");
			chatWindow->addText(globalContainer->texts.getString("[from:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YMT_PRIVATE_RECEIPT:
			chatWindow->addText("<");
			chatWindow->addText(globalContainer->texts.getString("[to:]"));
			chatWindow->addText(m->userName);
			chatWindow->addText("> ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		case YMT_ADMIN_MESSAGE:
			chatWindow->addText("[");
			chatWindow->addText(m->userName);
			chatWindow->addText("] ");
			chatWindow->addText(m->text);
			chatWindow->addText("\n");
			chatWindow->scrollToBottom();
		break;
		default:
			assert(false);
		break;
		}
		
		globalContainer->yog->receivedMessages.erase(m);
	}

	// the game connection part:
	multiplayersJoin->onTimer(tick);
	if (multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		printf("YOG::joining because state=%d.\n", multiplayersJoin->waitingState);
		globalContainer->yog->joinGame();
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 50);
		globalContainer->yog->unjoinGame();
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			printf("YOG::unable to join DISCONNECT returned.\n");
		}
		else if (rv==MultiplayersConnectedScreen::STARTED)
		{
			Engine engine;
			engine.startMultiplayer(multiplayersJoin);
			globalContainer->yog->gameStarted();
			int rc=engine.run();
			globalContainer->yog->gameEnded();
			delete multiplayersJoin;
			multiplayersJoin=new MultiplayersJoin(true);
			assert(multiplayersJoin);
			if (rc==-1)
				endExecute(EXIT);
			printf("YOG::startMultiplayer() in join ended (rc=%d).\n", rc);
		}
		else if (rv==-1)
		{
			endExecute(-1);
		}
		else
		{
			printf("rv=%d\n", rv);
			assert(false);
		}
		if (globalContainer->yog->newGameList(true))
		{
			updateGameList();
			gameList->commit();
		}
		if (globalContainer->yog->newPlayerList(true))
		{
			updatePlayerList();
			playerList->commit();
		}
		dispatchPaint(gfxCtx);
		delete multiplayersConnectedScreen;
	}
}
