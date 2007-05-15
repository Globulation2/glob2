/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "MultiplayersHost.h"
#include "MultiplayersJoin.h"
#include <GAG.h>
#include "NetDefine.h"
#include "YOG.h"
#include "Marshaling.h"
#include "Utilities.h"
#include "LogFileManager.h"
#include "GlobalContainer.h"
#include "NetConsts.h"

#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST (SDL_SwapBE32(0x7F000001))
#endif

MultiplayersHost::MultiplayersHost(SessionInfo *sessionInfo, bool shareOnYOG, SessionInfo *savedSessionInfo)
:MultiplayersCrossConnectable()
{
	this->sessionInfo=*sessionInfo;
	validSessionInfo=true;
	if (savedSessionInfo)
		this->savedSessionInfo=new SessionInfo(*savedSessionInfo);
	else
		this->savedSessionInfo=NULL;

	logFile=globalContainer->logFileManager->getFile("MultiplayersHost.log");
	logFileDownload=globalContainer->logFileManager->getFile("MultiplayersHostDownload.log");
	assert(logFile);
	// net things:
	initHostGlobalState();

	socket=NULL;
	isServer=true;
	serverIP.host=0;
	serverIP.port=0;
	fprintf(logFile, "Openning a socket...\n");
	socket=SDLNet_UDP_Open(GAME_SERVER_PORT);
	if (socket)
		fprintf(logFile, "Socket opened at port (%d).\n", GAME_SERVER_PORT);
	else
		socket=SDLNet_UDP_Open(ANY_PORT);
	if (socket)
	{
		IPaddress *localAddress=SDLNet_UDP_GetPeerAddress(socket, -1);
		serverIP.host=localAddress->host;
		serverIP.port=localAddress->port;
		fprintf(logFile, "Socket opened at ip (%s)\n", Utilities::stringIP(serverIP));
	}
	else
		fprintf(logFile, "failed to open a socket.\n");
		
	
	firstDraw=true;

	this->shareOnYOG=shareOnYOG;
	if (shareOnYOG)
	{
		fprintf(logFile, "sharing on YOG\n");
		yog->shareGame(sessionInfo->getMapNameC());
		yog->setHostGameSocket(socket);
	}
	
	stream = NULL;
	mapFileCheckSum=0;
	// get filename
	std::string filename = sessionInfo->getFileName();
	fprintf(logFile, "MultiplayersHost() mapFileName=%s.\n", filename.c_str());
	
	// checksum
	mapFileCheckSum = globalContainer->fileManager->checksum(filename);
		
	// compress if possible
	if (Toolkit::getFileManager()->gzip(filename, filename + ".gz"))
	{
		fprintf(logFile, "MultiplayersHost() map %s successfully compressed to %s.gz.\n", filename.c_str(), filename.c_str());
		stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename + ".gz"));
	}
	else
	{
		fprintf(logFile, "MultiplayersHost() map %s compression to %s.gz failed, sending uncompressed.\n", filename.c_str(), filename.c_str());
		stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	}
	
	fileSize=0;
	if (stream)
	{
		stream->seekFromEnd(0);
		fileSize = stream->getPosition();
	}
	
	for (int p=0; p<32; p++)
	{
		playerFileTra[p].wantsFile=false;
		playerFileTra[p].receivedFile=false;
		playerFileTra[p].unreceivedIndex=0;
		playerFileTra[p].brandwidth=0;
		playerFileTra[p].lastNbPacketsLost=0;
		for (int i=0; i<PACKET_SLOTS; i++)
		{
			playerFileTra[p].packetSlot[i].index=0;
			playerFileTra[p].packetSlot[i].sent=false;
			playerFileTra[p].packetSlot[i].received=false;
			playerFileTra[p].packetSlot[i].brandwidth=0;
			playerFileTra[p].packetSlot[i].time=0;
		}
		playerFileTra[p].time=0;
		playerFileTra[p].latency=32;
		playerFileTra[p].totalSent=0;
		playerFileTra[p].totalLost=0;
		playerFileTra[p].totalReceived=0;
	}
	
	if (!shareOnYOG)
	{
		sendBroadcastLanGameHosting(GAME_JOINER_PORT_1, true);
		SDL_Delay(10);
		sendBroadcastLanGameHosting(GAME_JOINER_PORT_2, true);
		SDL_Delay(10);
		sendBroadcastLanGameHosting(GAME_JOINER_PORT_3, true);
	}
	
	strncpy(serverNickName, globalContainer->getUsername().c_str(), 32);
}

