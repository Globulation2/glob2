/*
 * Globulation 2 preparation gui
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "PreparationGui.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "SDL_net.h"
#include "NetConsts.h"






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
	if (action==BUTTON_PRESSED)
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
	arch=globalContainer->gfx->loadSprite("data/gui/mplayermenu");
	font=globalContainer->gfx->loadFont("data/fonts/arial8green.png");

	addWidget(new Button(250, 150, 140, 60, arch, -1, 1, HOST));
	addWidget(new Button(270, 230, 220, 60, arch, -1, 2, JOIN));
	addWidget(new Button(190, 310, 180, 60, arch, -1, 3, QUIT));

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}

MultiplayersOfferScreen::~MultiplayersOfferScreen()
{
	delete font;
	delete arch;
}

void MultiplayersOfferScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
		endExecute(par1);
}

void MultiplayersOfferScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawSprite(0, 0, arch, 0);
}

int MultiplayersOfferScreen::menu(void)
{
	return MultiplayersOfferScreen().execute(globalContainer->gfx, 30);
}



// SessionScreen pannel uniformisation part !!

SessionScreen::SessionScreen()
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

SessionScreen::~SessionScreen()
{

}

void SessionScreen::paintSessionInfo(int state)
{
	//printf("sing map, s=%d\n", state);

	gfxCtx->drawFilledRect(440, 200, 200, 14, 40,40,40);
	gfxCtx->drawString(440, 200, font, "state %d", state);

	if (startGameTimeCounter)
	{
		gfxCtx->drawFilledRect(440, 214, 200, 14, 40,40,40);
		gfxCtx->drawString(440, 214, font, "starting game...%d", startGameTimeCounter/20);
	}
	gfxCtx->drawFilledRect(0, 0, 640, (sessionInfo.numberOfPlayer+1)*14, 30,40,70);

	{
		for (int p=0; p<sessionInfo.numberOfPlayer; p++)
		{
			gfxCtx->drawString(10, 0+(14*p), font, "player %d", p);
			gfxCtx->drawString(70, 0+(14*p), font, "number %d, teamNumber %d, netState %d, ip %x, name %s, TOTL %d, cc %d",
				sessionInfo.players[p].number, sessionInfo.players[p].teamNumber, sessionInfo.players[p].netState,
				sessionInfo.players[p].ip.host, sessionInfo.players[p].name, sessionInfo.players[p].netTOTL, crossPacketRecieved[p]);

			/*gfxCtx->drawString(70, 0+(14*p), font, "type=%d, number=%d, numberMask=%d, teamNumber=%d, teamNumberMask=%d,
				ip=%x, name=%s",
				sessionInfo.players[p].type, sessionInfo.players[p].number, sessionInfo.players[p].numberMask, sessionInfo.players[p].teamNumber, sessionInfo.players[p].teamNumberMask,
				sessionInfo.players[p].ip.host, sessionInfo.players[p].name);
			*/
		}
	}

	gfxCtx->drawFilledRect(0, 480-(sessionInfo.numberOfTeam+1)*14, 640, (sessionInfo.numberOfTeam+1)*14, 30,70,40);

	{
		for (int t=0; t<sessionInfo.numberOfTeam; t++)
		{
			gfxCtx->drawString(10, 480-(14*(t+1)), font, "team %d", t);
			gfxCtx->drawString(70, 480-(14*(t+1)), font, "type=%d, teamNumber=%d, numberOfPlayer=%d, color=%d, %d, %d, playersMask=%d",
				sessionInfo.team[t].type, sessionInfo.team[t].teamNumber, sessionInfo.team[t].numberOfPlayer, sessionInfo.team[t].colorR, sessionInfo.team[t].colorG, sessionInfo.team[t].colorB, sessionInfo.team[t].playersMask);

			/*gfxCtx->drawString(70, 480-(14*(t+1)), font, "type=%d, teamNumber=%d, numberOfPlayer=%d, color=%d, playersMask=%d",
				sessionInfo.team[t].type, sessionInfo.team[t].teamNumber, sessionInfo.team[t].numberOfPlayer, sessionInfo.team[t].color, sessionInfo.team[t].playersMask);
			*/
		}
	}
}


//MultiplayersCrossConnectableScreen unification part !!

void MultiplayersCrossConnectableScreen::tryCrossConnections(void)
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
			if (crossPacketRecieved[j]<2)
			{
				if ( (sessionInfo.players[j].netState<BasePlayer::PNS_BINDED)&&(!sessionInfo.players[j].bind()) )
				{
					printf("Player %d with ip(%x, %d) is not bindable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
					sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
					sucess=false;
					break;
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
	arch=globalContainer->gfx->loadSprite("data/gui/mplayerchoosemap");
	font=globalContainer->gfx->loadFont("data/fonts/arial8green.png");

	mapName=new TextInput(400, 40, 128, 12, font, "net.map", true);
	load=new Button(230, 150, 120, 60, arch, -1, 1, LOAD);
	share=new Button(250, 230, 140, 60, arch, -1, 2, SHARE);
	cancel=new Button(290, 310, 140, 60, arch, -1, 3, CANCEL);

	addWidget(load);
	addWidget(share);
	addWidget(cancel);

	addWidget(mapName);

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}

MultiplayersChooseMapScreen::~MultiplayersChooseMapScreen()
{
	delete font;
	delete arch;
}

void MultiplayersChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
	{
		if (source==load)
		{
			printf("PGU : Loading map '%s' ...\n", mapName->text);

			SDL_RWops *stream=globalContainer->fileManager.open(mapName->text,"rb");
			if (stream==NULL)
				printf("Map '%s' not found!\n", mapName->text);
			else
			{
				validSessionInfo=sessionInfo.load(stream);
				SDL_RWclose(stream);
				if (validSessionInfo)
				{
					strncpy(sessionInfo.map.mapName, mapName->text, 32);
					sessionInfo.map.mapName[31]=0;
					printf("PGU : Map %s loaded. Inside name=%s.\n", mapName->text, sessionInfo.map.mapName);
					paintSessionInfo(0);
					addUpdateRect();
				}
				else
					printf("PGU : Warning, Error during map load\n");
			}
		}
		else if (source==share)
		{
			if (validSessionInfo)
				endExecute(SHARE);
			else
				printf("PGU : This is not a valid map!\n");
		}
		else
			endExecute(par1);
	}
}

void MultiplayersChooseMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawSprite(0, 0, arch, 0);
	gfxCtx->drawString(300, 40, font, "Map File :");

	if (validSessionInfo)
		paintSessionInfo(0);
}




