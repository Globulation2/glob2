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

YOGScreen::YOGScreen()
{
	multiplayersJoin=new MultiplayersJoin(true);
	//strncpy(multiplayersJoin->serverName, "nohost", 128);
	//strncpy(multiplayersJoin->playerName, globalContainer->settings.userName, 128);

	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit]"), CANCEL, 27));
	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[create game]"), CREATE_GAME));
	addWidget(new TextButton(440, 300, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[update list]"), UPDATE_LIST));

	gameList=new List(20, 60, 600, 220, globalContainer->standardFont);
	addWidget(gameList);
	textInput=new TextInput(20, 435, 400, 25, globalContainer->standardFont, "", true);
	addWidget(textInput);
	chatWindow=new TextArea(20, 300, 400, 115, globalContainer->standardFont);
	addWidget(chatWindow);

	timerCounter=0;
}

YOGScreen::~YOGScreen()
{
	std::vector<char *>::iterator ipIt;
	for( ipIt=IPs.begin(); ipIt!=IPs.end(); ++ipIt)
	{
		delete[] (*ipIt);
	}
	delete multiplayersJoin;
}

//TCPsocket YOGScreen::socket = NULL;
//SDLNet_SocketSet YOGScreen::socketSet = NULL;
//YOG YOGScreen::yog;

// NOTE : I have removed the -ansi flag that prevented strcasecmp and snprintf to link
// win32 uses thoses define :
// NOTE angel > WIN32 use _stricmp and not _strcasecmp sorry...
#ifdef WIN32
#	define strcasecmp _stricmp
#	define snprintf _snprintf
#endif

void YOGScreen::updateList(void)
{
	std::vector<char *>::iterator ipIt;
	for( ipIt=IPs.begin(); ipIt!=IPs.end(); ++ipIt)
	{
		delete[] (*ipIt);
	}
	IPs.clear();
	gameList->clear();

	// TODO : use new yog here
	/*char data[GAME_INFO_MAX_SIZE];
	data[0]=0;

	//sendString(socket, "listenoff");
	sendString(socket, "listgames");
	while (strcasecmp(data, "end")!=0)
	{
		bool res=getString(socket, data);
		//if (!res)
		//	printf("WARNING : error in receive list for %s", data);
		if ((!res) || (strcasecmp(data, "end")==0))
			break;
		if (data[0]!=0)
			gameList->addText(data);
		else
			printf("YOG : We got null string through network during list reception, why ?\n");
	}*/
	//sendString(socket, "listenon");
	
	if (globalContainer->yog.resetGameLister())
	{
		do
		{
			const char *source=globalContainer->yog.getGameSource();
			const char *identifier=globalContainer->yog.getGameIdentifier();
			const char *version=globalContainer->yog.getGameVersion();
			const char *comment=globalContainer->yog.getGameComment();
			const char *hostname=globalContainer->yog.getGameHostname();

			char data[128];
			snprintf(data, sizeof(data), "%s : %s ver %s : %s", source, identifier, version, comment);
			gameList->addText(data);
			
			char *ip;
			int ipLen=strlen(hostname);
			ip=new char[ipLen+1];
			strncpy(ip, hostname, ipLen+1);
			IPs.push_back(ip);
		}
		while (globalContainer->yog.getNextGame());
	}
}
/*
bool YOGScreen::getString(TCPsocket socket, char data[GAME_INFO_MAX_SIZE])
{
	if (socket)
	{
		int i;
		int value;
		char c;

		i=0;
		while ( (  (value=SDLNet_TCP_Recv(socket, &c, 1)) >0) && (i<GAME_INFO_MAX_SIZE-1))
		{
			if ((c==0) || (c=='\n') || (c=='\r'))
			{
				break;
			}
			else
			{
				data[i]=c;
			}
			i++;
		}
		data[i]=0;
		if (value<=0)
			return false;
		else
			return true;
	}
	else
	{
		return false;
	}
}

bool YOGScreen::sendString(TCPsocket socket, char *data)
{
	if (socket)
	{
		int len=strlen(data)+1; // add one for the terminating NULL
		int result=SDLNet_TCP_Send(socket, data, len);
		return (result==len);
	}
	else
	{
		return false;
	}
}
*/


void YOGScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==SCREEN_CREATED)
	{
		globalContainer->yog.setChatChannel(globalContainer->texts.getString("[yog-chat]"));
		globalContainer->yog.joinChannel();
		globalContainer->yog.joinChannel(DEFAULT_GAME_CHAN);
	}
	else if (action==SCREEN_DESTROYED)
	{
		globalContainer->yog.quitChannel();
		globalContainer->yog.quitChannel(DEFAULT_GAME_CHAN);
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
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

			// quit chat
			globalContainer->yog.quitChannel();
			// host game and wait for players
			int rc=engine.initMutiplayerHost(true);
			// quit game listing
			globalContainer->yog.quitChannel(DEFAULT_GAME_CHAN);

			// execute game
			if (rc==Engine::EE_NO_ERROR)
			{
				if (engine.run()==-1)
					endExecute(EXIT);
					//run=false;
			}
			else if (rc==-1)
				endExecute(-1);

			// rejoin chat and game listing
			globalContainer->yog.joinChannel();
			globalContainer->yog.joinChannel(DEFAULT_GAME_CHAN);

			// redraw all stuff
			updateList();
			gameList->commit();
			dispatchPaint(gfxCtx);
		}
		else if (par1==UPDATE_LIST)
		{
			updateList();
			gameList->commit();
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
		//char data[GAME_INFO_MAX_SIZE];
		//snprintf(data, GAME_INFO_MAX_SIZE, "say <%s> %s", globalContainer->settings.userName, textInput->text);
		//sendString(socket, data);
		globalContainer->yog.sendCommand(textInput->text);

		chatWindow->addText("<");
		chatWindow->addText(globalContainer->settings.userName);
		chatWindow->addText("> ");
		chatWindow->addText(textInput->text);
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();

		textInput->setText("");
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		/*const char *listElement=gameList->getText(par1);
		char text[GAME_INFO_MAX_SIZE];
		strncpy(text, listElement, GAME_INFO_MAX_SIZE);
		char *token=strtok(text, ":");
		Uint32 ip;
		//ip=atoi(token);
		sscanf(token, "%x", &ip);

		ip=SDL_SwapLE32(ip);

		char s[128];
		snprintf(s, 128, "%d.%d.%d.%d", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF));

		printf("YOG : selected ip is %s\n", s);
		*/
		// we create a new screen to join this game:
		printf("YOG : Selected hostname is [%s]\n", IPs[par1]);
		strncpy(multiplayersJoin->serverName, IPs[par1], 128);
		multiplayersJoin->serverName[127]=0;
		strncpy(multiplayersJoin->playerName, globalContainer->settings.userName, 128);
		multiplayersJoin->playerName[127]=0;
		multiplayersJoin->tryConnection();
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
	//sendString(socket, "listenon");
	addUpdateRect();
}

