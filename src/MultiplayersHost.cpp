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
			// We need to have the same Port openened to comunicate with all players to pass firewalls.
			// Then, "socket" is the same in all players and in "MultiplayersHost".
			// Therefore, we need to unbind players BEFORE deleting "socket".
			
			for (int p=0; p<sessionInfo.numberOfPlayer; p++)
				if (sessionInfo.players[p].socket==socket)
					sessionInfo.players[p].unbind();
			SDLNet_UDP_Close(socket);
			socket=NULL;
			NETPRINTF("Socket closed.\n");
		}
	}
	
	if (savedSessionInfo)
		delete savedSessionInfo;
}

int MultiplayersHost::newTeamIndice()
{
	// We put the new player in a team with the less number of player
	// and the shortest indice:
	int t=0;
	int lessPlayer=33;
	for (int ti=0; ti<sessionInfo.numberOfTeam; ti++)
	{
		int numberOfPlayer=0;
		Uint32 m=1;
		Uint32 pm=sessionInfo.team[ti].playersMask;
		for (int i=0; i<32; i++)
		{
			if (m&pm)
				numberOfPlayer++;
			m=m<<1;
		}
		if (numberOfPlayer<lessPlayer)
		{
			lessPlayer=numberOfPlayer;
			t=ti;
		}
	}
	assert(t>=0);
	assert(t<32);
	assert(t<sessionInfo.numberOfTeam);
	
	return t;
}

void MultiplayersHost::initHostGlobalState(void)
{
	for (int i=0; i<32; i++)
		crossPacketRecieved[i]=0;
	
	hostGlobalState=HGS_SHARING_SESSION_INFO;
}

void MultiplayersHost::reinitPlayersState()
{
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].netState>BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST)
		{
			sessionInfo.players[j].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
			sessionInfo.players[j].netTimeout=2*j; // we just split the sendings by 1/10 seconds.
			sessionInfo.players[j].netTimeoutSize=LONG_NETWORK_TIMEOUT;
			sessionInfo.players[j].netTOTL++;
		}
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
		NETPRINTF("This is a bad and unknow(%d) hostGlobalState case. Should not happend!\n",hostGlobalState);
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
	bool wasKnownByOthers=(sessionInfo.players[p].netState>BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST);
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
	
	if (wasKnownByOthers)
	{
		// all other players are ignorant of the new situation:
		initHostGlobalState();
		reinitPlayersState();
	}
}

void MultiplayersHost::switchPlayerTeam(int p)
{
	Sint32 teamNumber=(sessionInfo.players[p].teamNumber+1)%sessionInfo.numberOfTeam;
	sessionInfo.players[p].setTeamNumber(teamNumber);
	
	reinitPlayersState();
}

void MultiplayersHost::removePlayer(char *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a quit game !!!\n", ip.host, ip.port);
		return;
	}
	removePlayer(i);
}

void MultiplayersHost::newPlayerPresence(char *data, int size, IPaddress ip)
{
	if (size!=36)
	{
		NETPRINTF("Bad size(%d) for an Presence request from ip %x.\n", size, ip.host);
		return;
	}
	int p=sessionInfo.numberOfPlayer;
	int t=newTeamIndice();
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

			sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST;
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

	if ( sessionInfo.players[p].send(SERVER_PRESENCE) )
	{
		printf("this ip(%x:%d) is added in player list. (player %d)\n", ip.host, ip.port, p);
		sessionInfo.numberOfPlayer++;
		sessionInfo.team[sessionInfo.players[p].teamNumber].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.team[sessionInfo.players[p].teamNumber].numberOfPlayer++;
		sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST;
		sessionInfo.players[p].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[p].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL;
	}
}

void MultiplayersHost::playerWantsSession(char *data, int size, IPaddress ip)
{
	if (size!=12)
	{
		NETPRINTF("Bad size(%d) for an Session request from ip %x.\n", size, ip.host);
		return;
	}
	
	int p;
	for (p=0; p<sessionInfo.numberOfPlayer; p++)
		if (sessionInfo.players[p].sameip(ip))
			break;
	if (p>=sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a Session request !!!\n", ip.host, ip.port);
		return;
	}
	
	bool serverIPReceived=false;
	Uint32 newHost=SDL_SwapBE32(getUint32(data, 4));
	Uint32 newPort=(Uint32)SDL_SwapBE16((Uint16)getUint32(data, 8));
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
		serverIPReceived=true;
		NETPRINTF("I recived my ip!:(%x:%d).\n", serverIP.host, serverIP.port);
	}

	sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=LONG_NETWORK_TIMEOUT;
	sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL+1;

	printf("this ip(%x:%d) wantsSession (player %d)\n", ip.host, ip.port, p);
	
	// all other players are ignorant of the new situation:
	initHostGlobalState();
	reinitPlayersState();
	
	if (serverIPReceived)
		sessionInfo.players[p].netTimeout=3; // =1 would be enough, if loopback where safe.
}

