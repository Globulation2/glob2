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

#include "MultiplayersHost.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "NetDefine.h"

MultiplayersHost::MultiplayersHost(SessionInfo *sessionInfo, bool shareOnYOG, SessionInfo *savedSessionInfo)
:MultiplayersCrossConnectable()
{
	this->sessionInfo=*sessionInfo;
	validSessionInfo=true;
	if (savedSessionInfo)
		this->savedSessionInfo=new SessionInfo(*savedSessionInfo);
	else
		this->savedSessionInfo=NULL;

	// net things:
	initHostGlobalState();

	socket=NULL;
	socket=SDLNet_UDP_Open(SERVER_PORT);

	serverIP.host=0;
	serverIP.port=0;

	NETPRINTF("Openning a socket...\n");
	if (socket)
	{
		NETPRINTF("Socket opened at port %d.\n", SERVER_PORT);
	}
	else
	{
		NETPRINTF("failed to open a socket.\n");
		return;
	}

	firstDraw=true;

	this->shareOnYOG=shareOnYOG;
	if (shareOnYOG)
	{
		globalContainer->yog.shareGame("glob2", "0.1-pre", sessionInfo->map.getMapName());
	}
}

MultiplayersHost::~MultiplayersHost()
{

	if (shareOnYOG)
	{
		globalContainer->yog.unshareGame();
	}

	if (destroyNet)
	{
		assert(channel==-1);
		if (channel!=-1)
		{
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			NETPRINTF("Socket unbinded.\n");
		}
		if (socket)
		{
			SDLNet_UDP_Close(socket);
			socket=NULL;
			NETPRINTF("Socket closed.\n");
		}
	}

	if (savedSessionInfo)
		delete savedSessionInfo;
}

void MultiplayersHost::initHostGlobalState(void)
{
	for (int i=0; i<32; i++)
		crossPacketRecieved[i]=0;

	hostGlobalState=HGS_SHARING_SESSION_INFO;
}