MultiplayersHost::~MultiplayersHost()
{
	
	if (shareOnYOG)
	{
		yog->unshareGame();
	}
	else
	{
		sendBroadcastLanGameHosting(GAME_JOINER_PORT_1, false);
		SDL_Delay(10);
		sendBroadcastLanGameHosting(GAME_JOINER_PORT_2, false);
		SDL_Delay(10);
		sendBroadcastLanGameHosting(GAME_JOINER_PORT_3, false);
	}
	
	if (destroyNet)
	{
		assert(channel==-1);
		if (channel!=-1)
		{
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			fprintf(logFile, "Socket unbinded.\n");
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
			fprintf(logFile, "Socket closed.\n");
		}
	}
	
	if (savedSessionInfo)
		delete savedSessionInfo;
		
	if (stream)
		delete stream;
	
	if (logFileDownload && logFileDownload!=stdout)
		for (int p=0; p<32; p++)
			if (playerFileTra[p].totalSent)
			{
				fprintf(logFileDownload, "player %d \n", p);
				fprintf(logFileDownload, "playerFileTra[p].totalSent=%d.\n", playerFileTra[p].totalSent);
				fprintf(logFileDownload, "playerFileTra[p].totalLost=%d. (%f)\n", playerFileTra[p].totalLost, (float)playerFileTra[p].totalLost/(float)playerFileTra[p].totalSent);
				fprintf(logFileDownload, "playerFileTra[p].totalReceived=%d. (%f)\n", playerFileTra[p].totalReceived, (float)playerFileTra[p].totalReceived/(float)playerFileTra[p].totalSent);
			}
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
		Uint32 pm=sessionInfo.teams[ti].playersMask;
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
		fprintf(logFile, "This is a bad hostGlobalState case. Should not happend!\n");
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
			fprintf(logFile, "OK, now we are waiting for cross connections\n");
			hostGlobalState=HGS_WAITING_CROSS_CONNECTIONS;
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
				if (sessionInfo.players[i].type==BasePlayer::P_IP)
				{
					sessionInfo.players[i].netState=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START;
					sessionInfo.players[i].netTimeout=i;
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
					fprintf(logFile, "player %d is not cross connected.\n", j);
					allPlayersCrossConnected=false;
					break;
				}
		
		if (allPlayersCrossConnected && (hostGlobalState>=HGS_WAITING_CROSS_CONNECTIONS))
		{
			fprintf(logFile, "Great, all players are cross connected, Game could start, except the file!.\n");
			hostGlobalState=HGS_ALL_PLAYERS_CROSS_CONNECTED;
			stepHostGlobalState();
		}
	}
	break;

	case HGS_ALL_PLAYERS_CROSS_CONNECTED :
	{
		bool allPlayersHaveFile=true;
		
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			if (sessionInfo.players[j].type==BasePlayer::P_IP)
				if (playerFileTra[j].wantsFile && !playerFileTra[j].receivedFile)
				{
					fprintf(logFile, "player %d is still downloading game file.\n", j);
					allPlayersHaveFile=false;
					break;
				}
		
		if (allPlayersHaveFile && (hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED))
		{
			fprintf(logFile, "Great, all players have the game file too, Game could start!.\n");
			hostGlobalState=HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE;
		}
	}
	break;
	
	case HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE :
		
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
		
		if (allPlayersPlaying && (hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE))
		{
			fprintf(logFile, "Great, all players have recieved start info.\n");
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
		fprintf(logFile, "This is a bad and unknow(%d) hostGlobalState case. Should not happend!\n",hostGlobalState);
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
	fprintf(logFile, "player %d quited the game, from team %d.\n", p, t);
	sessionInfo.teams[t].playersMask&=~sessionInfo.players[p].numberMask;
	sessionInfo.teams[t].numberOfPlayer--;
	
	if (playerFileTra[p].wantsFile)
	{
		playerFileTra[p].wantsFile=false;
		playerFileTra[p].receivedFile=false;
		playerFileTra[p].unreceivedIndex=0;
		playerFileTra[p].brandwidth=0;
		playerFileTra[p].lastNbPacketsLost=0;
		for (int i=0; i<PACKET_SLOTS; i++)
		{
			playerFileTra[p].packetSlot[i].index=0;
			playerFileTra[p].packetSlot[i].sent=false;
			playerFileTra[p].packetSlot[i].received=false;
			playerFileTra[p].packetSlot[i].brandwidth=0;
			playerFileTra[p].packetSlot[i].time=0;
		}
		playerFileTra[p].time=0;
		playerFileTra[p].latency=32;
		playerFileTra[p].totalSent=0;
		playerFileTra[p].totalLost=0;
		playerFileTra[p].totalReceived=0;
	}
	
	sessionInfo.players[p].unbind();
	sessionInfo.players[p].netState=BasePlayer::PNS_BAD;
	sessionInfo.players[p].type=BasePlayer::P_NONE;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=DEFAULT_NETWORK_TIMEOUT;//Relase version
	sessionInfo.players[p].netTimeoutSize=0;// TODO : Only for debug version
	sessionInfo.players[p].netTOTL=0;

	int mp=sessionInfo.numberOfPlayer-1;
	if (mp>p)
	{
		fprintf(logFile, "replace it by another player: %d\n", mp);
		int mt=sessionInfo.players[mp].teamNumber;
		sessionInfo.teams[mt].playersMask&=~sessionInfo.players[mp].numberMask;
		sessionInfo.teams[mt].numberOfPlayer--;

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

		sessionInfo.teams[t].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.teams[t].numberOfPlayer++;
	}
	sessionInfo.numberOfPlayer--;
	fprintf(logFile, "nop %d.\n", sessionInfo.numberOfPlayer);
	
	if (wasKnownByOthers)
	{
		// all other players are ignorant of the new situation:
		initHostGlobalState();
		reinitPlayersState();
	}
}

void MultiplayersHost::switchPlayerTeam(int p, int newTeamNumber)
{
	// get actual team number for player
	Sint32 oldTeamNumber = sessionInfo.players[p].teamNumber;
	fprintf(logFile, "\nswitchPlayerTeam(p=%d, newTeamNumber=%d), oldTeamNumber=%d, numberOfTeam=%d\n", p, newTeamNumber, oldTeamNumber, sessionInfo.numberOfTeam);
	assert(newTeamNumber>=0);
	assert(newTeamNumber<sessionInfo.numberOfTeam);
	assert(oldTeamNumber>=0);
	assert(oldTeamNumber<sessionInfo.numberOfTeam);
	// remove from old team, add to new
	sessionInfo.teams[oldTeamNumber].playersMask &= ~sessionInfo.players[p].numberMask;
	sessionInfo.teams[newTeamNumber].playersMask |= sessionInfo.players[p].numberMask;
	sessionInfo.players[p].setTeamNumber(newTeamNumber);
	// send changes to other players
	reinitPlayersState();
}

void MultiplayersHost::removePlayer(Uint8 *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%x, %d) has sent a quit game !!!\n", ip.host, ip.port);
		return;
	}
	removePlayer(i);
}

void MultiplayersHost::yogClientRequestsGameInfo(Uint8 *rdata, int rsize, IPaddress ip)
{
	if (rsize!=8)
	{
		fprintf(logFile, "bad size for a yogClientRequestsGameInfo from ip %s size=%d\n", Utilities::stringIP(ip), rsize);
		return;
	}
	else
		fprintf(logFile, "yogClientRequestsGameInfo from ip %s size=%d\n", Utilities::stringIP(ip), rsize);
	
	Uint8 sdata[64+20];
	sdata[0]=YMT_GAME_INFO_FROM_HOST;
	sdata[1]=0;
	sdata[2]=0;
	sdata[3]=0;
	memcpy(sdata+4, rdata+4, 4); // we copy game's uid
	
	sdata[ 8]=(Uint8)sessionInfo.numberOfPlayer;
	sdata[ 9]=(Uint8)sessionInfo.numberOfTeam;
	sdata[10]=(Uint8)sessionInfo.fileIsAMap;
	if (sessionInfo.mapGenerationDescriptor)
		sdata[11]=(Uint8)sessionInfo.mapGenerationDescriptor->methode;
	else
		sdata[11]=(Uint8)MapGenerationDescriptor::eNONE;
	sdata[12]=0; // pad and trick to show a pseudo game name
	sdata[13]=0; // pad and trick to show a pseudo game name
	sdata[14]=0; // pad
	sdata[15]=NET_PROTOCOL_VERSION;
	addUint32(sdata, globalContainer->getConfigCheckSum(), 16);
	strncpy((char *)(sdata+20), sessionInfo.getMapNameC(), 64);
	int ssize=Utilities::strmlen((char *)(sdata+20), 64)+20;
	assert(ssize<64+20);
	UDPpacket *packet=SDLNet_AllocPacket(64+20);
	if (packet==NULL)
		return;
	if (ip.host==0)
		return;
	packet->len=ssize;

	memcpy(packet->data, sdata, ssize);

	bool success;

	packet->address=ip;
	packet->channel=-1;
	success=SDLNet_UDP_Send(socket, -1, packet)==1;
	if (!success)
		fprintf(logFile, "failed to send yogGameInfo packet!\n");

	SDLNet_FreePacket(packet);
}

