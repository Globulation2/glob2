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

YOGScreen::YOGScreen()
{
	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit]") ,0));
	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[create game]") ,1));
	addWidget(new TextButton(440, 300, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[update list]") ,3));

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
}

TCPsocket YOGScreen::socket = NULL;
SDLNet_SocketSet YOGScreen::socketSet = NULL;

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
}

void YOGScreen::closeYOG(void)
{
	if (socket)
	{
		SDLNet_TCP_Close(socket);
		socket=NULL;
		SDLNet_FreeSocketSet(socketSet);
	}
}

void YOGScreen::createConnection(void)
{
	openYOG();
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
		if (par1 ==3)
		{
			updateList();
			gameList->repaint();
		}
		else
			endExecute(par1);
	}
	else if (action==TEXT_VALIDATED)
	{
		char data[GAME_INFO_MAX_SIZE];
		snprintf(data, GAME_INFO_MAX_SIZE, "say <%s> %s", globalContainer->settings.userName, textInput->text);
		sendString(socket, data);
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
		printf("YOG : selected ip is %d.%d.%d.%d\n", ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), (ip&0xFF));
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

}

void YOGScreen::onTimer(Uint32 tick)
{
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
