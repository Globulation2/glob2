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

#include "MultiplayersJoin.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "NetDefine.h"

MultiplayersJoin::MultiplayersJoin()
:MultiplayersCrossConnectable()
{
	init();
}

void MultiplayersJoin::init()
{
	// net things:

	waitingState=WS_TYPING_SERVER_NAME;
	waitingTimeout=0;
	waitingTimeoutSize=0;
	waitingTOTL=0;

	startGameTimeCounter=0;

	serverName[0]=0;
	playerName[0]=0;
	
	kicked=false;
	
	if (lan.enable(SERVER_PORT))
		broadcastState=BS_ENABLE;
	else
		broadcastState=BS_BAD;
	
	broadcastTimeout=0;
	listHasChanged=false;
}

MultiplayersJoin::~MultiplayersJoin()
{
	if (destroyNet)
	{
		if (socket)
		{
			if (channel!=-1)
			{
				send(CLIENT_QUIT_NEW_GAME);
				SDLNet_UDP_Unbind(socket, channel);
				NETPRINTF("Socket unbinded.\n");
			}
			SDLNet_UDP_Close(socket);
			socket=NULL;
			NETPRINTF("Socket closed.\n");
		}
	}
}

void MultiplayersJoin::dataSessionInfoRecieved(char *data, int size, IPaddress ip)
{
	int pn=getSint32(data, 4);

	if ((pn<0)||(pn>=32))
	{
		NETPRINTF("Warning: bad dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);
		waitingTimeout=0;
		return;
	}

	myPlayerNumber=pn;
	NETPRINTF("dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);

	unCrossConnectSessionInfo();

	if (!sessionInfo.setData(data+8, size-8))
	{
		NETPRINTF("Bad content, or bad size for a sessionInfo packet recieved!\n");
		return;
	}

	validSessionInfo=true;
	waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
	waitingTimeout=0;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoin::checkSumConfirmationRecieved(char *data, int size, IPaddress ip)
{
	NETPRINTF("checkSumConfirmationRecieved\n");

	if (size!=8)
	{
		NETPRINTF("Bad size for a checksum confirmation packet recieved!\n");
		waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}

	Sint32 rsc=getSint32(data, 4);
	Sint32 lsc=sessionInfo.checkSum();

	if (rsc!=lsc)
	{
		NETPRINTF("Bad checksum confirmation packet value recieved!\n");
		waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}

	NETPRINTF("Checksum confirmation packet recieved and valid!\n");

	waitingState=WS_OK;
	waitingTimeout=SHORT_NETWORK_TIMEOUT;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoin::unCrossConnectSessionInfo()
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

/*void MultiplayersJoin::tryCrossConnections(void)
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
				NETPRINTF("Player %d with ip(%x, %d) is not bindable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
				sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
				sucess=false;
				break;
			}
			sessionInfo.players[j].netState=BasePlayer::PNS_BINDED;

			if ( (sessionInfo.players[j].netState<=BasePlayer::PNS_SENDING_FIRST_PACKET)&&(!sessionInfo.players[j].send(data, 8)) )
			{
				NETPRINTF("Player %d with ip(%x, %d) is not sendable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
				sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
				sucess=false;
				break;
			}
			sessionInfo.players[j].netState=BasePlayer::PNS_SENDING_FIRST_PACKET;
		}
}*/

void MultiplayersJoin::startCrossConnections(void)
{
	NETPRINTF("OK, we can start cross connections.\n");

	waitingTimeout=0;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	if (waitingState<WS_CROSS_CONNECTING)
	{
		waitingState=WS_CROSS_CONNECTING;
	}
	MultiplayersCrossConnectable::tryCrossConnections();
	checkAllCrossConnected();
}

void MultiplayersJoin::crossConnectionFirstMessage(char *data, int size, IPaddress ip)
{
	NETPRINTF("crossConnectionFirstMessage\n");

	if (size!=8)
	{
		NETPRINTF("Bad size for a crossConnectionFirstMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];
	NETPRINTF("p=%d\n", p);

	if ((p>=0)&&(p<sessionInfo.numberOfPlayer))
	{
		if (sessionInfo.players[p].ip.host!=ip.host)
		{
			NETPRINTF("Warning: crossConnectionFirstMessage packet recieved(p=%d), but from ip(%x), but should be ip(%x)!\n", p, ip.host, sessionInfo.players[p].ip.host);
		}
		else if ((sessionInfo.players[p].netState>=BasePlayer::PNS_BINDED))
		{
			if (crossPacketRecieved[p]<1)
				crossPacketRecieved[p]=1;
			NETPRINTF("crossConnectionFirstMessage packet recieved (%d)\n", p);

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
		}
		else
		{
			NETPRINTF("player %d is not binded! (crossPacketRecieved[%d]=%d).\n", p, p, crossPacketRecieved[p]);
		}
	}
	else
		NETPRINTF("Dangerous crossConnectionFirstMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoin::checkAllCrossConnected(void)
{
	bool allCrossConnected=true;
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].type==BasePlayer::P_IP)
			if (crossPacketRecieved[j]<2)
			{
				allCrossConnected=false;
				break;
			}
	if (allCrossConnected)
	{
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			if (sessionInfo.players[j].type==BasePlayer::P_IP)
				if ((sessionInfo.players[j].netState!=BasePlayer::PNS_SENDING_FIRST_PACKET)&&(sessionInfo.players[j].netState!=BasePlayer::PNS_HOST))
				{
					allCrossConnected=false;
					break;
				}
		
	}
	if (allCrossConnected)
	{
		NETPRINTF("All players are cross connected to me !!\n");
		waitingState=WS_CROSS_CONNECTING_ACHIEVED;
		if (waitingTimeout>0)
		{
			waitingTimeout-=waitingTimeoutSize;
			assert(waitingTimeoutSize);
			waitingTOTL++;
		}
	}
}

void MultiplayersJoin::crossConnectionSecondMessage(char *data, int size, IPaddress ip)
{
	NETPRINTF("crossConnectionSecondMessage\n");

	if (size!=8)
	{
		NETPRINTF("Bad size for a crossConnectionSecondMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];
	if ((p>=0)&&(p<32))
	{
		crossPacketRecieved[p]=2;
		NETPRINTF("crossConnectionSecondMessage packet recieved (%d)\n", p);
		checkAllCrossConnected();
	}
	else
		NETPRINTF("Dangerous crossConnectionSecondMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoin::stillCrossConnectingConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_START_CONFIRMED)
	{
		NETPRINTF("server(%x,%d has recieved our stillCrossConnecting state.\n", ip.host, ip.port);
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		NETPRINTF("Warning: ip(%x,%d) sent us a stillCrossConnectingConfirmation while in a bad state!.\n", ip.host, ip.port);
}

void MultiplayersJoin::crossConnectionsAchievedConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		NETPRINTF("server(%x,%d has recieved our crossConnection achieved state.\n", ip.host, ip.port);
		waitingState=WS_CROSS_CONNECTING_SERVER_HEARD;
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		NETPRINTF("Warning: ip(%x,%d) sent us a crossConnection achieved state while in a bad state!.\n", ip.host, ip.port);
}

void MultiplayersJoin::serverAskForBeginning(char *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		NETPRINTF("Warning: ip(%x,%d) sent us a bad serverAskForBeginning!.\n", ip.host, ip.port);
	}

	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		waitingState=WS_SERVER_START_GAME;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL+1;

		startGameTimeCounter=data[4];

		NETPRINTF("Server(%x,%d) asked for game start, timecounter=%d\n", ip.host, ip.port, startGameTimeCounter);
	}
	else
		NETPRINTF("Warning: ip(%x,%d) sent us a serverAskForBeginning!.\n", ip.host, ip.port);

}

void MultiplayersJoin::treatData(char *data, int size, IPaddress ip)
{
	if ((data[1]!=0)||(data[2]!=0)||(data[3]!=0))
	{
		NETPRINTF("Bad packet recieved (%d,%d,%d,%d)!\n", data[0], data[1], data[2], data[3]);
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

		case SERVER_KICKED_YOU :
			if (waitingState<WS_SERVER_START_GAME)
			{
				//TODO : show an explaination pannel for the joiner.
				NETPRINTF("Server kicked you.\n");
				myPlayerNumber=-1;
				waitingState=WS_TYPING_SERVER_NAME;
				kicked=true;
			}
		break;
		case SERVER_QUIT_NEW_GAME :
			if (waitingState<WS_SERVER_START_GAME)
			{
				//TODO : show an explaination pannel for the joiner.
				NETPRINTF("Server has quit.\n");
				myPlayerNumber=-1;
				waitingState=WS_TYPING_SERVER_NAME;
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
			NETPRINTF("Unknow kind of packet(%d) recieved from ip(%x,%d)!\n", data[0], ip.host, ip.port);
	}
}

bool MultiplayersJoin::getList(char ***list, int *length)
{
	if (listHasChanged)
	{
		listHasChanged=false;
		
		int l=LANHosts.size();
		*list=new char*[l];
		
		std::list<LANHost>::iterator it;
		int i=0;
		for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
		{
			assert(i<l);
			(*list)[i]=new char[16];
			
			Uint32 netHost=SDL_SwapBE32(it->ip);
			snprintf((*list)[i], 16, "%d.%d.%d.%d", (netHost>>24)&0xFF, (netHost>>16)&0xFF, (netHost>>8)&0xFF, netHost&0xFF);
			printf("getList::list[%d]=%s\n", i, (*list)[i]);
			i++;
		}
		*length=l;
		return true;
	}
	else
		return false;
}

void MultiplayersJoin::receiveTime()
{
	std::list<LANHost>::iterator it;
	for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
		it->timeout--;
	
	int v;
	if (lan.receive(&v))
	{
		LANHost lanhost;
		lanhost.rv=v;
		lanhost.ip=lan.getSenderIP();
		lanhost.timeout=SHORT_NETWORK_TIMEOUT;
		LANHosts.push_front(lanhost);
		listHasChanged=true;
	}
	
	for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
	{
		if (it->timeout<0)
		{
			std::list<LANHost>::iterator ittemp=it;
			it=LANHosts.erase(ittemp);
			listHasChanged=true;
			printf("removed (%d).\n", ittemp->rv);
		}
	}
}

void MultiplayersJoin::onTimer(Uint32 tick)
{
	sendingTime();
	receiveTime();

	if ((waitingState!=WS_TYPING_SERVER_NAME) && socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		if (SDLNet_UDP_Recv(socket, packet)==1)
		{
			NETPRINTF("recieved packet (%d)\n", packet->data[0]);
			//NETPRINTF("packet=%d\n", (int)packet);
			//NETPRINTF("packet->channel=%d\n", packet->channel);
			//NETPRINTF("packet->len=%d\n", packet->len);
			//NETPRINTF("packet->maxlen=%d\n", packet->maxlen);
			//NETPRINTF("packet->status=%d\n", packet->status);
			//NETPRINTF("packet->address=%x,%d\n", packet->address.host, packet->address.port);

			//NETPRINTF("packet->data=%s\n", packet->data);

			treatData((char *)(packet->data), packet->len, packet->address);
		}

		SDLNet_FreePacket(packet);
	}

	if (waitingState==WS_SERVER_START_GAME)
	{
		if (--startGameTimeCounter<0)
		{
			NETPRINTF("MultiplayersJoin::Lets quit this screen and start game!\n");
		}
	}
}

char *MultiplayersJoin::getStatusString()
{
	char *s;
	switch (waitingState)
	{
		case WS_BAD:
			s=globalContainer->texts.getString("[bad error in connection system]");
		break;
		case WS_TYPING_SERVER_NAME:
			s=globalContainer->texts.getString("[not connected]");
		break;
		case WS_WAITING_FOR_SESSION_INFO:
			s=globalContainer->texts.getString("[request sended, waiting for answer]");
		break;
		case WS_WAITING_FOR_CHECKSUM_CONFIRMATION:
			s=globalContainer->texts.getString("[answer recieved, checksum sended]");
		break;
		case WS_OK:
			s=globalContainer->texts.getString("[connected to server]");
		break;
		case WS_CROSS_CONNECTING:
		case WS_CROSS_CONNECTING_START_CONFIRMED:
			s=globalContainer->texts.getString("[connecting to all players]");
		break;
		case WS_CROSS_CONNECTING_ACHIEVED:
		case WS_CROSS_CONNECTING_SERVER_HEARD:
		case WS_SERVER_START_GAME:
			s=globalContainer->texts.getString("[connected to all players]");
		break;
	}
	int l=strlen(s)+1;
	char *t=new char[l];
	strncpy(t, s, l);
	return t;
}


void MultiplayersJoin::sendingTime()
{
	if ((broadcastState==BS_ENABLE)&&(--broadcastTimeout<0))
	{
		if (lan.send(BROADCAST_REQUEST))
			broadcastTimeout=SHORT_NETWORK_TIMEOUT;
		else
			broadcastState=BS_BAD;
	}

	if ((waitingState!=WS_TYPING_SERVER_NAME)&&(--waitingTimeout<0))
	{
		if (--waitingTOTL<0)
		{
			NETPRINTF("Last TOTL spent, server has left\n");
			waitingState=WS_TYPING_SERVER_NAME;
		}
		else
			NETPRINTF("TOTL %d\n", waitingTOTL);

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
			NETPRINTF("Im ok, but I want the server to know I m still here!\n");
			if (!sendSessionInfoConfirmation())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_CROSS_CONNECTING:
		{
			NETPRINTF("We tell the server that we heard about croos connection start.\n");
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
			NETPRINTF("We try cross connecting aggain:\n");
			// we have to inform the server that we are still alive:
			if (!send(PLAYERS_STILL_CROSS_CONNECTING))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
			MultiplayersCrossConnectable::tryCrossConnections();
			checkAllCrossConnected();
		}
		break;

		case WS_CROSS_CONNECTING_ACHIEVED:
		{
			NETPRINTF("We are cross connected.\n");
			if (!send(PLAYERS_CROSS_CONNECTIONS_ACHIEVED))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		case WS_CROSS_CONNECTING_SERVER_HEARD:
		{
			NETPRINTF("Im fully cross connected and server confirmed!\n");
			if (!send(PLAYERS_CROSS_CONNECTIONS_ACHIEVED))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		case WS_SERVER_START_GAME :
		{
			NETPRINTF("Starting game within %d seconds.\n", (int)(startGameTimeCounter/20));
			if (!send(PLAYER_CONFIRM_GAME_BEGINNING, startGameTimeCounter))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		default:
			NETPRINTF("Im in a bad state %d!\n", waitingState);

		}

		waitingTimeout=waitingTimeoutSize;
		assert(waitingTimeoutSize);
	}
}

bool MultiplayersJoin::sendSessionInfoRequest()
{
	assert(BasePlayer::MAX_NAME_LENGTH==32);
	UDPpacket *packet=SDLNet_AllocPacket(44);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=44;
	packet->data[0]=NEW_PLAYER_WANTS_SESSION_INFO;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	memset(packet->data+4, 0, 32);
	strncpy((char *)(packet->data+4), playerName, 32);

	memset(packet->data+36, 0, 8);

	Uint32 netHost=SDL_SwapBE32((Uint32)serverIP.host);
	Uint32 netPort=(Uint32)SDL_SwapBE16(serverIP.port);
	NETPRINTF("sendSessionInfoRequest() host=%x, port=%x, netHost=%x netPort=%x\n", serverIP.host, serverIP.port, netHost, netPort);
	addUint32(packet->data, netHost, 36);
	addUint32(packet->data, netPort, 40);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		NETPRINTF("succeded to send session request packet\n");
		//NETPRINTF("packet->channel=%d\n", packet->channel);
		//NETPRINTF("packet->len=%d\n", packet->len);
		//NETPRINTF("packet->maxlen=%d\n", packet->maxlen);
		//NETPRINTF("packet->status=%d\n", packet->status);
		//NETPRINTF("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//NETPRINTF("packet->data=%s\n", packet->data);
	}
	else
	{
		NETPRINTF("failed to send session request packet\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=LONG_NETWORK_TIMEOUT;
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	return true;
}

bool MultiplayersJoin::sendSessionInfoConfirmation()
{

	UDPpacket *packet=SDLNet_AllocPacket(8);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=8;
	packet->data[0]=NEW_PLAYER_SEND_CHECKSUM_CONFIRMATION;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	Sint32 cs=sessionInfo.checkSum();
	printf("MultiplayersJoin::cs=%x.\n", cs);
	addSint32((char *)(packet->data), cs, 4);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		NETPRINTF("suceeded to send confirmation packet\n");
		//NETPRINTF("packet->channel=%d\n", packet->channel);
		//NETPRINTF("packet->len=%d\n", packet->len);
		//NETPRINTF("packet->maxlen=%d\n", packet->maxlen);
		//NETPRINTF("packet->status=%d\n", packet->status);
		//NETPRINTF("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//NETPRINTF("packet->data=%s, cs=%x\n", packet->data, cs);
	}
	else
	{
		NETPRINTF("failed to send confirmation packet\n");
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=LONG_NETWORK_TIMEOUT;
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	return true;
}

bool MultiplayersJoin::send(const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(4);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=4;
	packet->data[0]=v;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		NETPRINTF("suceeded to send packet v=%d\n", v);
		//NETPRINTF("packet->channel=%d\n", packet->channel);
		//NETPRINTF("packet->len=%d\n", packet->len);
		//NETPRINTF("packet->maxlen=%d\n", packet->maxlen);
		//NETPRINTF("packet->status=%d\n", packet->status);
		//NETPRINTF("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//NETPRINTF("packet->data=%s\n", packet->data);
	}
	else
	{
		NETPRINTF("failed to send packet (v=%d) (channel=%d)\n", v, channel);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}
bool MultiplayersJoin::send(const int u, const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(8);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=8;
	packet->data[0]=u;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->data[4]=v;
	packet->data[5]=0;
	packet->data[6]=0;
	packet->data[7]=0;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		NETPRINTF("suceeded to send packet v=%d\n", v);
		//NETPRINTF("packet->channel=%d\n", packet->channel);
		//NETPRINTF("packet->len=%d\n", packet->len);
		//NETPRINTF("packet->maxlen=%d\n", packet->maxlen);
		//NETPRINTF("packet->status=%d\n", packet->status);
		//NETPRINTF("packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//NETPRINTF("packet->data=%s\n", packet->data);
	}
	else
	{
		NETPRINTF("failed to send packet (v=%d) (channel=%d)\n", v, channel);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}

bool MultiplayersJoin::tryConnection()
{
	quitThisGame();

	assert(socket==NULL);
	socket=SDLNet_UDP_Open(ANY_PORT);

	if (socket)
	{
		NETPRINTF("Socket opened at port.\n");
	}
	else
	{
		NETPRINTF("failed to open a socket.\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	if (SDLNet_ResolveHost(&serverIP, serverName, SERVER_PORT)==0)
	{
		NETPRINTF("found serverIP.host=%x(%d)\n", serverIP.host, serverIP.host);
	}
	else
	{
		NETPRINTF("failed to find adresse\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	channel=SDLNet_UDP_Bind(socket, -1, &serverIP);
	//or zzz? channel=SDLNet_UDP_Bind(socket, 0, &serverIP);

	if (channel != -1)
	{
		NETPRINTF("suceeded to bind socket (socket=%x) (channel=%d)\n", (int)socket, channel);

		NETPRINTF("serverIP.host=%x(%d)\n", serverIP.host, serverIP.host);
		NETPRINTF("serverIP.port=%x(%d)\n", serverIP.port, serverIP.port);
	}
	else
	{
		NETPRINTF("failed to bind socket\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	waitingTOTL=DEFAULT_NETWORK_TOTL-1;
	return sendSessionInfoRequest();
}

void MultiplayersJoin::quitThisGame()
{
	NETPRINTF("quitThisGame() (this=%x)(socket=%x).\n", (int)this, (int)socket);
	unCrossConnectSessionInfo();

	if (socket)
	{
		if (channel!=-1)
		{
			NETPRINTF("Unbinding socket.\n");
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			NETPRINTF("Socket unbinded.\n");
		}
		NETPRINTF("Closing socket.\n");
		SDLNet_UDP_Close(socket);
		socket=NULL;
		NETPRINTF("Socket closed.\n");
	}
	
	waitingState=WS_TYPING_SERVER_NAME;
}