void MultiplayersHost::stepHostGlobalState(void)
{
	switch (hostGlobalState)
	{
	case HGS_BAD :
		NETPRINTF("This is a bad hostGlobalState case. Should not happend!\n");
	break;
	case HGS_SHARING_SESSION_INFO :
	{
		bool allOK=true;
		for (int i=0; i<sessionInfo.numberOfPlayer; i++)
			if (sessionInfo.players[i].type==BasePlayer::P_IP)
				if (sessionInfo.players[i].netState<BasePlayer::PNS_OK)
					allOK=false;
		

		if (allOK)
		{
			NETPRINTF("OK, now we are waiting for cross connections\n");
			hostGlobalState=HGS_WAITING_CROSS_CONNECTIONS;
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
				if (sessionInfo.players[i].type==BasePlayer::P_IP)
				{
					sessionInfo.players[i].netState=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START;
					if (sessionInfo.players[i].netTimeout>0)
						sessionInfo.players[i].netTimeout-=sessionInfo.players[i].netTimeoutSize-i*2;
					else
						NETPRINTF("usefull\n");
					sessionInfo.players[i].netTOTL++;
				}
		}

	}
	break;
	case HGS_WAITING_CROSS_CONNECTIONS :
	{
		bool allPlayersCrossConnected=true;
		
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			if (sessionInfo.players[j].type==BasePlayer::P_IP)
				if (crossPacketRecieved[j]<3)
				{
					NETPRINTF("player %d is not cross connected.\n", j);
					allPlayersCrossConnected=false;
					break;
				}
		
		if (allPlayersCrossConnected && (hostGlobalState>=HGS_WAITING_CROSS_CONNECTIONS))
		{
			NETPRINTF("Great, all players are cross connected, Game could start!.\n");
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
		
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			if (sessionInfo.players[j].type==BasePlayer::P_IP)
			{
				if (crossPacketRecieved[j]<4)
				{
					allPlayersPlaying=false;
					break;
				}
			}
		
		if (allPlayersPlaying && (hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED))
		{
			NETPRINTF("Great, all players have recieved start info.\n");
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
		NETPRINTF("This is a bad and unknow(%d) hostGlobalState case. Should no happend!\n",hostGlobalState);
	}
	break;

	}

}

void MultiplayersHost::kickPlayer(int p)
{
	if (sessionInfo.players[p].type==BasePlayer::P_IP)
		sessionInfo.players[p].send(SERVER_KICKED_YOU);
	removePlayer(p);
}

void MultiplayersHost::removePlayer(int p)
{
	int t=sessionInfo.players[p].teamNumber;
	NETPRINTF("player %d quited the game, from team %d.\n", p, t);
	sessionInfo.team[t].playersMask&=~sessionInfo.players[p].numberMask;
	sessionInfo.team[t].numberOfPlayer--;

	sessionInfo.players[p].netState=BasePlayer::PNS_BAD;
	sessionInfo.players[p].type=BasePlayer::P_NONE;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=DEFAULT_NETWORK_TIMEOUT;//Relase version
	sessionInfo.players[p].netTimeoutSize=0;// TODO : Only for debug version
	sessionInfo.players[p].netTOTL=0;

	sessionInfo.players[p].close();
	int mp=sessionInfo.numberOfPlayer-1;
	if (mp>p)
	{
		NETPRINTF("replace it by another player: %d\n", mp);
		int mt=sessionInfo.players[mp].teamNumber;
		sessionInfo.team[mt].playersMask&=~sessionInfo.players[mp].numberMask;
		sessionInfo.team[mt].numberOfPlayer--;

		sessionInfo.players[p]=sessionInfo.players[mp];

		sessionInfo.players[p].type=sessionInfo.players[mp].type;
		sessionInfo.players[p].netState=sessionInfo.players[mp].netState;
		sessionInfo.players[p].netTimeout=sessionInfo.players[mp].netTimeout;
		sessionInfo.players[p].netTimeoutSize=sessionInfo.players[mp].netTimeoutSize;
		sessionInfo.players[p].netTOTL=sessionInfo.players[mp].netTOTL;
		sessionInfo.players[p].numberMask=sessionInfo.players[mp].numberMask;

		//int t=(p%sessionInfo.numberOfTeam);
		int t=sessionInfo.players[mp].teamNumber;
		sessionInfo.players[p].setNumber(p);
		sessionInfo.players[p].setTeamNumber(t);
		
		// We erase replaced player:
		sessionInfo.players[mp].init();

		sessionInfo.team[t].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.team[t].numberOfPlayer++;
	}
	sessionInfo.numberOfPlayer--;
	NETPRINTF("nop %d.\n", sessionInfo.numberOfPlayer);
	// all other players are ignorant of the new situation:
	initHostGlobalState();
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
	{
		sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		if (sessionInfo.players[j].netTimeout>0)
			sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
		sessionInfo.players[j].netTOTL++;
	}
}

void MultiplayersHost::switchPlayerTeam(int p)
{
	Sint32 teamNumber=(sessionInfo.players[p].teamNumber+1)%sessionInfo.numberOfTeam;
	sessionInfo.players[p].setTeamNumber(teamNumber);
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
	{
		sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		if (sessionInfo.players[j].netTimeout>0)
			sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
		sessionInfo.players[j].netTOTL++;
	}
}

void MultiplayersHost::removePlayer(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a quit game !!!\n", ip.host, ip.port);
		return;
	}
	removePlayer(i);
}

void MultiplayersHost::newPlayer(char *data, int size, IPaddress ip)
{
	if (size!=44)
	{
		NETPRINTF("Bad size(%d) for an newPlayer request from ip %x.\n", size, ip.host);
		return;
	}
	int p=sessionInfo.numberOfPlayer;
	int t=(p)%sessionInfo.numberOfTeam;
	assert(BasePlayer::MAX_NAME_LENGTH==32);
	if (savedSessionInfo)
	{
		char playerName[BasePlayer::MAX_NAME_LENGTH];
		memcpy(playerName, (char *)(data+4), 32);
		t=savedSessionInfo->getTeamNumber(playerName, t);
	}

	sessionInfo.players[p].init();
	sessionInfo.players[p].type=BasePlayer::P_IP;
	sessionInfo.players[p].setNumber(p);
	sessionInfo.players[p].setTeamNumber(t);
	memcpy(sessionInfo.players[p].name, (char *)(data+4), 32);
	sessionInfo.players[p].setip(ip);

	// we check if this player has already a connection:

	for (int i=0; i<p; i++)
	{
		if (sessionInfo.players[i].sameip(ip))
		{
			NETPRINTF("this ip(%x:%d) is already in the player list!\n", ip.host, ip.port);

			sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
			sessionInfo.players[i].netTimeout=0;
			sessionInfo.players[i].netTimeoutSize=LONG_NETWORK_TIMEOUT;
			sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
			return;
		}
	}

	int freeChannel=getFreeChannel();
	if (!sessionInfo.players[p].bind(socket, freeChannel))
	{
		NETPRINTF("this ip(%x:%d) is not bindable\n", ip.host, ip.host);
		return;
	}
	
	
		
	Uint32 newHost=SDL_SwapBE32(getUint32(data, 36));
	Uint32 newPort=(Uint32)SDL_SwapBE16((Uint16)getUint32(data, 40));
	if (serverIP.host)
	{
		if (serverIP.host!=newHost)
		{
			NETPRINTF("Bad ip received by(%x:%d). old=(%x) new=(%x)\n", ip.host, ip.port, serverIP.host, newHost);
			return;
		}
		if (serverIP.port!=newPort)
		{
			NETPRINTF("Bad port received by(%x:%d). old=(%d) new=(%d)\n", ip.host, ip.port, serverIP.port, newPort);
			return;
		}
	}
	else
	{
		serverIP.host=newHost;
		serverIP.port=newPort;
		NETPRINTF("I recived my ip!:(%x:%d).\n", serverIP.host, serverIP.port);
	}

	if ( sessionInfo.players[p].send(sessionInfo.getData(), sessionInfo.getDataLength()) )
	{
		NETPRINTF("this ip(%x:%d) is added in player list.\n", ip.host, ip.port);
		sessionInfo.numberOfPlayer++;
		sessionInfo.team[sessionInfo.players[p].teamNumber].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.team[sessionInfo.players[p].teamNumber].numberOfPlayer++;
		sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		sessionInfo.players[p].netTimeout=0;
		sessionInfo.players[p].netTimeoutSize=LONG_NETWORK_TIMEOUT;
		sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL+1;

		// all other players are ignorant of the new situation:
		initHostGlobalState();
		
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		{
			sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
			if (sessionInfo.players[j].netTimeout>0)
				sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
			sessionInfo.players[j].netTOTL++;
		}
	}
}

void MultiplayersHost::addAI()
{
	int p=sessionInfo.numberOfPlayer;
	int t=(p)%sessionInfo.numberOfTeam;
	if (savedSessionInfo)
		t=savedSessionInfo->getAITeamNumber(&sessionInfo, t);
	
	sessionInfo.players[p].init();
	sessionInfo.players[p].type=BasePlayer::P_AI;
	sessionInfo.players[p].setNumber(p);
	sessionInfo.players[p].setTeamNumber(t);
	strncpy(sessionInfo.players[p].name, globalContainer->texts.getString("[AI]", abs(rand())%globalContainer->texts.AI_NAME_SIZE), BasePlayer::MAX_NAME_LENGTH);
	
	sessionInfo.numberOfPlayer++;
	sessionInfo.team[sessionInfo.players[p].teamNumber].playersMask|=sessionInfo.players[p].numberMask;
	sessionInfo.team[sessionInfo.players[p].teamNumber].numberOfPlayer++;
	
	/*sessionInfo.players[p].netState=BasePlayer::PNS_AI;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=LONG_NETWORK_TIMEOUT;
	sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL+1;
	crossPacketRecieved[p]=4;*/
	
	// all other players are ignorant of the new situation:
	initHostGlobalState();
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
	{
		sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		if (sessionInfo.players[j].netTimeout>0)
			sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
		sessionInfo.players[j].netTOTL++;
	}
}

void MultiplayersHost::confirmPlayer(char *data, int size, IPaddress ip)
{
	Sint32 rcs=getSint32(data, 4);
	Sint32 lcs=sessionInfo.checkSum();

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a checksum !!!\n", ip.host, ip.port);
		return;
	}

	if (rcs!=lcs)
	{
		NETPRINTF("this ip(%x:%d) is has confirmed a wrong check sum !.\n", ip.host, ip.port);
		NETPRINTF("rcs=%x, lcs=%x.\n", rcs, lcs);
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		return;
	}
	else
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_CHECK_SUM;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		NETPRINTF("this ip(%x) is confirmed in player list.\n", ip.host);
		return;
	}
}
void MultiplayersHost::confirmStartCrossConnection(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a confirmStartCrossConnection !!!\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START;
		sessionInfo.players[i].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		NETPRINTF("this ip(%x, %d) is start cross connection confirmed..\n", ip.host, ip.port);
		return;
	}
}
void MultiplayersHost::confirmStillCrossConnecting(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a confirmStillCrossConnecting !!!\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState==BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START)
	{
		//sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START;
		sessionInfo.players[i].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		sessionInfo.players[i].send(SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING);
		NETPRINTF("this ip(%x, %d) is continuing cross connection confirmed..\n", ip.host, ip.port);
		return;
	}
}