void MultiplayersHost::addAI()
{
	int p=sessionInfo.numberOfPlayer;
	int t=newTeamIndice();
	if (savedSessionInfo)
		t=savedSessionInfo->getAITeamNumber(&sessionInfo, t);
	
	sessionInfo.players[p].init();
	sessionInfo.players[p].type=BasePlayer::P_AI;
	sessionInfo.players[p].setNumber(p);
	sessionInfo.players[p].setTeamNumber(t);
	sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
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
	reinitPlayersState();
}

void MultiplayersHost::confirmPlayer(char *data, int size, IPaddress ip)
{
	Sint32 rcs=getSint32(data, 4);
	Sint32 lcs=sessionInfo.checkSum();

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		NETPRINTF("An unknow player (%x, %d) has sended a checksum !!!\n", ip.host, ip.port);
		return;
	}

	if (rcs!=lcs)
	{
		printf("this ip(%x:%d) confirmed a wrong checksum (player %d)!\n", ip.host, ip.port, i);
		NETPRINTF("rcs=%x, lcs=%x.\n", rcs, lcs);
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=LONG_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		return;
	}
	else
	{
		printf("this ip(%x:%d) confirmed a good checksum (player %d)\n", ip.host, ip.port, i);
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
	if (i>=sessionInfo.numberOfPlayer)
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
	if (i>=sessionInfo.numberOfPlayer)
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
	if (i>=sessionInfo.numberOfPlayer)
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
	if (i>=sessionInfo.numberOfPlayer)
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

void MultiplayersHost::broadcastRequest(char *data, int size, IPaddress ip)
{
	if (size!=4)
	{
		NETPRINTF("broad:Bad size(%d) for a broadcast request from ip %x.\n", size, ip.host);
		return;
	}

	int channel=getFreeChannel();
	channel=SDLNet_UDP_Bind(socket, channel, &ip);
	if (channel!=-1)
	{
		UDPpacket *packet=SDLNet_AllocPacket(36);
		
		if (packet==NULL)
		{
			NETPRINTF("broad:can't alocate packet!\n");
			return;
		}
		
		if (ip.host==0)
		{
			NETPRINTF("broad:can't have a null ip.host\n");
			return;
		}
		
		char data[36];
		if (shareOnYOG)
			data[0]=BROADCAST_RESPONSE_YOG;
		else
			data[0]=BROADCAST_RESPONSE_LAN;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		memset(&data[4], 0, 32);
		strncpy(&data[4], sessionInfo.map.getMapName(), 32);
		printf("MultiplayersHost sending1 (%d, %d, %d, %d).\n", data[4], data[5], data[6], data[7]);
		printf("MultiplayersHost sending2 (%s).\n", sessionInfo.map.getMapName());
		printf("MultiplayersHost sendingB (%s).\n", &data[4]);
		packet->len=36;
		memcpy((char *)packet->data, data, 36);
		
		bool sucess;
		
		packet->address=ip;
		packet->channel=channel;
		
		sucess=SDLNet_UDP_Send(socket, channel, packet)==1;
		// Notice that we can choose between giving a "channel", or the ip.
		// Here we do both. Then "channel" could be -1.
		// This is interesting because getFreeChannel() may return -1.
		// We have no real use of "channel".
		if (sucess)
			NETPRINTF("broad:sucedded to response.\n");
		
		
		SDLNet_FreePacket(packet);
		
		NETPRINTF("broad:Unbinding (socket=%x)(channel=%d).\n", (int)socket, channel);
		SDLNet_UDP_Unbind(socket, channel);
		channel=-1;
	}
	else
	{
		printf("broad:can't bind (socket=%x).\n", (int)socket);
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
		case BROADCAST_REQUEST:
			broadcastRequest(data, size, ip);
		break;
		
		case NEW_PLAYER_WANTS_PRESENCE:
			newPlayerPresence(data, size, ip);
		break;
		
		case NEW_PLAYER_WANTS_SESSION_INFO:
			playerWantsSession(data, size, ip);
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
	if (shareOnYOG)
	{
		globalContainer->yog.step();
		
		if (globalContainer->yog.isFirewallActivation())
		{
			bool isNext=true;
			while(isNext)
			{
				Uint16 port=globalContainer->yog.getFirewallActivationPort();
				char *hostName=globalContainer->yog.getFirewallActivationHostname();
				IPaddress ip;
				if(SDLNet_ResolveHost(&ip, hostName, port)!=0)
				{
					printf("failed to resolve host (%s).\n", hostName);
					continue;
				}
				UDPpacket *packet=SDLNet_AllocPacket(4);
				if (packet==NULL)
					continue;
				packet->len=4;
				char data[4];
				data[0]=SERVER_WATER;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				memcpy((char *)packet->data, data, 4);
				bool success;
				packet->address=ip;
				success=(SDLNet_UDP_Send(socket, -1, packet)==1);
				SDLNet_FreePacket(packet);
				if (!success)
					printf("MultiplayersHost::failed to send water to ip=(%x), port=(%d)\n", ip.host, ip.port);
				
				isNext=globalContainer->yog.getNextFirewallActivation();
			}
		}
	}
	
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
			reinitPlayersState();
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

			case BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST :
			{
				NETPRINTF("Lets send the presence to player %d.\n", i);
				sessionInfo.players[i].send(SERVER_PRESENCE);
			}
			break;

			case BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST :
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
				
				free(data);
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
