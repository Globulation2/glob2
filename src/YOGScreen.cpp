/*
 * Globulation 2 YOGScreen gui
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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

	createList();
}

YOGScreen::~YOGScreen()
{
	if (socket)
		SDLNet_TCP_Close(socket);
	SDLNet_FreeSocketSet(socketSet);
}

void YOGScreen::closeConnection(void)
{
	if (socket)
	{
		SDLNet_TCP_Close(socket);
		socket=NULL;
	}
}

// NOTE : I have removed the -ansi flag that prevented strcasecmp and snprintf to link
// win32 uses thoses define :
#ifdef WIN32
#define strcasecmp _strcasecmp
#define snprintf _snprintf
#endif

void YOGScreen::createList(void)
{
	IPs.clear();
	gameList->clear();

	char data[GAME_INFO_MAX_SIZE];
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

	sendString(socket, "listgames");
	while (strcasecmp(data, "end")!=0)
	{
		bool res=getString(socket, data);
		if ((!res) || (strcasecmp(data, "end")==0))
			break;
		gameList->addText(data);
	}
}

bool YOGScreen::getString(TCPsocket socket, char data[GAME_INFO_MAX_SIZE])
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

bool YOGScreen::sendString(TCPsocket socket, char *data)
{
	int len=strlen(data)+1; // add one for the terminating NULL
	int result=SDLNet_TCP_Send(socket, data, len);
	return (result==len);
}



void YOGScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
	{
		if (par1 ==3)
		{
			// TODO : some update here
			//updateList();
		}
		else
			endExecute(par1);
	}
	else if (action==TEXT_VALIDATED)
	{
		char data[GAME_INFO_MAX_SIZE];
		snprintf(data, GAME_INFO_MAX_SIZE, "say %s", textInput->text);
		sendString(socket, data);
		textInput->setText("");
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
		printf("SVR MSG : %s\n", data);
	}
}