void MultiplayersHost::confirmCrossConnectionAchieved(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a confirmCrossConnectionAchieved !!!\n", ip.host, ip.port);
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_FINISHED_CROSS_CONNECTION;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		NETPRINTF("this ip(%x, %d) is cross connection achievement confirmed..\n", ip.host, ip.port);

		crossPacketRecieved[i]=3;

		// let's check if all players are cross Connected
		stepHostGlobalState();

		return;
	}
}

void MultiplayersHost::confirmPlayerStartGame(char *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		NETPRINTF("A player (%x, %d) has sent a bad sized confirmPlayerStartGame.\n", ip.host, ip.port);
		return;
	}

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i==sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sent a confirmPlayerStartGame.\n", ip.host, ip.port);
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
		NETPRINTF("this ip(%x, %d) confirmed start game within %d seconds.\n", ip.host, ip.port, sgtc/20);

		crossPacketRecieved[i]=4;

		// let's check if all players are playing
		stepHostGlobalState();

		return;
	}
}

void MultiplayersHost::treatData(char *data, int size, IPaddress ip)
{
	if ((data[1]!=0)||(data[2]!=0)||(data[3]!=0))
	{
		NETPRINTF("Bad packet received (%d,%d,%d,%d)!\n", data[0], data[1], data[2], data[3]);
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
			NETPRINTF("Unknow kind of packet(%d) recieved by ip(%x:%d).\n", data[0], ip.host, ip.port);
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
			NETPRINTF("Unknow kind of packet(%d) recieved by ip(%x:%d).\n", data[0], ip.host, ip.port);
		};
	}
}