void YOGScreen::onTimer(Uint32 tick)
{
	// update list each one or second
	if (((timerCounter++)&0x1F)==0)
	{
		updateList();
		gameList->commit();
	}

	globalContainer->yog.step();
	while (globalContainer->yog.isChatMessage())
	{
		chatWindow->addText("<");
		chatWindow->addText(globalContainer->yog.getChatMessageSource());
		chatWindow->addText("> ");
		chatWindow->addText(globalContainer->yog.getChatMessage());
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();
		globalContainer->yog.freeChatMessage();
	}

	while (globalContainer->yog.isInfoMessage())
	{
		switch (globalContainer->yog.getInfoMessageType())
		{
			case YOG::IRC_MSG_JOIN:
			{
				const char *diffusion=globalContainer->yog.getInfoMessageDiffusion();
				assert(diffusion);
				if (strncmp(diffusion, DEFAULT_GAME_CHAN, YOG::IRC_CHANNEL_SIZE)!=0)
				{
					chatWindow->addText(globalContainer->yog.getInfoMessageSource());
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
				chatWindow->addText(globalContainer->yog.getInfoMessageSource());
				chatWindow->addText(" ");
				chatWindow->addText(globalContainer->texts.getString("[has quit]"));
				chatWindow->addText("\n");
			}
			break;

			default:
			break;
		}
		globalContainer->yog.freeInfoMessage();
	}

	// the game connection part:
	multiplayersJoin->onTimer(tick);
	if (multiplayersJoin->waitingState>MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		printf("YOG::joining because state=%d.\n", multiplayersJoin->waitingState);
		MultiplayersConnectedScreen *multiplayersConnectedScreen=new MultiplayersConnectedScreen(multiplayersJoin);
		int rv=multiplayersConnectedScreen->execute(globalContainer->gfx, 20);
		if (rv==MultiplayersConnectedScreen::DISCONNECT)
		{
			printf("YOG::unable to join DISCONNECT returned.\n");
		}
		else if (rv==MultiplayersConnectedScreen::STARTED)
		{
			Engine engine;
			engine.startMultiplayer(multiplayersJoin);
			int rc=engine.run();
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

	// the YOG part:
	/*if (socket)
		if (SDLNet_CheckSockets(socketSet, 0))
		{
			char data[GAME_INFO_MAX_SIZE];
			getString(socket, data);
			if (data[0]==0)
				printf("YOG : We got null string through network, why ?\n");
			else
			{
				chatWindow->addText(data);
				chatWindow->addText("\n");
				chatWindow->scrollToBottom();
			}
		}*/
}
