/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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
	multiplayersJoin=new MultiplayersJoin();
	strncpy(multiplayersJoin->serverName, "nohost", 128);
	strncpy(multiplayersJoin->playerName, globalContainer->settings.userName, 128);

	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit]"), CANCEL));
	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[create game]"), CREATE_GAME));
	addWidget(new TextButton(440, 300, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[update list]"), UPDATE_LIST));

	gameList=new List(20, 60, 600, 220, globalContainer->standardFont);
	addWidget(gameList);
	textInput=new TextInput(20, 435, 400, 25, globalContainer->standardFont, "", true);
	addWidget(textInput);
	chatWindow=new TextArea(20, 300, 400, 115, globalContainer->standardFont);
	addWidget(chatWindow);
}

YOGScreen::~YOGScreen()
{
	closeYOG();
	delete multiplayersJoin;
}

TCPsocket YOGScreen::socket = NULL;
SDLNet_SocketSet YOGScreen::socketSet = NULL;
YOG YOGScreen::yog;

// NOTE : I have removed the -ansi flag that prevented strcasecmp and snprintf to link
// win32 uses thoses define :
// NOTE angel > WIN32 use _stricmp and not _strcasecmp sorry... 
#ifdef WIN32
#	define strcasecmp _stricmp
#	define snprintf _snprintf
#endif

void YOGScreen::openYOG(void)
{
	IPaddress ip;
	socketSet=SDLNet_AllocSocketSet(1);
	if(SDLNet_ResolveHost(&ip, globalContainer->metaServerName, globalContainer->metaServerPort)==-1)
	{
		fprintf(stderr, "YOG : ResolveHost: %s\n", SDLNet_GetError());
		return;
	}

	socket=SDLNet_TCP_Open(&ip);
	if(!socket)
	{
		fprintf(stderr, "YOG : TCP_Open: %s\n", SDLNet_GetError());
		return;
	}

	SDLNet_TCP_AddSocket(socketSet, socket);

	yog.connect("irc.debian.org", 6667, "nct");
}

void YOGScreen::closeYOG(void)
{
	if (socket)
	{
		SDLNet_TCP_Close(socket);
		socket=NULL;
		SDLNet_FreeSocketSet(socketSet);
	}
	yog.forceDisconnect();
}

void YOGScreen::createConnection(void)
{
	//openYOG();
	yog.connect("irc.debian.org", 6667, "nct");
	updateList();
}

void YOGScreen::updateList(void)
{
	IPs.clear();
	gameList->clear();

	char data[GAME_INFO_MAX_SIZE];
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
	}
	//sendString(socket, "listenon");
}

void YOGScreen::closeConnection(void)
{
	closeYOG();
}

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



void YOGScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_RELEASED)
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
			int rc=engine.initMutiplayerHost(true);
			if (rc==Engine::EE_NO_ERROR)
			{
				if (engine.run()==-1)
					endExecute(EXIT);
					//run=false;
			}
			else if (rc==-1)
				endExecute(-1);
			updateList();
			gameList->repaint();
			dispatchPaint(gfxCtx);
		}
		else if (par1==UPDATE_LIST)
		{
			updateList();
			gameList->repaint();
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
		char data[GAME_INFO_MAX_SIZE];
		snprintf(data, GAME_INFO_MAX_SIZE, "say <%s> %s", globalContainer->settings.userName, textInput->text);
		sendString(socket, data);
		yog.sendCommand(textInput->text);

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
		const char *listElement=gameList->getText(par1);
		char text[GAME_INFO_MAX_SIZE];
		strncpy(text, listElement, GAME_INFO_MAX_SIZE);
		char *token=strtok(text, ":");
		Uint32 ip;
		//ip=atoi(token);
		sscanf(token, "%x", &ip);
		
		ip=SDL_SwapLE32(ip); //TODO: YOG should work in BigEndian.

		char s[128];
		snprintf(s, 128, "%d.%d.%d.%d", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF));
		
		printf("YOG : selected ip is %s\n", s);
		
		// we create a new screen to join this game:
		strncpy(multiplayersJoin->serverName, s, 128);
		strncpy(multiplayersJoin->playerName, globalContainer->settings.userName, 128);
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
	sendString(socket, "listenon");
	addUpdateRect();
}

void YOGScreen::onTimer(Uint32 tick)
{
	yog.step();
	while (yog.isChatMessage())
	{
		chatWindow->addText("<");
		chatWindow->addText(yog.getChatMessageSource());
		chatWindow->addText("> ");
		chatWindow->addText(yog.getChatMessage());
		chatWindow->addText("\n");
		chatWindow->scrollToBottom();
		yog.freeChatMessage();
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
			multiplayersJoin->init();
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
		gameList->repaint();
		dispatchPaint(gfxCtx);
		delete multiplayersConnectedScreen;
	}
	
	// the YOG part:
	if (socket)
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
		}
}