void MultiplayersHost::newPlayerPresence(Uint8 *data, int size, IPaddress ip)
{
	fprintf(logFile, "MultiplayersHost::newPlayerPresence().\n");
	if (size!=44)
	{
		fprintf(logFile, " Bad size(%d) for an Presence request from ip %s.\n", size, Utilities::stringIP(ip));
		return;
	}
	Uint8 playerNetProtocolVersion=data[1];
	Uint32 playerConfigCheckSum;
	playerConfigCheckSum=getUint32(data, 4);
	fprintf(logFile, " playerNetProtocolVersion=%d\n", playerNetProtocolVersion);
	fprintf(logFile, " playerConfigCheckSum=%08x\n", playerConfigCheckSum);
	
	int p=sessionInfo.numberOfPlayer;
	int t=newTeamIndice();
	assert(BasePlayer::MAX_NAME_LENGTH==32);
	if (savedSessionInfo)
	{
		char playerName[BasePlayer::MAX_NAME_LENGTH];
		memcpy(playerName, data+12, 32);
		t=savedSessionInfo->getTeamNumber(playerName, t);
	}
	
	assert(p<32);
	playerFileTra[p].wantsFile=false;
	playerFileTra[p].receivedFile=false;
	playerFileTra[p].unreceivedIndex=0;
	playerFileTra[p].brandwidth=0;
	playerFileTra[p].lastNbPacketsLost=0;
	for (int i=0; i<PACKET_SLOTS; i++)
	{
		playerFileTra[p].packetSlot[i].index=0;
		playerFileTra[p].packetSlot[i].sent=false;
		playerFileTra[p].packetSlot[i].received=false;
		playerFileTra[p].packetSlot[i].brandwidth=0;
		playerFileTra[p].packetSlot[i].time=0;
	}
	playerFileTra[p].latency=32;
	playerFileTra[p].time=0;
	playerFileTra[p].totalSent=0;
	playerFileTra[p].totalLost=0;
	playerFileTra[p].totalReceived=0;
	
	sessionInfo.players[p].init();
	sessionInfo.players[p].type=BasePlayer::P_IP;
	sessionInfo.players[p].setNumber(p);
	sessionInfo.players[p].setTeamNumber(t);
	memcpy(sessionInfo.players[p].name, data+12, 32);
	sessionInfo.players[p].setip(ip);
	sessionInfo.players[p].ipFromNAT=(bool)getSint32(data, 8);
	fprintf(logFile, " this ip(%s) has ipFromNAT=(%d)\n", Utilities::stringIP(ip), sessionInfo.players[p].ipFromNAT);

	yog->joinerConnected(ip);
	// we check if this player has already a connection:

	for (int i=0; i<p; i++)
	{
		if (sessionInfo.players[i].sameip(ip))
		{
			fprintf(logFile, " this ip(%s) is already in the player list!\n", Utilities::stringIP(ip));

			sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST;
			sessionInfo.players[i].netTimeout=0;
			sessionInfo.players[i].netTimeoutSize=LONG_NETWORK_TIMEOUT;
			sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
			return;
		}
	}

	if (!sessionInfo.players[p].bind(socket))
	{
		fprintf(logFile, " this ip(%s) is not bindable\n", Utilities::stringIP(ip));
		return;
	}

	if (playerNetProtocolVersion!=NET_PROTOCOL_VERSION || playerConfigCheckSum!=globalContainer->getConfigCheckSum())
	{
		fprintf(logFile, " bad playerNetProtocolVersion!=%d or playerConfigCheckSum=%08x\n",
				NET_PROTOCOL_VERSION, globalContainer->getConfigCheckSum());
		sessionInfo.players[p].send(SERVER_PRESENCE, NET_PROTOCOL_VERSION, globalContainer->getConfigCheckSum());
		return;
	}
	else if (sessionInfo.players[p].send(SERVER_PRESENCE, NET_PROTOCOL_VERSION, globalContainer->getConfigCheckSum()))
	{
		fprintf(logFile, " newPlayerPresence::this ip(%s) is added in player list. (player %d), name=(%s)\n", Utilities::stringIP(ip), p, sessionInfo.players[p].name);
		sessionInfo.numberOfPlayer++;
		sessionInfo.teams[sessionInfo.players[p].teamNumber].playersMask|=sessionInfo.players[p].numberMask;
		sessionInfo.teams[sessionInfo.players[p].teamNumber].numberOfPlayer++;
		sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST;
		sessionInfo.players[p].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[p].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL;
	}
}

void MultiplayersHost::playerWantsSession(Uint8 *data, int size, IPaddress ip)
{
	if (size!=10)
	{
		fprintf(logFile, "Bad size(%d) for an Session request from ip %s.\n", size, Utilities::stringIP(ip));
		return;
	}
	
	int p;
	for (p=0; p<sessionInfo.numberOfPlayer; p++)
		if (sessionInfo.players[p].sameip(ip))
			break;
	if (p>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a Session request !!!\n", Utilities::stringIP(ip));
		return;
	}
	
	bool serverIPReceived=false;
	if (!sessionInfo.players[p].ipFromNAT)
	{
		Uint32 newHost=SDL_SwapBE32(getUint32(data, 4));
		Uint16 newPort=SDL_SwapBE16(getUint16(data, 8));
		fprintf(logFile, "serverIP=(%s), new=(%s)\n", Utilities::stringIP(serverIP), Utilities::stringIP(newHost, newPort));
		if (serverIP.host && (serverIP.host!=SDL_SwapBE32(0x7F000001)))
		{
			if (serverIP.host!=newHost)
			{
				fprintf(logFile, "Bad ip received by(%s). old=(%s) new=(%s)\n", Utilities::stringIP(ip), Utilities::stringIP(serverIP.host), Utilities::stringIP(newHost));
				return;
			}
			if (serverIP.port!=newPort)
			{
				fprintf(logFile, "Bad port received by(%s). old=(%d) new=(%d)\n", Utilities::stringIP(ip), serverIP.port, newPort);
				return;
			}
		}
		else
		{
			serverIP.host=newHost;
			serverIP.port=newPort;
			serverIPReceived=true;
			fprintf(logFile, "I recived my ip!:(%s).\n", Utilities::stringIP(serverIP));
		}
	}

	sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=LONG_NETWORK_TIMEOUT;
	sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL+1;

	fprintf(logFile, "this ip(%s) wantsSession (player %d)\n", Utilities::stringIP(ip), p);
	
	// all other players are ignorant of the new situation:
	initHostGlobalState();
	reinitPlayersState();
	
	if (serverIPReceived)
		sessionInfo.players[p].netTimeout=3; // =1 would be enough, if loopback where safe.
}