//MultiplayersHostScreen pannel part !!

// This is the screen that add Players to sessionInfo.
// There are two buttons:
// -Start
// -Cancel

MultiplayersHostScreen::MultiplayersHostScreen(SessionInfo *sessionInfo)
{
	arch=globalContainer->gfx->loadSprite("data/gui/mplayerhost");
	font=globalContainer->gfx->loadFont("data/fonts/arial8green.png");

	addWidget(new Button(270, 200, 140, 60, arch, -1, 1, START));
	addWidget(new Button(210, 280, 180, 60, arch, -1, 2, CANCEL));

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	this->sessionInfo=*sessionInfo;
	validSessionInfo=true;

	// net things:
	initHostGlobalState();

	socket=NULL;
	socket=SDLNet_UDP_Open(SERVER_PORT);

	serverIP.host=0;
	serverIP.port=0;

	printf("Openning a socket...\n");
	if (socket)
	{
		printf("Socket opened at port %d.\n", SERVER_PORT);
	}
	else
	{
		printf("failed to open a socket.\n");
		return;
	}
}

MultiplayersHostScreen::~MultiplayersHostScreen()
{
	delete font;
	delete arch;

	if (destroyNet)
	{
		assert(channel==-1);
		if (channel!=-1)
		{
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			printf("Socket unbinded.\n");
		}
		if (socket)
		{
			SDLNet_UDP_Close(socket);
			socket=NULL;
			printf("Socket closed.\n");
		}
	}

}

void MultiplayersHostScreen::initHostGlobalState(void)
{
	for (int i=0; i<32; i++)
		crossPacketRecieved[i]=0;

	hostGlobalState=HGS_SHARING_SESSION_INFO;
}

void MultiplayersHostScreen::stepHostGlobalState(void)
{
	switch (hostGlobalState)
	{
	case HGS_BAD :
		printf("This is a bad hostGlobalState case. Should no happend!\n");
	break;
	case HGS_SHARING_SESSION_INFO :
	{
		bool allOK=true;
		{
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
				if (sessionInfo.players[i].netState<BasePlayer::PNS_OK)
					allOK=false;
		}

		if (allOK)
		{
			printf("OK, now we are waiting for cross connections\n");
			hostGlobalState=HGS_WAITING_CROSS_CONNECTIONS;
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
			{
				sessionInfo.players[i].netState=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START;
				if (sessionInfo.players[i].netTimeout>0)
					sessionInfo.players[i].netTimeout-=sessionInfo.players[i].netTimeoutSize-i*2;
				else
					printf("usefull\n");
				sessionInfo.players[i].netTOTL++;
			}

		}

	}
	break;
	case HGS_WAITING_CROSS_CONNECTIONS :
	{
		bool allPlayersCrossConnected=true;
		{
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			{
				if (crossPacketRecieved[j]<3)
				{
					allPlayersCrossConnected=false;
					break;
				}
			}
		}
		if (allPlayersCrossConnected && (hostGlobalState>=HGS_WAITING_CROSS_CONNECTIONS))
		{
			printf("Great, all players are cross connected, Game could start!.\n");
			hostGlobalState=HGS_ALL_PLAYERS_CROSS_CONNECTED;
		}
	}
	break;

	case HGS_ALL_PLAYERS_CROSS_CONNECTED :
	{

	}
	break;

	case HGS_GAME_START_SENDED:
	{
		bool allPlayersPlaying=true;
		{
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			{
				if (crossPacketRecieved[j]<4)
				{
					allPlayersPlaying=false;
					break;
				}
			}
		}
		if (allPlayersPlaying && (hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED))
		{
			printf("Great, all players have recieved start info.\n");
			hostGlobalState=HGS_PLAYING_COUNTER;
		}
	}
	break;

	case HGS_PLAYING_COUNTER :
	{

	}
	break;

	default:
	{
		printf("This is a bad and unknow(%d) hostGlobalState case. Should no happend!\n",hostGlobalState);
	}
	break;

	}

}

void MultiplayersHostScreen::removePlayer(int p)
{
	int t=sessionInfo.players[p].teamNumber;
	printf("player %d quited the game, from team %d.\n", p, t);
	sessionInfo.team[t].playersMask&=~sessionInfo.players[p].numberMask;
	sessionInfo.team[t].numberOfPlayer--;

	sessionInfo.players[p].netState=BasePlayer::PNS_BAD;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=DEFAULT_NETWORK_TIMEOUT;//Relase version
	sessionInfo.players[p].netTimeoutSize=0;// TODO : Only for debug version
	sessionInfo.players[p].netTOTL=0;

	sessionInfo.players[p].close();
	int mp=sessionInfo.numberOfPlayer-1;
	if (mp>p)
	{
		printf("replace it by another player: %d\n", mp);
		int mt=sessionInfo.players[mp].teamNumber;
		sessionInfo.team[mt].playersMask&=~sessionInfo.players[mp].numberMask;
		sessionInfo.team[mt].numberOfPlayer--;

		sessionInfo.players[p]=sessionInfo.players[mp];

		sessionInfo.players[p].netState=sessionInfo.players[mp].netState;
		sessionInfo.players[p].netTimeout=sessionInfo.players[mp].netTimeout;
		sessionInfo.players[p].netTimeoutSize=sessionInfo.players[mp].netTimeoutSize;
		sessionInfo.players[p].netTOTL=sessionInfo.players[mp].netTOTL;
		sessionInfo.players[p].numberMask=sessionInfo.players[mp].numberMask;

		int t=(p%sessionInfo.numberOfTeam);
		sessionInfo.players[p].setNumber(p);
		sessionInfo.players[p].setTeamNumber(t);

		sessionInfo.team[t].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.team[t].numberOfPlayer++;
	}
	sessionInfo.numberOfPlayer--;
	printf("nop %d.\n", sessionInfo.numberOfPlayer);
	// all other players are ignorant of the new situation:
	initHostGlobalState();
	{
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		{
			sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
			if (sessionInfo.players[j].netTimeout>0)
				sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
			sessionInfo.players[j].netTOTL++;
		}
	}
}