void MultiplayersHost::onTimer(Uint32 tick)
{
	// call yog step
	globalContainer->yog.step();
	
	if (hostGlobalState>=HGS_GAME_START_SENDED)
	{
		if (--startGameTimeCounter<0)
		{
			send(SERVER_ASK_FOR_GAME_BEGINNING, startGameTimeCounter);
			NETPRINTF("Lets quit this screen and start game!\n");
			if (hostGlobalState<=HGS_GAME_START_SENDED)
			{
				// done in game: drop player.
			}
		}
		else if (startGameTimeCounter%20==0)
		{
			send(SERVER_ASK_FOR_GAME_BEGINNING, startGameTimeCounter);
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
			NETPRINTF("Packet received.\n");
			//NETPRINTF("packet=%d\n", (int)packet);
			//NETPRINTF("packet->channel=%d\n", packet->channel);
			//NETPRINTF("packet->len=%d\n", packet->len);
			//NETPRINTF("packet->maxlen=%d\n", packet->maxlen);
			//NETPRINTF("packet->status=%d\n", packet->status);
			//NETPRINTF("packet->address=%x,%d\n", packet->address.host, packet->address.port);

			//NETPRINTF("packet->data=%s\n", packet->data);

			treatData((char *)(packet->data), packet->len, packet->address);

			//paintSessionInfo(hostGlobalState);
			//addUpdateRect();
		}

		SDLNet_FreePacket(packet);
	}
}