void MultiplayersHost::playerWantsFile(Uint8 *data, int size, IPaddress ip)
{
	if (size>72)
	{
		fprintf(logFile, "Bad size(%d) for an File request from ip %s.\n", size, Utilities::stringIP(ip));
		fprintf(logFileDownload, "Bad size(%d) for an File request from ip %s.\n", size, Utilities::stringIP(ip));
		return;
	}
	
	int p;
	for (p=0; p<sessionInfo.numberOfPlayer; p++)
		if (sessionInfo.players[p].sameip(ip))
			break;
	if (p>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a File request !!!\n", Utilities::stringIP(ip));
		fprintf(logFileDownload, "An unknow player (%s) has sent a File request !!!\n", Utilities::stringIP(ip));
		return;
	}
	
	if (data[1]&1)
	{
		Uint8 data[12];
		data[0]=FULL_FILE_DATA;
		data[1]=1;
		data[2]=0;
		data[3]=0;
		addUint32(data, fileSize, 4);
		bool success=sessionInfo.players[p].send(data, 8);
		assert(success);
		if (size==4)
		{
			fprintf(logFileDownload, "Size only requested.\n");
			return;
		}
	}
	
	if (!playerFileTra[p].wantsFile)
	{
		if (!playerFileTra[p].receivedFile)
		{
			fprintf(logFile, "player %d (%s) first requests file.\n", p, Utilities::stringIP(ip));
			fprintf(logFileDownload, "player %d (%s) first requests file.\n", p, Utilities::stringIP(ip));
			
			playerFileTra[p].wantsFile=true;
			playerFileTra[p].receivedFile=false;
			playerFileTra[p].unreceivedIndex=0;
			playerFileTra[p].brandwidth=1;
			playerFileTra[p].lastNbPacketsLost=0;
			for (int i=0; i<PACKET_SLOTS; i++)
			{
				playerFileTra[p].packetSlot[i].index=0;
				playerFileTra[p].packetSlot[i].sent=false;
				playerFileTra[p].packetSlot[i].received=false;
				playerFileTra[p].packetSlot[i].brandwidth=0;
				playerFileTra[p].packetSlot[i].time=0;
			}
			playerFileTra[p].latency=32;
			playerFileTra[p].time=0;
			playerFileTra[p].totalSent=0;
			playerFileTra[p].totalLost=0;
			playerFileTra[p].totalReceived=0;
			
			// we prevently decreases the other players'brandwidth:
			for (int pi=0; pi<32; pi++)
				if (pi!=p && playerFileTra[p].brandwidth>1)
					playerFileTra[p].brandwidth--;
		}
		else
		{
			fprintf(logFile, "player %d (%s) double requests file.\n", p, Utilities::stringIP(ip));
			fprintf(logFileDownload, "player %d (%s) double requests file.\n", p, Utilities::stringIP(ip));
		}
	}
	else if (size>=8)
	{
		Uint32 unreceivedIndex=getUint32(data, 4);
		if (unreceivedIndex<playerFileTra[p].unreceivedIndex)
		{
			fprintf(logFile, "Bad FileRequest packet received !!!\n");
			fprintf(logFileDownload, "Bad FileRequest packet received !!!\n");
			fprintf(logFileDownload, " (player %d unreceivedIndex=%d (%dk), fileSize=%d (%dk))\n", p, unreceivedIndex, unreceivedIndex/1024, fileSize, fileSize/1024);
			return;
		}
		playerFileTra[p].unreceivedIndex=unreceivedIndex;
		fprintf(logFileDownload, "player %d unreceivedIndex=%d (%dk), fileSize=%d (%dk)\n", p, unreceivedIndex, unreceivedIndex/1024, fileSize, fileSize/1024);
		
		if (unreceivedIndex>=fileSize)
		{
			if (playerFileTra[p].totalSent && !playerFileTra[p].receivedFile)
			{
				fprintf(logFileDownload, "player %d \n", p);
				fprintf(logFileDownload, "playerFileTra[p].totalSent=%d.\n", playerFileTra[p].totalSent);
				fprintf(logFileDownload, "playerFileTra[p].totalLost=%d. (%f)\n", playerFileTra[p].totalLost, (float)playerFileTra[p].totalLost/(float)playerFileTra[p].totalSent);
				fprintf(logFileDownload, "playerFileTra[p].totalReceived=%d. (%f)\n", playerFileTra[p].totalReceived, (float)playerFileTra[p].totalReceived/(float)playerFileTra[p].totalSent);
			}
			playerFileTra[p].wantsFile=true;
			playerFileTra[p].receivedFile=true;
			stepHostGlobalState();
		}
		else
		{
			// We dump the current packet's received's confirmation:
			int ixend=(size-8)/8;
			fprintf(logFileDownload, "ixend=%d\n", ixend);
			Uint32 receivedBegin[8];
			Uint32 receivedEnd[8];
			fprintf(logFileDownload, "received=(");
			for (int ix=0; ix<ixend; ix++)
			{
				receivedBegin[ix]=getUint32(data, 8+ix*8);
				receivedEnd[ix]=getUint32(data, 12+ix*8);
				fprintf(logFileDownload, "(%d to %d)+", receivedBegin[ix]/1024, receivedEnd[ix]/1024);
				if (receivedBegin[ix]<=unreceivedIndex)
				{
					fprintf(logFileDownload, "Warning, critical error, (receivedBegin[ix]<=unreceivedIndex), (%d)(%d)\n", receivedBegin[ix], unreceivedIndex);
					return;
				}
			}
			fprintf(logFileDownload, ")\n");
			
			// We record which packets have been received, and how many:
			int nbPacketsReceived=0;
			for (int i=0; i<PACKET_SLOTS; i++)
			{
				Uint32 index=playerFileTra[p].packetSlot[i].index;
				if (playerFileTra[p].packetSlot[i].sent && !playerFileTra[p].packetSlot[i].received)
				{
					if (index<unreceivedIndex)
					{
						nbPacketsReceived++;
						playerFileTra[p].totalReceived++;
						playerFileTra[p].packetSlot[i].received=true;
					}
					else
						for (int ix=0; ix<ixend; ix++)
							if (index>=receivedBegin[ix] && index<=receivedEnd[ix])
							{
								nbPacketsReceived++;
								playerFileTra[p].totalReceived++;
								playerFileTra[p].packetSlot[i].received=true;
								break;
							}
				}
			}
			
			// We compute the number of (probably) lost packets:
			int nbPacketsLost=0;
			int latency=playerFileTra[p].latency;
			for (int i=0; i<PACKET_SLOTS; i++)
			{
				PacketSlot &ps=playerFileTra[p].packetSlot[i];
				if (ps.sent && !ps.received && ps.time>latency)
					nbPacketsLost++;
			}
			if (nbPacketsLost==0)
			{
				int brandwidth=playerFileTra[p].brandwidth;
				brandwidth+=nbPacketsReceived;
				if (playerFileTra[p].brandwidth!=brandwidth)
					fprintf(logFileDownload, "new brandwidth=%d.\n", brandwidth);
				playerFileTra[p].brandwidth=brandwidth;
			}
		}
	}
	else
	{
		fprintf(logFile, "Bad size(%d) for an File request from ip %s.\n", size, Utilities::stringIP(ip));
		fprintf(logFileDownload, "Bad size(%d) for an File request from ip %s.\n", size, Utilities::stringIP(ip));
		return;
	}
	sessionInfo.players[p].netTimeout=sessionInfo.players[p].netTimeoutSize;
	sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersHost::addAI(AI::ImplementitionID aiImplementationId)
{
	int p=sessionInfo.numberOfPlayer;
	int t=newTeamIndice();
	if (savedSessionInfo)
		t=savedSessionInfo->getAITeamNumber(&sessionInfo, t);
	
	sessionInfo.players[p].init();
	sessionInfo.players[p].type=(BasePlayer::PlayerType)(BasePlayer::P_AI+aiImplementationId);
	sessionInfo.players[p].setNumber(p);
	sessionInfo.players[p].setTeamNumber(t);
	sessionInfo.players[p].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
	strncpy(sessionInfo.players[p].name, Toolkit::getStringTable()->getString("[AI]", aiImplementationId), BasePlayer::MAX_NAME_LENGTH);
	
	sessionInfo.numberOfPlayer++;
	sessionInfo.teams[sessionInfo.players[p].teamNumber].playersMask|=sessionInfo.players[p].numberMask;
	sessionInfo.teams[sessionInfo.players[p].teamNumber].numberOfPlayer++;
	
	/*sessionInfo.players[p].netState=BasePlayer::PNS_AI;
	sessionInfo.players[p].netTimeout=0;
	sessionInfo.players[p].netTimeoutSize=LONG_NETWORK_TIMEOUT;
	sessionInfo.players[p].netTOTL=DEFAULT_NETWORK_TOTL+1;
	crossPacketRecieved[p]=4;*/
	
	// all other players are ignorant of the new situation:
	initHostGlobalState();
	reinitPlayersState();
}

void MultiplayersHost::confirmPlayer(Uint8 *data, int size, IPaddress ip)
{
	Uint32 rcs=getUint32(data, 4);
	Uint32 lcs=sessionInfo.checkSum();

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a checksum !!!\n", Utilities::stringIP(ip));
		return;
	}

	if (rcs!=lcs)
	{
		fprintf(logFile, "this ip(%s) confirmed a wrong checksum (player %d)!\n", Utilities::stringIP(ip), i);
		fprintf(logFile, "rcs=%x, lcs=%x.\n", rcs, lcs);
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=LONG_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		return;
	}
	else
	{
		fprintf(logFile, "this ip(%s) confirmed a good checksum (player %d)\n", Utilities::stringIP(ip), i);
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_SEND_CHECK_SUM;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL+1;
		return;
	}
}
void MultiplayersHost::confirmStartCrossConnection(Uint8 *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a confirmStartCrossConnection !!!\n", Utilities::stringIP(ip));
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START;
		sessionInfo.players[i].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		fprintf(logFile, "this ip(%s) is start cross connection confirmed..\n", Utilities::stringIP(ip));
		return;
	}
	else
		fprintf(logFile, "this ip(%s) is start cross connection confirmed too early ??\n", Utilities::stringIP(ip));
}
void MultiplayersHost::confirmStillCrossConnecting(Uint8 *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a confirmStillCrossConnecting !!!\n", Utilities::stringIP(ip));
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START;
		sessionInfo.players[i].netTimeout=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		
		fprintf(logFile, "this ip(%s)(%s) is continuing cross connection confirmed..\n", Utilities::stringIP(ip), sessionInfo.players[i].name);
		
		if (shareOnYOG)
		{
			assert(yog);
			int size=8+10*(int)yog->joiners.size();
			VARARRAY(Uint8,data,size);
			
			data[0]=SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			
			data[4]=(Uint8)yog->joiners.size();
			data[5]=0;
			data[6]=0;
			data[7]=0;
			
			int l=8;
			for (std::list<YOG::Joiner>::iterator ji=yog->joiners.begin(); ji!=yog->joiners.end(); ++ji)
			{
				addUint32(data, ji->uid, l);
				l+=4;
				addUint32(data, ji->ip.host, l);
				l+=4;
				addUint16(data, ji->ip.port, l);
				l+=2;
				
				fprintf(logFile, " with joiner info uid=%d, ip=(%s)\n", ji->uid, Utilities::stringIP(ji->ip));
			}
			assert(l==size);
			
		
			sessionInfo.players[i].send(data, size);
		}
		else
			sessionInfo.players[i].send(SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING);
		
		return;
	}
}