void MultiplayersHostScreen::removePlayer(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		printf("An unknow player (%x, %d) has sended a quit game !!!\n", ip.host, ip.port);
		return;
	}
	removePlayer(i);
}

void MultiplayersHostScreen::newPlayer(char *data, int size, IPaddress ip)
{
	if (size!=28)
	{
		printf("Bas size(%d) for an newPlayer request from ip %x.\n", size, ip.host);
		return;
	}

	int p=sessionInfo.numberOfPlayer;
	int t=(p)%sessionInfo.numberOfTeam;

	sessionInfo.players[p].init();
	sessionInfo.players[p].type=BasePlayer::P_IP;
	sessionInfo.players[p].setNumber(p);
	sessionInfo.players[p].setTeamNumber(t);
	memcpy(sessionInfo.players[p].name, (char *)(data+4), 16);
	sessionInfo.players[p].setip(ip);

	// we check if this player has already a connection:

	{
		for (int i=0; i<p; i++)
		{
			if (sessionInfo.players[i].sameip(ip))
			{
				printf("this ip(%x:%d) is already in the player list!\n", ip.host, ip.port);

				sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
				sessionInfo.players[i].netTimeout=0;
				sessionInfo.players[i].netTimeoutSize=LONG_NETWORK_TIMEOUT;
				sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
				return;
			}
		}
	}

	if (!sessionInfo.players[p].bind())
	{
		printf("this ip(%x:%d) is not bindable\n", ip.host, ip.host);
		return;
	}

	if (serverIP.host)
	{
		if (serverIP.host!=getUint32(data, 20))
		{
			printf("Bad ip(%x) received by(%x:%d)!\n", serverIP.host, ip.host, ip.port);
			return;
		}
		if (serverIP.port!=getUint32(data, 24))
		{
			printf("Bad port(%d) received by(%x:%d)!\n", serverIP.port, ip.host, ip.port);
			return;
		}
	}
	else
	{
		serverIP.host=getUint32(data, 20);
		serverIP.port=getUint32(data, 24);
		printf("I recived my ip!:(%x:%d).\n", serverIP.host, serverIP.port);
	}

	if ( sessionInfo.players[p].send(sessionInfo.getData(), sessionInfo.getDataLength()) )
	{
		printf("this ip(%x:%d) is added in player list.\n", ip.host, ip.port);
		sessionInfo.numberOfPlayer++;
		sessionInfo.team[sessionInfo.players[p].teamNumber].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.team[sessionInfo.players[p].teamNumber].numberOfPlayer++;
		sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		sessionInfo.players[p].netTimeout=0;
		sessionInfo.players[p].netTimeoutSize=LONG_NETWORK_TIMEOUT;
		sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL+1;

		// all other players are ignorant of the new situation:
		initHostGlobalState();
		{
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			{
				sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
				if (sessionInfo.players[j].netTimeout>0)
					sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
				sessionInfo.players[j].netTOTL++;
			}
		}
	}
}

void MultiplayersHostScreen::newHostPlayer(void)
{

}

void MultiplayersHostScreen::confirmPlayer(char *data, int size, IPaddress ip)
{
	Sint32 rcs=getSint32(data, 4);
	Sint32 lcs=sessionInfo.checkSum();

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		printf("An unknow player (%x, %d) has sended a checksum !!!\n", ip.host, ip.port);
		return;
	}

	if (rcs!=lcs)
	{
		printf("this ip(%x:%d) is has confirmed a wrong check sum !.\n", ip.host, ip.host);
		printf("rcs=%x, lcs=%x.\n", rcs, lcs);
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
		return;
	}
	else
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_CHECK_SUM;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		printf("this ip(%x) is confirmed in player list.\n", ip.host);
		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
		return;
	}
}
void MultiplayersHostScreen::confirmStartCrossConnection(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		printf("An unknow player (%x, %d) has sended a confirmStartCrossConnection !!!\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START;
		sessionInfo.players[i].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		printf("this ip(%x, %d) is start cross connection confirmed..\n", ip.host, ip.port);
		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
		return;
	}
}
void MultiplayersHostScreen::confirmStillCrossConnecting(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		printf("An unknow player (%x, %d) has sended a confirmStillCrossConnecting !!!\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState==BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START)
	{
		//sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START;
		sessionInfo.players[i].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		sessionInfo.players[i].send(SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING);
		printf("this ip(%x, %d) is continuing cross connection confirmed..\n", ip.host, ip.port);
		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
		return;
	}
}

void MultiplayersHostScreen::confirmCrossConnectionAchieved(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		printf("An unknow player (%x, %d) has sended a confirmCrossConnectionAchieved !!!\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_FINISHED_CROSS_CONNECTION;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		printf("this ip(%x, %d) is cross connection achievement confirmed..\n", ip.host, ip.port);

		crossPacketRecieved[i]=3;

		// let's check if all players are cross Connected
		stepHostGlobalState();

		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
		return;
	}
}

void MultiplayersHostScreen::confirmPlayerStartGame(char *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		printf("A player (%x, %d) has sent a bad sized confirmPlayerStartGame.\n", ip.host, ip.port);
		return;
	}

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		printf("An unknow player (%x, %d) has sent a confirmPlayerStartGame.\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_SERVER_SEND_START_GAME)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_START_GAME;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		int sgtc=data[4];
		if((abs(sgtc-startGameTimeCounter)<20) && (sgtc>0))
			startGameTimeCounter=(startGameTimeCounter*3+sgtc)/4;
			// ping=(startGameTimeCounter-sgtc)/2
			// startGameTimeCounter=(startGameTimeCounter+sgtc)/2 would be a full direct correction
			// but the division by 4 will gives a fair average ping between all players
		printf("this ip(%x, %d) confirmed start game within %d seconds.\n", ip.host, ip.port, sgtc/20);

		crossPacketRecieved[i]=4;

		// let's check if all players are playing
		stepHostGlobalState();

		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
		return;
	}
}