bool MultiplayersHost::send(const int v)
{
	//NETPRINTF("Sending packet to all players (%d).\n", v);
	char data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	for (int i=0; i<sessionInfo.numberOfPlayer; i++)
		sessionInfo.players[i].send(data, 4);

	return true;
}
bool MultiplayersHost::send(const int u, const int v)
{
	//NETPRINTF("Sending packet to all players (%d;%d).\n", u, v);
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

void MultiplayersHost::sendingTime()
{
	bool update=false;
	if (hostGlobalState<HGS_GAME_START_SENDED)
	{
		for (int i=0; i<sessionInfo.numberOfPlayer; i++)
			if (sessionInfo.players[i].netState==BasePlayer::PNS_BAD)
			{
				removePlayer(i);
				update=true;
			}

		if (update)
		{
			// all other players are ignorant of the new situation:
			initHostGlobalState();
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			{
				sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_ONE_REQUEST;
				if (sessionInfo.players[j].netTimeout>0)
					sessionInfo.players[j].netTimeout-=sessionInfo.players[j].netTimeoutSize-2*j; // we just split the sendings by 1/10 seconds.
				sessionInfo.players[j].netTOTL++;
			}
		}
	}
	
	for (int i=0; i<sessionInfo.numberOfPlayer; i++)
	{
		if ((sessionInfo.players[i].type==BasePlayer::P_IP)&&(--sessionInfo.players[i].netTimeout<0))
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
					NETPRINTF("Last timeout for player %d has been spent.\n", i);
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
				NETPRINTF("Lets send the session info to player %d.\n", i);

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
				NETPRINTF("Lets send the confiramtion for checksum to player %d.\n", i);
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

				NETPRINTF("player %d is know ok. (%d)\n", i, sessionInfo.players[i].netState);
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
					NETPRINTF("Player %d is all right, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
				// players keeps ok.
			}
			break;

			case BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START :
			{
				NETPRINTF("We have to inform player %d to start cross connection.\n", i);
				sessionInfo.players[i].send(PLAYERS_CAN_START_CROSS_CONNECTIONS);
			}
			break;

			case BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START :
			{
				NETPRINTF("Player %d is cross connecting, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
				sessionInfo.players[i].send(PLAYERS_CAN_START_CROSS_CONNECTIONS);
			}
			break;

			case BasePlayer::PNS_PLAYER_FINISHED_CROSS_CONNECTION :
			{
				NETPRINTF("We have to inform player %d that we recieved his crossConnection confirmation.\n", i);
				sessionInfo.players[i].send(SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION);

				sessionInfo.players[i].netState=BasePlayer::PNS_CROSS_CONNECTED;
			}
			break;

			case BasePlayer::PNS_CROSS_CONNECTED :
			{
				NETPRINTF("Player %d is cross connected ! Yahoo !, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
			}
			break;

			case BasePlayer::PNS_SERVER_SEND_START_GAME :
			{
				NETPRINTF("We send start game to player %d, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
				sessionInfo.players[i].send(SERVER_ASK_FOR_GAME_BEGINNING);
			}
			break;

			case BasePlayer::PNS_PLAYER_CONFIRMED_START_GAME :
			{
				// here we could tell other players
				NETPRINTF("Player %d plays, TOTL %d.\n", i, sessionInfo.players[i].netTOTL);
			}
			break;

			default:
			{
				NETPRINTF("Buggy state for player %d.\n", i);
			}

			}

		}
	}
	
}

void MultiplayersHost::stopHosting(void)
{
	NETPRINTF("Every player has one chance to get the server-quit packet.\n");
	send(SERVER_QUIT_NEW_GAME);
	
	if (shareOnYOG)
	{
		globalContainer->yog.unshareGame();
	}
}

void MultiplayersHost::startGame(void)
{
	if(hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED)
	{
		NETPRINTF("Lets tell all players to start game.\n");
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
	}
	else
		NETPRINTF("can't start now. hostGlobalState=(%d)<(%d)\n", hostGlobalState, HGS_ALL_PLAYERS_CROSS_CONNECTED);

}