void MultiplayersHost::confirmCrossConnectionAchieved(Uint8 *data, int size, IPaddress ip)
{
	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a confirmCrossConnectionAchieved !!!\n", Utilities::stringIP(ip));
		return;
	}

	if (sessionInfo.players[i].netState>=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START)
	{
		sessionInfo.players[i].netState=BasePlayer::PNS_PLAYER_FINISHED_CROSS_CONNECTION;
		sessionInfo.players[i].netTimeout=0;
		sessionInfo.players[i].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
		sessionInfo.players[i].netTOTL=DEFAULT_NETWORK_TOTL;
		fprintf(logFile, "this ip(%s) player(%d) is cross connection achievement confirmed..\n", Utilities::stringIP(ip), i);

		crossPacketRecieved[i]=3;

		// let's check if all players are cross Connected
		stepHostGlobalState();

		return;
	}
}

void MultiplayersHost::confirmPlayerStartGame(Uint8 *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		fprintf(logFile, "A player (%s) has sent a bad sized confirmPlayerStartGame.\n", Utilities::stringIP(ip));
		return;
	}

	int i;
	for (i=0; i<sessionInfo.numberOfPlayer; i++)
		if (sessionInfo.players[i].sameip(ip))
			break;
	if (i>=sessionInfo.numberOfPlayer)
	{
		fprintf(logFile, "An unknow player (%s) has sent a confirmPlayerStartGame.\n", Utilities::stringIP(ip));
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
		fprintf(logFile, "this ip(%s) confirmed start game within %d seconds.\n", Utilities::stringIP(ip), sgtc/20);

		crossPacketRecieved[i]=4;

		// let's check if all players are playing
		stepHostGlobalState();

		return;
	}
}

void MultiplayersHost::broadcastRequest(Uint8 *data, int size, IPaddress ip)
{
	if (size>5+32 || size<5+2)
	{
		fprintf(logFile, "Warning, bad size for a joinerBroadcastRequest (%d) from ip=(%s)\n", size, Utilities::stringIP(ip));
		return;
	}
	char name[32];
	memcpy(name, data+5, size-5);
	name[size-6]=0;
	
	UDPpacket *packet=SDLNet_AllocPacket(4+64+32);
	if (packet==NULL)
	{
		fprintf(logFile, "broad:can't alocate packet!\n");
		return;
	}

	if (ip.host==0)
	{
		fprintf(logFile, "broad:can't have a null ip.host\n");
		return;
	}

	char sdata[4+64+32];
	if (shareOnYOG)
		sdata[0]=BROADCAST_RESPONSE_YOG;
	else
		sdata[0]=BROADCAST_RESPONSE_LAN;
	sdata[1]=0;
	sdata[2]=0;
	sdata[3]=0;
	memset(sdata+4, 0, 64);
	memcpy(sdata+4, sessionInfo.getMapNameC(), 64);
	sdata[4+64-1] = 0;
	memset(sdata+4+64, 0, 32);
	memcpy(sdata+4+64, globalContainer->getUsername().c_str(), 32);
	sdata[4+64+32-1] = 0;
	
	// TODO: allow to use a game name different than mapName.
	
	packet->len=4+64+32;
	memcpy(packet->data, sdata, 4+64+32);

	bool success;

	packet->address=ip;
	packet->channel=-1;

	success=SDLNet_UDP_Send(socket, -1, packet)==1;
	if (success)
		fprintf(logFile, "Successfuly sent a joinerBroadcastResponse packet to (%s) with ip=(%s).\n", name, Utilities::stringIP(ip));

	SDLNet_FreePacket(packet);
}