void MultiplayersHostScreen::treatData(char *data, int size, IPaddress ip)
{
	if ((data[1]!=0)||(data[2]!=0)||(data[3]!=0))
	{
		printf("Bad packet recieved (%d,%d,%d,%d)!\n", data[0], data[1], data[2], data[3]);
		return;
	}
	if (hostGlobalState<HGS_GAME_START_SENDED)
	{
		switch (data[0])
		{
		case NEW_PLAYER_WANTS_SESSION_INFO:
			newPlayer(data, size, ip);
		break;

		case NEW_PLAYER_SEND_CHECKSUM_CONFIRMATION:
			confirmPlayer(data, size, ip);
		break;

		case CLIENT_QUIT_NEW_GAME:
			removePlayer(data, size, ip);
		break;

		case PLAYERS_CONFIRM_START_CROSS_CONNECTIONS:
			confirmStartCrossConnection(data, size, ip);
		break;

		case PLAYERS_STILL_CROSS_CONNECTING:
			confirmStillCrossConnecting(data, size, ip);
		break;

		case PLAYERS_CROSS_CONNECTIONS_ACHIEVED:
			confirmCrossConnectionAchieved(data, size, ip);
		break;

		default:
			printf("Unknow kind of packet(%d) recieved by ip(%x:%d).\n", data[0], ip.host, ip.port);
		};
	}
	else
	{
		switch (data[0])
		{
		case PLAYER_CONFIRM_GAME_BEGINNING :
			confirmPlayerStartGame(data, size, ip);
		break;

		default:
			printf("Unknow kind of packet(%d) recieved by ip=%x.\n", data[0], ip.host);
		};
	}
}

void MultiplayersHostScreen::onTimer(Uint32 tick)
{
	if (hostGlobalState>=HGS_GAME_START_SENDED)
	{
		if (--startGameTimeCounter<0)
		{
			send(SERVER_ASK_FOR_GAME_BEGINNING, startGameTimeCounter);
			printf("Lets quit this screen and start game!\n");
			if (hostGlobalState<=HGS_GAME_START_SENDED)
			{
				// done in game: drop player.
			}
			endExecute(STARTED);
		}
		else if (startGameTimeCounter%20==0)
		{
			send(SERVER_ASK_FOR_GAME_BEGINNING, startGameTimeCounter);
			if (validSessionInfo)
				paintSessionInfo(hostGlobalState);
			addUpdateRect();
		}
	}
	else
		sendingTime();

	if (socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		if (SDLNet_UDP_Recv(socket, packet)==1)
		{
			printf("Packet recieved.\n");
			//printf("packet=%d\n", (int)packet);
			//printf("packet->channel=%d\n", packet->channel);
			//printf("packet->len=%d\n", packet->len);
			//printf("packet->maxlen=%d\n", packet->maxlen);
			//printf("packet->status=%d\n", packet->status);
			//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

			//printf("packet->data=%s\n", packet->data);

			treatData((char *)(packet->data), packet->len, packet->address);

			paintSessionInfo(hostGlobalState);
			addUpdateRect();
		}

		SDLNet_FreePacket(packet);
	}
}

bool MultiplayersHostScreen::send(const int v)
{
	//printf("Sending packet to all players (%d).\n", v);
	char data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	{
		for (int i=0; i<sessionInfo.numberOfPlayer; i++)
			sessionInfo.players[i].send(data, 4);
	}

	return true;
}
bool MultiplayersHostScreen::send(const int u, const int v)
{
	//printf("Sending packet to all players (%d;%d).\n", u, v);
	char data[8];
	data[0]=u;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=v;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	for (int i=0; i<sessionInfo.numberOfPlayer; i++)
		sessionInfo.players[i].send(data, 8);

	return true;
}

