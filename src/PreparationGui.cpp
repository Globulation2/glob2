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

#include "PreparationGui.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "NetConsts.h"
#include "YOGScreen.h"


// MainMenuScreen pannel part !!

// this is the screen where you choose between :
// -play campain
// -
// -multiplayer
// -
// -map editor
// -quit

MainMenuScreen::MainMenuScreen()
{
	/*arch=globalContainer->gfx->loadSprite("data/gui/mainmenu");

	addWidget(new Button(30, 50, 280, 60, arch, -1, 1, 0));
	addWidget(new Button(50, 130, 280, 60, arch, -1, 2, 1));
	addWidget(new Button(90, 210, 280, 60, arch, -1, 3, 2));
	addWidget(new Button(170, 290, 280, 60, arch, -1, 4, 3));
	addWidget(new Button(330, 370, 280, 60, arch, -1, 5, 4));
	addWidget(new Button(30, 370, 140, 60, arch, -1, 6, 5));*/
	addWidget(new TextButton(150, 25, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[campagn]") ,0));
	addWidget(new TextButton(150, 90, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[custom game]") ,1));
	addWidget(new TextButton(150, 155, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[yog]") ,2));
	addWidget(new TextButton(150, 220, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[lan]") ,3));
	addWidget(new TextButton(150, 285, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[settings]") ,4));
	addWidget(new TextButton(150, 350, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[editor]") ,5));
	addWidget(new TextButton(150, 415, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit]") ,6));

	globalContainer->gfx->setClipRect();
}

MainMenuScreen::~MainMenuScreen()
{
	//delete arch;
}

void MainMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_RELEASED)
		endExecute(par1);
}

void MainMenuScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	//gfxCtx->drawSprite(0, 0, arch, 0);
}

int MainMenuScreen::menu(void)
{
	return MainMenuScreen().execute(globalContainer->gfx, 20);
}




//MultiplayersOfferScreen pannel part !!

// this is the screen where you choose berteen:
// -Join
// -Host
// -Cancel

MultiplayersOfferScreen::MultiplayersOfferScreen()
{
/*	arch=globalContainer->gfx->loadSprite("data/gui/mplayermenu");
	font=globalContainer->gfx->loadFont("data/fonts/arial8green.png");

	addWidget(new Button(250, 150, 140, 60, arch, -1, 1, HOST));
	addWidget(new Button(270, 230, 220, 60, arch, -1, 2, JOIN));
	addWidget(new Button(190, 310, 180, 60, arch, -1, 3, QUIT));*/
	addWidget(new TextButton(150, 25, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[host]"), HOST));
	addWidget(new TextButton(150, 90, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[join]"), JOIN));
	addWidget(new TextButton(150, /*155*/415, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[goto main menu]"), QUIT));

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}

MultiplayersOfferScreen::~MultiplayersOfferScreen()
{
	/*delete font;
	delete arch;*/
}

void MultiplayersOfferScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_RELEASED)
	{
		endExecute(par1);
	}
}

void MultiplayersOfferScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	//gfxCtx->drawSprite(0, 0, arch, 0);
}

int MultiplayersOfferScreen::menu(void)
{
	return MultiplayersOfferScreen().execute(globalContainer->gfx, 30);
}



// SessionConnection pannel uniformisation part !!

SessionConnection::SessionConnection()
{
	validSessionInfo=false;
	{
		for (int i=0; i<32; i++)
			crossPacketRecieved[i]=0;
	}

	socket=NULL;
	destroyNet=true;
	channel=-1;
	startGameTimeCounter=0;
	myPlayerNumber=-1;
}

SessionConnection::~SessionConnection()
{

}
/*
void SessionScreen::paintSessionInfo(int state)
{
	//printf("sing map, s=%d\n", state);

	gfxCtx->drawFilledRect(440, 200, 200, 14, 40,40,40);
	gfxCtx->drawString(440, 200, globalContainer->littleFontGreen, "state %d", state);

	if (startGameTimeCounter)
	{
		gfxCtx->drawFilledRect(440, 214, 200, 14, 40,40,40);
		gfxCtx->drawString(440, 214, globalContainer->littleFontGreen, "starting game...%d", startGameTimeCounter/20);
	}
	gfxCtx->drawFilledRect(0, 0, 640, (sessionInfo.numberOfPlayer+1)*14, 30,40,70);

	{
		for (int p=0; p<sessionInfo.numberOfPlayer; p++)
		{
			gfxCtx->drawString(10, 0+(14*p), globalContainer->littleFontGreen, "player %d", p);
			gfxCtx->drawString(70, 0+(14*p), globalContainer->littleFontGreen, "number %d, teamNumber %d, netState %d, ip %x, name %s, TOTL %d, cc %d",
				sessionInfo.players[p].number, sessionInfo.players[p].teamNumber, sessionInfo.players[p].netState,
				sessionInfo.players[p].ip.host, sessionInfo.players[p].name, sessionInfo.players[p].netTOTL, crossPacketRecieved[p]);


		}
	}

	gfxCtx->drawFilledRect(0, 480-(sessionInfo.numberOfTeam+1)*14, 640, (sessionInfo.numberOfTeam+1)*14, 30,70,40);

	{
		for (int t=0; t<sessionInfo.numberOfTeam; t++)
		{
			gfxCtx->drawString(10, 480-(14*(t+1)), globalContainer->littleFontGreen, "team %d", t);
			gfxCtx->drawString(70, 480-(14*(t+1)), globalContainer->littleFontGreen, "type=%d, teamNumber=%d, numberOfPlayer=%d, color=%d, %d, %d, playersMask=%d",
				sessionInfo.team[t].type, sessionInfo.team[t].teamNumber, sessionInfo.team[t].numberOfPlayer, sessionInfo.team[t].colorR, sessionInfo.team[t].colorG, sessionInfo.team[t].colorB, sessionInfo.team[t].playersMask);
		}
	}
}*/


//MultiplayersCrossConnectable unification part !!

void MultiplayersCrossConnectable::tryCrossConnections(void)
{
	bool sucess=true;
	char data[8];
	data[0]=PLAYER_CROSS_CONNECTION_FIRST_MESSAGE;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=myPlayerNumber;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	{
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		{
			if (crossPacketRecieved[j]<3) // NOTE: is this still usefull ?
			{
				if (sessionInfo.players[j].netState<BasePlayer::PNS_BINDED)
				{
					if (!sessionInfo.players[j].bind())
					{
						printf("Player %d with ip(%x, %d) is not bindable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
						sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
						sucess=false;
						break;
					}
				}
				sessionInfo.players[j].netState=BasePlayer::PNS_BINDED;

				if ( (sessionInfo.players[j].netState<=BasePlayer::PNS_SENDING_FIRST_PACKET)&&(!sessionInfo.players[j].send(data, 8)) )
				{
					printf("Player %d with ip(%x, %d) is not sendable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
					sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
					sucess=false;
					break;
				}
				sessionInfo.players[j].netState=BasePlayer::PNS_SENDING_FIRST_PACKET;
			}
		}
	}

}

//MultiplayersChooseMapScreen pannel part !!

// this is the screen where you choose the map name and you have the buttons:
// -Share
// -Cancel


MultiplayersChooseMapScreen::MultiplayersChooseMapScreen()
{
	ok=new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK);
	cancel=new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL);
	fileList=new List(20, 60, 200, 400, globalContainer->standardFont);
	mapPreview=new MapPreview(240, 60, "net.map");

	addWidget(new Text(20, 18, globalContainer->menuFont, globalContainer->texts.getString("[choose map]"), 600));

	addWidget(ok);
	addWidget(cancel);
	addWidget(mapPreview);

	if (globalContainer->fileManager.initDirectoryListing(".", "map"))
	{
		const char *file;
		while ((file=globalContainer->fileManager.getNextDirectoryEntry())!=NULL)
			fileList->addText(file);
	}
	addWidget(fileList);

	validSessionInfo=false;

	globalContainer->gfx->setClipRect();
}

MultiplayersChooseMapScreen::~MultiplayersChooseMapScreen()
{
}

void MultiplayersChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		const char *mapName=fileList->getText(par1);
		mapPreview->setMapThumbnail(mapName);
		printf("PGU : Loading map '%s' ...\n", mapName);
		SDL_RWops *stream=globalContainer->fileManager.open(mapName,"rb");
		if (stream==NULL)
			printf("Map '%s' not found!\n", mapName);
		else
		{
			validSessionInfo=sessionInfo.load(stream);
			SDL_RWclose(stream);
			if (validSessionInfo)
			{
				paint(388, 60, 640-388, 128);
				sessionInfo.map.mapName[31]=0;
				gfxCtx->drawString(388, 60, globalContainer->standardFont, sessionInfo.map.mapName);
				char textTemp[256];
				snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, globalContainer->texts.getString("[teams]"));
				gfxCtx->drawString(388, 90, globalContainer->standardFont, textTemp);
				addUpdateRect(388, 60, 640-388, 128);
			}
			else
				printf("PGU : Warning, Error during map load\n");
		}
	}
	else if (action==BUTTON_RELEASED)
	{
		if (source==ok)
		{
			if (validSessionInfo)
				endExecute(OK);
			else
				printf("PGU : This is not a valid map!\n");
		}
		else
			endExecute(par1);
	}
}

void MultiplayersChooseMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}