void MultiplayersHost::treatData(Uint8 *data, int size, IPaddress ip)
{
	if (size<=0)
	{
		fprintf(logFile, "Bad zero size packet recieved from ip=(%s)\n", Utilities::stringIP(ip));
		return;
	}
	if (data[0]!=NEW_PLAYER_WANTS_FILE)
		fprintf(logFile, "\nMultiplayersHost::treatData (%d)\n", data[0]);
	if ((data[2]!=0)||(data[3]!=0))
	{
		fprintf(logFile, "Bad packet recieved (%d,%d,%d,%d), size=(%d), ip=(%s)\n", data[0], data[1], data[2], data[3], size, Utilities::stringIP(ip));
		return;
	}
	if (hostGlobalState<HGS_GAME_START_SENDED)
	{
		switch (data[0])
		{
		case BROADCAST_REQUEST:
			broadcastRequest(data, size, ip);
		break;
		
		case YOG_CLIENT_REQUESTS_GAME_INFO:
			yogClientRequestsGameInfo(data, size, ip);
		break;
		
		case NEW_PLAYER_WANTS_PRESENCE:
			newPlayerPresence(data, size, ip);
		break;
		
		case NEW_PLAYER_WANTS_SESSION_INFO:
			playerWantsSession(data, size, ip);
		break;
		
		case NEW_PLAYER_WANTS_FILE:
			playerWantsFile(data, size, ip);
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
		
		case ORDER_TEXT_MESSAGE_CONFIRMATION:
			confirmedMessage(data, size, ip);
		break;
		
		case ORDER_TEXT_MESSAGE:
			receivedMessage(data, size, ip);
		break;

		default:
			fprintf(logFile, "Unknow kind of packet(%d) recieved by ip(%s).\n", data[0], Utilities::stringIP(ip));
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
			fprintf(logFile, "Unknow kind of packet(%d) recieved by ip(%s).\n", data[0], Utilities::stringIP(ip));
		};
	}
}

void MultiplayersHost::onTimer(Uint32 tick, MultiplayersJoin *multiplayersJoin)
{
	// call yog step
	if (shareOnYOG && multiplayersJoin && multiplayersJoin->kicked)
		yog->step(); // YOG cares about firewall and NAT
	
	if (hostGlobalState>=HGS_GAME_START_SENDED)
	{
		if (--startGameTimeCounter<0)
		{
			fprintf(logFile, "Lets quit this screen and start game!\n");
			if (hostGlobalState<=HGS_GAME_START_SENDED)
			{
				fprintf(logFile, "But, we didn\'t received the start game from all players!!\n");
				// done in game: drop player.
			}
		}
		for (int i=0; i<sessionInfo.numberOfPlayer; i++)
			if ((startGameTimeCounter%10)==(i%10))
			{
				Uint8 data[8];
				data[0]=SERVER_ASK_FOR_GAME_BEGINNING;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				data[4]=startGameTimeCounter;
				data[5]=0;
				data[6]=0;
				data[7]=0;
				sessionInfo.players[i].send(data, 8);
			}
	}
	else
		sendingTime();

	if (socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		while (SDLNet_UDP_Recv(socket, packet)==1)
			treatData(packet->data, packet->len, packet->address);

		SDLNet_FreePacket(packet);
	}
}

