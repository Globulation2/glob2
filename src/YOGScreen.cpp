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
	
	gameList=new List(20, 60, 600, 220, globalContainer->standardFont);
	addWidget(gameList);
	textInput=new TextInput(20, 435, 400, 25, globalContainer->standardFont, "", true);
	addWidget(textInput);
	chatWindow=new TextArea(20, 300, 400, 115, globalContainer->standardFont);
	addWidget(chatWindow);

	timerCounter=0;
	selectedGameInfo=NULL;
}

YOGScreen::~YOGScreen()
{
	delete multiplayersJoin;
	if (selectedGameInfo)
		delete selectedGameInfo;
}


void YOGScreen::updateList(void)
{
	gameList->clear();
	for (std::list<YOG::GameInfo>::iterator game=globalContainer->yog->games.begin(); game!=globalContainer->yog->games.end(); ++game)
		gameList->addText(game->name);
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
			updateList();
			gameList->commit();
			dispatchPaint(gfxCtx);
			globalContainer->yog->unshareGame();
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
		/*
		chatWindow->addText("<");
		chatWindow->addText(globalContainer->settings.userName);
		chatWindow->addText("> ");
		chatWindow->addText(textInput->text);
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();*/
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
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 18, globalContainer->menuFont, text);
	}
	addUpdateRect();
}

void YOGScreen::onTimer(Uint32 tick)
{
	// update list each one or second
	if (globalContainer->yog->newGameList(true))
	{
		updateList();
		gameList->commit();
	}

	globalContainer->yog->step();
	while (globalContainer->yog->isMessage())
	{
		chatWindow->addText("<");
		chatWindow->addText(globalContainer->yog->getMessageSource());
		chatWindow->addText("> ");
		chatWindow->addText(globalContainer->yog->getMessage());
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();
		globalContainer->yog->freeMessage();
	}
	
	/*zzz
	while (globalContainer->yog->isInfoMessage())
	{
		switch (globalContainer->yog->getInfoMessageType())
		{
			case YOG::IRC_MSG_JOIN:
			{
				const char *diffusion=globalContainer->yog->getInfoMessageDiffusion();
				assert(diffusion);
				if (strncmp(diffusion, DEFAULT_GAME_CHAN, YOG::IRC_CHANNEL_SIZE)!=0)
				{
					chatWindow->addText(globalContainer->yog->getInfoMessageSource());
					chatWindow->addText(" ");
					chatWindow->addText(globalContainer->texts.getString("[has joined]"));
					chatWindow->addText(" ");
					chatWindow->addText(diffusion);
					chatWindow->addText("\n");
				}
			}
			break;

			case YOG::IRC_MSG_QUIT:
			{
				chatWindow->addText(globalContainer->yog->getInfoMessageSource());
				chatWindow->addText(" ");
				chatWindow->addText(globalContainer->texts.getString("[has quit]"));
				chatWindow->addText("\n");
			}
			break;

			default:
			break;
		}
		globalContainer->yog->freeInfoMessage();
	}*/

	// the game connection part:
	multiplayersJoin->onTimer(tick);
	if (multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		printf("YOG::joining because state=%d.\n", multiplayersJoin->waitingState);
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 50);
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
		updateList();
		gameList->commit();
		dispatchPaint(gfxCtx);
		delete multiplayersConnectedScreen;
	}
}