void MultiplayersHostScreen::sendingTime()
{
	bool update=false;
	if (hostGlobalState<HGS_GAME_START_SENDED)
	{
		{
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
				if (sessionInfo.players[i].netState==BasePlayer::PNS_BAD)
				{
					removePlayer(i);
					update=true;
				}

		}

		if (update)
		{

			// all other players are ignorant of the new situation:
			initHostGlobalState();
			{
				for (int j=0; j<sessionInfo.numberOfPlayer; j++)
				{
					sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
					if (sessionInfo.players[j].netTimeout>0)
						sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
					sessionInfo.players[j].netTOTL++;
				}
			}
		}
	}

	{
		for (int i=0; i<sessionInfo.numberOfPlayer; i++)
		{
			if (--sessionInfo.players[i].netTimeout<0)
			{
				update=true;
				sessionInfo.players[i].netTimeout+=sessionInfo.players[i].netTimeoutSize;

				assert(sessionInfo.players[i].netTimeoutSize);

				if (--sessionInfo.players[i].netTOTL<0)
				{
					if (hostGlobalState>=HGS_GAME_START_SENDED)
					{
						// we only drop the players, because other player are already playing.
						// will be done in the game!
					}
					else
					{
						sessionInfo.players[i].netState=BasePlayer::PNS_BAD;
						printf("Last timeout for player %d has been spent.\n", i);
					}
				}

				switch (sessionInfo.players[i].netState)
				{
				case BasePlayer::PNS_BAD :
				{
					// we remove player out of this loop, to avoid mess.
				}
				break;

				case BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST :
				{
					printf("Lets send the session info to player %d.\n", i);

					char *data=NULL;
					int size=sessionInfo.getDataLength();

					data=(char *)malloc(size+8);
					assert(data);

					data[0]=DATA_SESSION_INFO;
					data[1]=0;
					data[2]=0;
					data[3]=0;
					addSint32(data, i, 4);

					memcpy(data+8, sessionInfo.getData(), size);

					sessionInfo.players[i].send(data, size+8);
				}
				break;


				case BasePlayer::PNS_PLAYER_SEND_CHECK_SUM :
				{
					printf("Lets send the confiramtion for checksum to player %d.\n", i);
					char data[8];
					data[0]=SERVER_SEND_CHECKSUM_RECEPTION;
					data[1]=0;
					data[2]=0;
					data[3]=0;
					addSint32(data, sessionInfo.checkSum(), 4);
					sessionInfo.players[i].send(data, 8);

					// Now that's not our problem if this packet don't sucess.
					// In such a case, the client will reply.
					sessionInfo.players[i].netTimeout=0;
					sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
					sessionInfo.players[i].netState=BasePlayer::PNS_OK;

					// Lets check if all players has the sessionInfo:
					stepHostGlobalState();

					printf("player %d is know ok. (%d)\n", i, sessionInfo.players[i].netState);
				}
				break;


				case BasePlayer::PNS_OK :
				{
					if (hostGlobalState>=HGS_WAITING_CROSS_CONNECTIONS)
					{

						sessionInfo.players[i].netState=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START;
						sessionInfo.players[i].netTimeout=0;
						sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
						sessionInfo.players[i].netTOTL++;
					}
					else
						printf("Player %d is all right, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
					// players keeps ok.
				}
				break;

				case BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START :
				{
					printf("We have to inform player %d to start cross connection.\n", i);
					sessionInfo.players[i].send(PLAYERS_CAN_START_CROSS_CONNECTIONS);
				}
				break;

				case BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START :
				{
					printf("Player %d is cross connecting, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
					sessionInfo.players[i].send(PLAYERS_CAN_START_CROSS_CONNECTIONS);
				}
				break;

				case BasePlayer::PNS_PLAYER_FINISHED_CROSS_CONNECTION :
				{
					printf("We have to inform player %d that we recieved his crossConnection confirmation.\n", i);
					sessionInfo.players[i].send(SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION);

					sessionInfo.players[i].netState=BasePlayer::PNS_CROSS_CONNECTED;
				}
				break;

				case BasePlayer::PNS_CROSS_CONNECTED :
				{
					printf("Player %d is cross connected ! Yahoo !, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
				}
				break;

				case BasePlayer::PNS_SERVER_SEND_START_GAME :
				{
					printf("We send start game to player %d, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
					sessionInfo.players[i].send(SERVER_ASK_FOR_GAME_BEGINNING);
				}
				break;

				case BasePlayer::PNS_PLAYER_CONFIRMED_START_GAME :
				{
					// here we could tell other players
					printf("Player %d plays, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
				}
				break;

				default:
				{
					printf("Buggy state for player %d.\n", i);
				}

				}


			}
		}
	}

	if (update)
	{
		if (validSessionInfo)
			paintSessionInfo(hostGlobalState);
		addUpdateRect();
	}
}

void MultiplayersHostScreen::stopHosting(void)
{
	printf("Every player has one chance to get the server-quit packet.\n");
	send(SERVER_QUIT_NEW_GAME);
}

void MultiplayersHostScreen::startGame(void)
{
	printf("Lets tell all players to start game.\n");
	startGameTimeCounter=SECOND_TIMEOUT*SECONDS_BEFORE_START_GAME;
	{
		for (int i=0; i<sessionInfo.numberOfPlayer; i++)
		{
			sessionInfo.players[i].netState=BasePlayer::PNS_SERVER_SEND_START_GAME;
			sessionInfo.players[i].send(SERVER_ASK_FOR_GAME_BEGINNING, startGameTimeCounter);
		}
	}
	hostGlobalState=HGS_GAME_START_SENDED;

	// let's check if all players are playing
	stepHostGlobalState();

	if (validSessionInfo)
		paintSessionInfo(hostGlobalState);
	addUpdateRect();
}

void MultiplayersHostScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
	{
		switch (par1)
		{
		case START :
			if(hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED)
				startGame();
		break;
		case CANCEL :
			stopHosting();
			endExecute(par1);
		break;
		}
	}
	if ( (action==TEXT_MODIFFIED) && (source==mapName) )
	{
		strncpy(sessionInfo.map.mapName, mapName->text, 32);
		sessionInfo.map.mapName[31]=0;
	}
}

void MultiplayersHostScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawSprite(0, 0, arch, 0);

	if (validSessionInfo)
		paintSessionInfo(hostGlobalState);
}


//MultiplayersJoinScreen pannel part !!

// this is the screen where you choose between :
// -Cancel

MultiplayersJoinScreen::MultiplayersJoinScreen()
{
	arch=globalContainer->gfx->loadSprite("data/gui/mplayerhost");
	font=globalContainer->gfx->loadFont("data/fonts/arial8green.png");

	serverName=new TextInput(400, 170, 128, 12, font, "192.168.1.1", true);
	playerName=new TextInput(400, 150, 128, 12, font, globalContainer->settings.userName, false);

	addWidget(new Button(270, 200, 140, 60, arch, -1, 1, CONNECT));
	addWidget(new Button(210, 280, 180, 60, arch, -1, 2, QUIT));

	addWidget(serverName);
	addWidget(playerName);

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	//sessionInfo

	// net things:

	waitingState=WS_TYPING_SERVER_NAME;
	waitingTimeout=0;
	waitingTimeoutSize=0;
	waitingTOTL=0;

	startGameTimeCounter=0;
}

MultiplayersJoinScreen::~MultiplayersJoinScreen()
{
	delete font;
	delete arch;

	if (destroyNet)
	{
		if (channel!=-1)
		{
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			printf("Socket unbinded.\n");
		}
		if (socket)
		{
			SDLNet_UDP_Close(socket);
			socket=NULL;
			printf("Socket closed.\n");
		}
	}
}

void MultiplayersJoinScreen::dataSessionInfoRecieved(char *data, int size, IPaddress ip)
{
	int pn=getSint32(data, 4);

	if ((pn<0)||(pn>=32))
	{
		printf("Warning: bad dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);
		waitingTimeout=0;
		return;
	}

	myPlayerNumber=pn;
	printf("dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);


	if (size!=sessionInfo.getDataLength()+8)
	{
		printf("Bad size for a sessionInfo packet recieved!\n");
		return;
	}

	unCrossConnectSessionInfo();

	if (!sessionInfo.setData(data+8, size-8))
	{
		printf("Bad content for a sessionInfo packet recieved!\n");
		return;
	}

	validSessionInfo=true;
	waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
	waitingTimeout=0;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoinScreen::checkSumConfirmationRecieved(char *data, int size, IPaddress ip)
{
	printf("checkSumConfirmationRecieved\n");

	if (size!=8)
	{
		printf("Bad size for a checksum confirmation packet recieved!\n");
		waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}

	Sint32 rsc=getSint32(data, 4);
	Sint32 lsc=sessionInfo.checkSum();

	if (rsc!=lsc)
	{
		printf("Bad checksum confirmation packet value recieved!\n");
		waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}

	printf("Checksum confirmation packet recieved and valid!\n");

	waitingState=WS_OK;
	waitingTimeout=SHORT_NETWORK_TIMEOUT;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoinScreen::unCrossConnectSessionInfo()
{
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
	{
		if (sessionInfo.players[j].netState>=BasePlayer::PNS_BINDED)
		{
			sessionInfo.players[j].close();
			sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
		}
		crossPacketRecieved[j]=0;
	}

}

/*void MultiplayersJoinScreen::tryCrossConnections(void)
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
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (crossPacketRecieved[j]<2)
		{
			if ( (sessionInfo.players[j].netState<BasePlayer::PNS_BINDED)&&(!sessionInfo.players[j].bind()) )
			{
				printf("Player %d with ip(%x, %d) is not bindable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
				sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
				sucess=false;
				break;
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
}*/

void MultiplayersJoinScreen::startCrossConnections(void)
{
	printf("OK, we can start cross connections.\n");

	waitingTimeout=0;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	if (waitingState<WS_CROSS_CONNECTING)
	{
		waitingState=WS_CROSS_CONNECTING;
	}
	MultiplayersCrossConnectableScreen::tryCrossConnections();
	checkAllCrossConnected();
}

void MultiplayersJoinScreen::crossConnectionFirstMessage(char *data, int size, IPaddress ip)
{
	printf("crossConnectionFirstMessage\n");

	if (size!=8)
	{
		printf("Bad size for a crossConnectionFirstMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];
	printf("p=%d\n", p);

	if ((p>=0)&&(p<sessionInfo.numberOfPlayer))
	{
		if (sessionInfo.players[p].ip.host!=ip.host)
		{
			printf("Warning: crossConnectionFirstMessage packet recieved(p=%d), but from ip(%x), but should be ip(%x)!\n", p, ip.host, sessionInfo.players[p].ip.host);
		}
		else if ((sessionInfo.players[p].netState>=BasePlayer::PNS_BINDED))
		{
			if (crossPacketRecieved[p]<1)
				crossPacketRecieved[p]=1;
			printf("crossConnectionFirstMessage packet recieved (%d)\n", p);

			char data[8];
			data[0]=PLAYER_CROSS_CONNECTION_SECOND_MESSAGE;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			data[4]=myPlayerNumber;
			data[5]=0;
			data[6]=0;
			data[7]=0;
			sessionInfo.players[p].send(data, 8);

			if ((waitingState!=WS_TYPING_SERVER_NAME) && validSessionInfo)
			{
				paintSessionInfo(waitingState);
				addUpdateRect();
			}
		}
	}
	else
		printf("Dangerous crossConnectionFirstMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoinScreen::checkAllCrossConnected(void)
{
	bool allCrossConnected=true;
	{
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		{
			if (crossPacketRecieved[j]<2)
			{
				allCrossConnected=false;
				break;
			}
		}
	}
	if (allCrossConnected)
	{
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		{
			if ((sessionInfo.players[j].netState!=BasePlayer::PNS_SENDING_FIRST_PACKET)&&(sessionInfo.players[j].netState!=BasePlayer::PNS_HOST))
			{
				allCrossConnected=false;
				break;
			}
		}
	}
	if (allCrossConnected)
	{
		printf("All players are cross connected to me !!\n");
		waitingState=WS_CROSS_CONNECTING_ACHIEVED;
		if (waitingTimeout>0)
		{
			waitingTimeout-=waitingTimeoutSize;
			assert(waitingTimeoutSize);
			waitingTOTL++;
		}
	}
	if ((waitingState!=WS_TYPING_SERVER_NAME) && validSessionInfo)
		paintSessionInfo(waitingState);
	addUpdateRect();
}

void MultiplayersJoinScreen::crossConnectionSecondMessage(char *data, int size, IPaddress ip)
{
	printf("crossConnectionFirstMessage\n");

	if (size!=8)
	{
		printf("Bad size for a crossConnectionSecondMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];
	if ((p>=0)&&(p<32))
	{
		crossPacketRecieved[p]=2;
		printf("crossConnectionSecondMessage packet recieved (%d)\n", p);
		checkAllCrossConnected();
	}
	else
		printf("Dangerous crossConnectionFirstMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoinScreen::stillCrossConnectingConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_START_CONFIRMED)
	{
		printf("server(%x,%d has recieved our stillCrossConnecting state.\n", ip.host, ip.port);
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		printf("Warning: ip(%x,%d) sent us a stillCrossConnectingConfirmation while in a bad state!.\n", ip.host, ip.port);
}

void MultiplayersJoinScreen::crossConnectionsAchievedConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		printf("server(%x,%d has recieved our crossConnection achieved state.\n", ip.host, ip.port);
		waitingState=WS_CROSS_CONNECTING_SERVER_HEARD;
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		printf("Warning: ip(%x,%d) sent us a crossConnection achieved state while in a bad state!.\n", ip.host, ip.port);
}

void MultiplayersJoinScreen::serverAskForBeginning(char *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		printf("Warning: ip(%x,%d) sent us a bad serverAskForBeginning!.\n", ip.host, ip.port);
	}

	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		waitingState=WS_SERVER_START_GAME;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL+1;

		startGameTimeCounter=data[4];

		printf("Server(%x,%d) asked for game start, timecounter=%d\n", ip.host, ip.port, startGameTimeCounter);
	}
	else
		printf("Warning: ip(%x,%d) sent us a serverAskForBeginning!.\n", ip.host, ip.port);

}

void MultiplayersJoinScreen::treatData(char *data, int size, IPaddress ip)
{
	if ((data[1]!=0)||(data[2]!=0)||(data[3]!=0))
	{
		printf("Bad packet recieved (%d,%d,%d,%d)!\n", data[0], data[1], data[2], data[3]);
		return;
	}
	switch (data[0])
	{
		case DATA_SESSION_INFO :
			dataSessionInfoRecieved(data, size, ip);
		break;

		case SERVER_SEND_CHECKSUM_RECEPTION :
			checkSumConfirmationRecieved(data, size, ip);
		break;

		case SERVER_QUIT_NEW_GAME :
			if (waitingState<WS_SERVER_START_GAME)
			{
				printf("Server has quit.\n");
				waitingState=WS_TYPING_SERVER_NAME;
				Screen::paint();
				addUpdateRect();
			}
		break;

		case PLAYERS_CAN_START_CROSS_CONNECTIONS :
			startCrossConnections();
		break;

		case PLAYER_CROSS_CONNECTION_FIRST_MESSAGE :
			crossConnectionFirstMessage(data, size, ip);
		break;

		case PLAYER_CROSS_CONNECTION_SECOND_MESSAGE :
			crossConnectionSecondMessage(data, size, ip);
		break;

		case SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING :
			stillCrossConnectingConfirmation(ip);
		break;

		case SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION :
			crossConnectionsAchievedConfirmation(ip);
		break;

		case SERVER_ASK_FOR_GAME_BEGINNING :
			serverAskForBeginning(data, size, ip);
		break;

		default:
			printf("Unknow kind of packet(%d) recieved from ip(%x,%d)!\n", data[0], ip.host, ip.port);
	}
}

void MultiplayersJoinScreen::onTimer(Uint32 tick)
{
	sendingTime();

	if ((waitingState!=WS_TYPING_SERVER_NAME) && socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		if (SDLNet_UDP_Recv(socket, packet)==1)
		{
			printf("recieved packet\n");
			//printf("packet=%d\n", (int)packet);
			//printf("packet->channel=%d\n", packet->channel);
			//printf("packet->len=%d\n", packet->len);
			//printf("packet->maxlen=%d\n", packet->maxlen);
			//printf("packet->status=%d\n", packet->status);
			//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

			//printf("packet->data=%s\n", packet->data);

			treatData((char *)(packet->data), packet->len, packet->address);

			if ((waitingState!=WS_TYPING_SERVER_NAME) && validSessionInfo)
				paintSessionInfo(waitingState);
			addUpdateRect();
		}

		SDLNet_FreePacket(packet);
	}

	if (waitingState==WS_SERVER_START_GAME)
	{
		if (--startGameTimeCounter<0)
		{
			printf("Lets quit this screen and start game!\n");
			endExecute(STARTED);
		}
	}
}

void MultiplayersJoinScreen::sendingTime()
{
	if ((waitingState!=WS_TYPING_SERVER_NAME)&&(--waitingTimeout<0))
	{
		if (--waitingTOTL<0)
		{
			printf("Last TOTL spent, server has left\n");
			waitingState=WS_TYPING_SERVER_NAME;
			Screen::paint();
			Screen::addUpdateRect();
		}
		else
			printf("TOTL %d\n", waitingTOTL);

		switch (waitingState)
		{
		case WS_TYPING_SERVER_NAME:
		{
			// Nothing to send to none.
		}
		break;

		case WS_WAITING_FOR_SESSION_INFO:
		{
			if (!sendSessionInfoRequest())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_WAITING_FOR_CHECKSUM_CONFIRMATION:
		{
			if (!sendSessionInfoConfirmation())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_OK:
		{
			printf("Im ok, but I want the server to know I m still here!\n");
			if (!sendSessionInfoConfirmation())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_CROSS_CONNECTING:
		{
			printf("We tell the server that we heard about croos connection start.\n");
			if (!send(PLAYERS_CONFIRM_START_CROSS_CONNECTIONS))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
			else
			{
				// The server has to reply if he wants another confimation.
				// But for us, it's all right.
				waitingState=WS_CROSS_CONNECTING_START_CONFIRMED;
			}
		}
		break;

		case WS_CROSS_CONNECTING_START_CONFIRMED:
		{
			printf("We try cross connecting aggain:\n");
			// we have to inform the server that we are still alive:
			if (!send(PLAYERS_STILL_CROSS_CONNECTING))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
			MultiplayersCrossConnectableScreen::tryCrossConnections();
			checkAllCrossConnected();
		}
		break;

		case WS_CROSS_CONNECTING_ACHIEVED:
		{
			printf("We are cross connected.\n");
			if (!send(PLAYERS_CROSS_CONNECTIONS_ACHIEVED))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		case WS_CROSS_CONNECTING_SERVER_HEARD:
		{
			printf("Im fully cross connected and server confirmed!\n");
			if (!send(PLAYERS_CROSS_CONNECTIONS_ACHIEVED))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		case WS_SERVER_START_GAME :
		{
			printf("Starting game within %d seconds.\n", (int)(startGameTimeCounter/20));
			if (!send(PLAYER_CONFIRM_GAME_BEGINNING, startGameTimeCounter))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		default:
			printf("Im in a bad state %d!\n", waitingState);

		}

		waitingTimeout=waitingTimeoutSize;
		assert(waitingTimeoutSize);
	}
}

void MultiplayersJoinScreen::onSDLEvent(SDL_Event *event)
{

}

bool MultiplayersJoinScreen::sendSessionInfoRequest()
{

	UDPpacket *packet=SDLNet_AllocPacket(28);

	assert(packet);

	packet->channel=channel;
	packet->len=28;
	packet->data[0]=NEW_PLAYER_WANTS_SESSION_INFO;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	memset(packet->data+4, 0, 16);
	strncpy((char *)(packet->data+4), playerName->text, 16);

	memset(packet->data+20, 0, 8);
	addUint32(packet->data, serverIP.host, 20);
	addUint32(packet->data, serverIP.port, 24);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		printf("suceeded to send session request packet\n");
		//printf("packet->channel=%d\n", packet->channel);
		//printf("packet->len=%d\n", packet->len);
		//printf("packet->maxlen=%d\n", packet->maxlen);
		//printf("packet->status=%d\n", packet->status);
		//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//printf("packet->data=%s\n", packet->data);
	}
	else
	{
		printf("failed to send session request packet\n");
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=LONG_NETWORK_TIMEOUT;
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	return true;
}

bool MultiplayersJoinScreen::sendSessionInfoConfirmation()
{

	UDPpacket *packet=SDLNet_AllocPacket(8);

	assert(packet);

	packet->channel=channel;
	packet->len=8;
	packet->data[0]=NEW_PLAYER_SEND_CHECKSUM_CONFIRMATION;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	Sint32 cs=sessionInfo.checkSum();
	addSint32((char *)(packet->data), cs, 4);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		printf("suceeded to send confirmation packet\n");
		//printf("packet->channel=%d\n", packet->channel);
		//printf("packet->len=%d\n", packet->len);
		//printf("packet->maxlen=%d\n", packet->maxlen);
		//printf("packet->status=%d\n", packet->status);
		//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//printf("packet->data=%s, cs=%x\n", packet->data, cs);
	}
	else
	{
		printf("failed to send confirmation packet\n");
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=LONG_NETWORK_TIMEOUT;
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	return true;
}

bool MultiplayersJoinScreen::send(const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(4);

	assert(packet);

	packet->channel=channel;
	packet->len=4;
	packet->data[0]=v;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		printf("suceeded to send packet v=%d\n", v);
		//printf("packet->channel=%d\n", packet->channel);
		//printf("packet->len=%d\n", packet->len);
		//printf("packet->maxlen=%d\n", packet->maxlen);
		//printf("packet->status=%d\n", packet->status);
		//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//printf("packet->data=%s\n", packet->data);
	}
	else
	{
		printf("failed to send packet (%d)\n", v);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}
bool MultiplayersJoinScreen::send(const int u, const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(8);

	assert(packet);

	packet->channel=channel;
	packet->len=8;
	packet->data[0]=u;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->data[4]=v;
	packet->data[5]=0;
	packet->data[6]=0;
	packet->data[7]=0;
	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		printf("suceeded to send packet v=%d\n", v);
		//printf("packet->channel=%d\n", packet->channel);
		//printf("packet->len=%d\n", packet->len);
		//printf("packet->maxlen=%d\n", packet->maxlen);
		//printf("packet->status=%d\n", packet->status);
		//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//printf("packet->data=%s\n", packet->data);
	}
	else
	{
		printf("failed to send packet (%d)\n", v);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}

bool MultiplayersJoinScreen::tryConnection()
{
	unCrossConnectSessionInfo();

	if (channel!=-1)
	{
		send(CLIENT_QUIT_NEW_GAME);
		SDLNet_UDP_Unbind(socket, channel);
	}
	if (socket)
	{
		SDLNet_UDP_Close(socket);
		socket=NULL;
		printf("Socket closed.\n");
	}

	socket=SDLNet_UDP_Open(ANY_PORT);

	if (socket)
	{
		printf("Socket opened at port.\n");
	}
	else
	{
		printf("failed to open a socket.\n");
		return false;
	}

	if (SDLNet_ResolveHost(&serverIP, serverName->text, SERVER_PORT)==0)
	{
		printf("found serverIP.host=%x(%d)\n", serverIP.host, serverIP.host);
	}
	else
	{
		printf("failed to find adresse\n");
		return false;
	}

	channel=SDLNet_UDP_Bind(socket, -1, &serverIP);

	if (channel != -1)
	{
		printf("suceeded to bind socket\n");

		printf("serverIP.host=%x(%d)\n", serverIP.host, serverIP.host);
		printf("serverIP.port=%x(%d)\n", serverIP.port, serverIP.port);
	}
	else
	{
		printf("failed to bind socket\n");
		return false;
	}

	waitingTOTL=DEFAULT_NETWORK_TOTL-1;
	return sendSessionInfoRequest();
}

void MultiplayersJoinScreen::onAction(Widget *source, Action action, int par1, int par2)
{

	if ( (action==TEXT_MODIFFIED) && (source==serverName) )
	{
		//printf("%s\n", serverName->text);
	}
	else if (action==BUTTON_PRESSED)
	{
		if (par1==CONNECT)
		{
			if (!tryConnection())
			{
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		else
		{
			endExecute(par1);
		}
	}
	else if (action==TEXT_ACTIVATED)
	{
		// we desactivate others texts inputs:
		if (source!=serverName)
			serverName->activated=false;
		if (source!=playerName)
			playerName->activated=false;
		Screen::paint();
		addUpdateRect();
	}


}

void MultiplayersJoinScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawSprite(0, 0, arch, 0);
	gfxCtx->drawString(280, 170, font, "Server hostname :");
	gfxCtx->drawString(280, 150, font, "Player name :");
	
	if ((validSessionInfo)&&(waitingState!=WS_TYPING_SERVER_NAME))
		paintSessionInfo(waitingState);
	serverName->paint(gfxCtx);
	playerName->paint(gfxCtx);
}