bool MultiplayersHost::send(const int v)
{
	//fprintf(logFile, "Sending packet to all players (%d).\n", v);
	Uint8 data[4];
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
	//fprintf(logFile, "Sending packet to all players (%d;%d).\n", u, v);
	Uint8 data[8];
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

void MultiplayersHost::sendBroadcastLanGameHosting(Uint16 port, bool create)
{
	UDPpacket *packet=SDLNet_AllocPacket(4);
	assert(packet);
	packet->channel=-1;
	packet->address.host=INADDR_BROADCAST;
	packet->address.port=SDL_SwapBE16(port);
	packet->len=4;
	packet->data[0]=BROADCAST_LAN_GAME_HOSTING;
	packet->data[1]=create;
	packet->data[2]=0;
	packet->data[3]=0;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
		fprintf(logFile, "Successed to send a BROADCAST_LAN_GAME_HOSTING(%d) packet to port=(%d).\n", create, port);
	else
		fprintf(logFile, "failed to send a BROADCAST_LAN_GAME_HOSTING(%d) packet to port=(%d)!\n", create, port);
	SDLNet_FreePacket(packet);
}

void MultiplayersHost::sendingTime()
{
	MultiplayersCrossConnectable::sendingTime();
	
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
	
	// We send the file if necessary
	for (int p=0; p<32; p++)
		if (playerFileTra[p].wantsFile && !playerFileTra[p].receivedFile)
		{
			// We compute an average latency:
			int latencySum=0;
			int latencyCount=0;
			for (int i=0; i<PACKET_SLOTS; i++)
				if (playerFileTra[p].packetSlot[i].sent && playerFileTra[p].packetSlot[i].received)
				{
					latencySum+=playerFileTra[p].packetSlot[i].time;
					latencyCount++;
				}
			int latency;
			if (latencyCount>8)
				latency=latencySum/latencyCount;
			else
				latency=8;
			int latencyChange=latency-playerFileTra[p].latency;
			if (latencyChange)
			{
				fprintf(logFileDownload, "new latency=%d.\n", latency);
				playerFileTra[p].latency=latency;
			}
			
			// We compute the number of (more probably) lost packets:
			latency=16+2*latency;
			int nbPacketsLost=0;
			for (int i=0; i<PACKET_SLOTS; i++)
				if (playerFileTra[p].packetSlot[i].sent && !playerFileTra[p].packetSlot[i].received && playerFileTra[p].packetSlot[i].time>latency)
					nbPacketsLost++;
			if (nbPacketsLost)
				fprintf(logFileDownload, "nbPacketsLost=%d.\n", nbPacketsLost);
			
			// We update the brandwidth:
			int brandwidth=playerFileTra[p].brandwidth;
			if (latencyChange>0)
				brandwidth/=1+latencyChange;
			if (nbPacketsLost>playerFileTra[p].lastNbPacketsLost)
				brandwidth-=2*(nbPacketsLost-playerFileTra[p].lastNbPacketsLost);
			if (brandwidth<1)
				brandwidth=1;
			if (playerFileTra[p].brandwidth!=brandwidth)
				fprintf(logFileDownload, "new brandwidth=%d.\n", brandwidth);
			playerFileTra[p].brandwidth=brandwidth;
			
			playerFileTra[p].lastNbPacketsLost=nbPacketsLost;
			
			// Can we send something ?
			int time=(playerFileTra[p].time++)&31;
			int toSend=(brandwidth>>5)+(time<=(brandwidth&31));
			
			// OK, let's sens "toSend" packets:
			fprintf(logFileDownload, "*Sending %d packets:\n", toSend);
			for (int s=0; s<toSend; s++)
			{
				// We take the oldest packet:
				Uint32 minIndex=(Uint32)-1;
				int mini=-1;
				for (int i=0; i<PACKET_SLOTS; i++)
					if (!playerFileTra[p].packetSlot[i].sent)
					{
						minIndex=playerFileTra[p].packetSlot[i].index;
						mini=i;
						break;
					}
				if (mini==-1)
					for (int i=0; i<PACKET_SLOTS; i++)
					{
						Uint32 index=playerFileTra[p].packetSlot[i].index;
						if (index<minIndex)
						{
							minIndex=index;
							mini=i;
							if (minIndex==0)
								break;
						}
					}
				assert(mini!=-1);
				assert(mini<PACKET_SLOTS);
				bool sent=playerFileTra[p].packetSlot[mini].sent;
				bool received=playerFileTra[p].packetSlot[mini].received;
				Uint32 sendingIndex=0;
				if ((sent && received) || !sent)
				{
					// This not a lost packet, we have to find a packet to send:
					// first, is it any late packet ?
					bool found=false;
					Uint32 minIndexLost=(Uint32)-1;
					for (int i=0; i<PACKET_SLOTS; i++)
					{
						PacketSlot &ps=playerFileTra[p].packetSlot[i];
						if (ps.sent && !ps.received && ps.time>latency)
						{
							found=true;
							Uint32 index=ps.index;
							if (index<minIndexLost)
							{
								minIndexLost=index;
								mini=i;
							}
						}
					}
					if (found)
					{
						sendingIndex=minIndexLost;
						fprintf(logFileDownload, "Ressending-v1 now the lost packet, sendingIndex=%d (%dk), mini=%d.\n", sendingIndex, sendingIndex/1024, mini);
						playerFileTra[p].totalLost++;
					}
					else
					{
						// Here, we have no late packet, we look for a new packet:
						Uint32 maxIndex=0;
						bool firstTime=true;
						for (int i=0; i<PACKET_SLOTS; i++)
							if (playerFileTra[p].packetSlot[i].sent)
							{
								Uint32 index=playerFileTra[p].packetSlot[i].index;
								firstTime=false;
								if (index>maxIndex)
									maxIndex=index;
							}
						if (firstTime)
						{
							fprintf(logFileDownload, " We send the first packet!\n");
							sendingIndex=0;
						}
						else
						{
							sendingIndex=maxIndex+1024; // 1024 is the default size.
							if (sendingIndex>=fileSize)
							{
								fprintf(logFileDownload, "Nothing-v1 more to send to player (%d). sendingIndex=(%d).\n", p, sendingIndex);
								toSend=0;
								break;
							}
							else
								fprintf(logFileDownload, "Sending the next packet, sendingIndex=%d.\n", sendingIndex);
						}
					}
				}
				else
				{
					// This is a definitely lost packet, we send it aggain:
					fprintf(logFileDownload, "Ressending-v2 now the lost packet, sendingIndex=%d, mini=%d.\n", sendingIndex, mini);
					sendingIndex=playerFileTra[p].packetSlot[mini].index;
					playerFileTra[p].totalLost++;
				}
				
				int size=1024; // 1024 is the default size.
				if (sendingIndex+size>fileSize)
					size=fileSize-sendingIndex;
				if (size<0)
				{
					fprintf(logFileDownload, "Nothing-v2 more to send to player (%d). sendingIndex=(%d).\n", p, sendingIndex);
					toSend=0;
					break;
				}
				
				Uint8 *data=(Uint8 *)malloc(12+size);
				assert(data);
				data[0]=FULL_FILE_DATA;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				if (size<1024)
					addSint32(data, 0, 4); // Send a confirmation at once, beacause we are at the end of the file.
				else
					addSint32(data, brandwidth, 4);
				addUint32(data, sendingIndex, 8);
				stream->seekFromStart(sendingIndex);
				stream->read(data+12, size, NULL);
				bool success=sessionInfo.players[p].send(data, 12+size);
				if (!success)
				{
					fflush(logFile);
					fflush(logFileDownload);
					printf("Error, p=%d, size=%d\n", p, size);
				}
				assert(success);
				free(data);
				playerFileTra[p].totalSent++;
				fprintf(logFileDownload, "sent a (size=%d) packet to player (%d). sendingIndex=(%d).\n", size, p, sendingIndex);
				
				playerFileTra[p].packetSlot[mini].index=sendingIndex;
				playerFileTra[p].packetSlot[mini].sent=true;
				playerFileTra[p].packetSlot[mini].received=false;
				playerFileTra[p].packetSlot[mini].brandwidth=brandwidth;
				playerFileTra[p].packetSlot[mini].time=0;
			}
			
			for (int i=0; i<PACKET_SLOTS; i++)
				if (playerFileTra[p].packetSlot[i].sent && !playerFileTra[p].packetSlot[i].received)
					playerFileTra[p].packetSlot[i].time++;
		}
	
	
	for (int pi=0; pi<sessionInfo.numberOfPlayer; pi++)
	{
		if ((sessionInfo.players[pi].type==BasePlayer::P_IP)&&(--sessionInfo.players[pi].netTimeout<0))
		{
			update=true;
			sessionInfo.players[pi].netTimeout+=sessionInfo.players[pi].netTimeoutSize;

			assert(sessionInfo.players[pi].netTimeoutSize);

			if (--sessionInfo.players[pi].netTOTL<0)
			{
				if (hostGlobalState>=HGS_GAME_START_SENDED)
				{
					// we only drop the players, because other player are already playing.
					// will be done in the game!
				}
				else
				{
					sessionInfo.players[pi].netState=BasePlayer::PNS_BAD;
					fprintf(logFile, "\nLast timeout for player %d has been spent.\n", pi);
				}
			}

			switch (sessionInfo.players[pi].netState)
			{
			case BasePlayer::PNS_BAD :
			{
				// we remove player out of this loop, to avoid mess.
			}
			break;

			case BasePlayer::PNS_PLAYER_SEND_PRESENCE_REQUEST :
			{
				fprintf(logFile, "\nLets send the presence to player %d.\n", pi);
				sessionInfo.players[pi].send(SERVER_PRESENCE, NET_PROTOCOL_VERSION, globalContainer->getConfigCheckSum());
			}
			break;

			case BasePlayer::PNS_PLAYER_SEND_SESSION_REQUEST :
			{
				fprintf(logFile, "\nLets send the session info to player %d. ip=%s\n", pi, Utilities::stringIP(sessionInfo.players[pi].ip));

				BasePlayer *backupPlayer[32];
				for (int pii=0; pii<sessionInfo.numberOfPlayer; pii++)
				{
					backupPlayer[pii]=(BasePlayer *)malloc(sizeof(BasePlayer));
					*backupPlayer[pii]=sessionInfo.players[pii];
				}
				if (!sessionInfo.players[pi].ipFromNAT)
				{
					//for (int p=0; p<sessionInfo.numberOfPlayer; p++)
					//	if (sessionInfo.players[p].ip.host==SDL_SwapBE32(0x7F000001))
					//		sessionInfo.players[p].ip.host=serverIP.host;
				
					if (shareOnYOG)
						for (int pii=0; pii<sessionInfo.numberOfPlayer; pii++)
							if (sessionInfo.players[pii].ipFromNAT)
							{
								IPaddress newip=yog->ipFromUserName(sessionInfo.players[pii].name);
								fprintf(logFile, "for player (%d) name (%s), may replace ip(%s) by ip(%s)\n", pii, sessionInfo.players[pii].name, Utilities::stringIP(sessionInfo.players[pii].ip), Utilities::stringIP(newip));
								if (newip.host)
								{
									sessionInfo.players[pii].setip(newip);
									sessionInfo.players[pii].ipFromNAT=false;
								}
							}
				}
				
				Uint8 *data=NULL;
				int size=sessionInfo.getDataLength(true);

				fprintf(logFile, "sessionInfo.getDataLength()=size=%d.\n", size);
				fprintf(logFile, "sessionInfo.mapGenerationDescriptor=%p.\n", sessionInfo.mapGenerationDescriptor);

				int hostUserNameSize=Utilities::strmlen(globalContainer->getUsername().c_str(), 32);
				data=(Uint8 *)malloc(12+hostUserNameSize+size);
				assert(data);

				data[0]=DATA_SESSION_INFO;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				addSint32(data, pi, 4);
				addSint32(data, mapFileCheckSum, 8);
				memcpy(data+12, globalContainer->getUsername().c_str(), hostUserNameSize);
				memcpy(data+12+hostUserNameSize, sessionInfo.getData(true), size);

				sessionInfo.players[pi].send(data, 12+hostUserNameSize+size);

				free(data);
				
				for (int pii=0; pii<sessionInfo.numberOfPlayer; pii++)
				{
					sessionInfo.players[pii]=*backupPlayer[pii];
					free(backupPlayer[pii]);
				}
			}
			break;


			case BasePlayer::PNS_PLAYER_SEND_CHECK_SUM :
			{
				fprintf(logFile, "\nLets send the confiramtion for checksum to player %d.\n", pi);
				Uint8 data[8];
				data[0]=SERVER_SEND_CHECKSUM_RECEPTION;
				data[1]=0;
				data[2]=0;
				data[3]=0;
				addUint32(data, sessionInfo.checkSum(), 4);
				sessionInfo.players[pi].send(data, 8);

				// Now that's not our problem if this packet don't success.
				// In such a case, the client will reply.
				sessionInfo.players[pi].netTimeout=0;
				sessionInfo.players[pi].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
				sessionInfo.players[pi].netState=BasePlayer::PNS_OK;

				// Lets check if all players has the sessionInfo:
				stepHostGlobalState();

				fprintf(logFile, "player %d is know ok. (%d)\n", pi, sessionInfo.players[pi].netState);
			}
			break;


			case BasePlayer::PNS_OK :
			{
				if (hostGlobalState>=HGS_WAITING_CROSS_CONNECTIONS)
				{
					sessionInfo.players[pi].netState=BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START;
					sessionInfo.players[pi].netTimeout=0;
					sessionInfo.players[pi].netTimeoutSize=SHORT_NETWORK_TIMEOUT;
					sessionInfo.players[pi].netTOTL++;
					fprintf(logFile, "\nPlayer %d is newly all right, TOTL %d.\n", pi, sessionInfo.players[pi].netTOTL);
				}
				else
					fprintf(logFile, "\nPlayer %d is all right, TOTL %d.\n", pi, sessionInfo.players[pi].netTOTL);
				// players keeps ok.
			}
			break;

			case BasePlayer::PNS_SERVER_SEND_CROSS_CONNECTION_START :
			{
				fprintf(logFile, "\nWe have to inform player %d to start cross connection.\n", pi);
				sessionInfo.players[pi].send(PLAYERS_CAN_START_CROSS_CONNECTIONS);
			}
			break;

			case BasePlayer::PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START :
			{
				fprintf(logFile, "\nPlayer %d is cross connecting, TOTL %d.\n", pi, sessionInfo.players[pi].netTOTL);
				sessionInfo.players[pi].send(PLAYERS_CAN_START_CROSS_CONNECTIONS);
			}
			break;

			case BasePlayer::PNS_PLAYER_FINISHED_CROSS_CONNECTION :
			{
				fprintf(logFile, "\nWe have to inform player %d that we recieved his crossConnection confirmation.\n", pi);
				sessionInfo.players[pi].send(SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION);

				sessionInfo.players[pi].netState=BasePlayer::PNS_CROSS_CONNECTED;
			}
			break;

			case BasePlayer::PNS_CROSS_CONNECTED :
			{
				fprintf(logFile, "\nPlayer %d is cross connected ! Yahoo !, TOTL %d.\n", pi, sessionInfo.players[pi].netTOTL);
			}
			break;

			case BasePlayer::PNS_SERVER_SEND_START_GAME :
			{
				fprintf(logFile, "\nWe send start game to player %d, TOTL %d.\n", pi, sessionInfo.players[pi].netTOTL);
				sessionInfo.players[pi].send(SERVER_ASK_FOR_GAME_BEGINNING);
			}
			break;

			case BasePlayer::PNS_PLAYER_CONFIRMED_START_GAME :
			{
				// here we could tell other players
				fprintf(logFile, "\nPlayer %d plays, TOTL %d.\n", pi, sessionInfo.players[pi].netTOTL);
			}
			break;

			default:
			{
				fprintf(logFile, "\nBuggy state for player %d.\n", pi);
			}

			}

		}
	}
	
}

void MultiplayersHost::stopHosting(void)
{
	fprintf(logFile, "Every player has one chance to get the server-quit packet.\n");
	send(SERVER_QUIT_NEW_GAME);
	
	if (shareOnYOG)
		yog->unshareGame();
}

void MultiplayersHost::startGame(void)
{
	if (hostGlobalState>=HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE)
	{
		if (hostGlobalState<HGS_GAME_START_SENDED)
		{
			fprintf(logFile, "Lets tell all players to start game.\n");
			startGameTimeCounter=SECOND_TIMEOUT*SECONDS_BEFORE_START_GAME;
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
				sessionInfo.players[i].netState=BasePlayer::PNS_SERVER_SEND_START_GAME;
			hostGlobalState=HGS_GAME_START_SENDED;

			// let's check if all players are playing
			stepHostGlobalState();
		}
	}
	else
		fprintf(logFile, "can't start now. hostGlobalState=(%d)<(%d)\n", hostGlobalState, HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE);

}
