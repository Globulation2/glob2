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
				printf("Socket unbinded.\n");
			}
			SDLNet_UDP_Close(socket);
			socket=NULL;
			printf("Socket closed.\n");
		}
	}
}

void MultiplayersJoin::dataSessionInfoRecieved(char *data, int size, IPaddress ip)
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

void MultiplayersJoin::checkSumConfirmationRecieved(char *data, int size, IPaddress ip)
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

void MultiplayersJoin::startCrossConnections(void)
{
	printf("OK, we can start cross connections.\n");

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
		}
		else
		{
			printf("player %d is not binded! (crossPacketRecieved[%d]=%d).\n", p, p, crossPacketRecieved[p]);
		}
	}
	else
		printf("Dangerous crossConnectionFirstMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoin::checkAllCrossConnected(void)
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
}

void MultiplayersJoin::crossConnectionSecondMessage(char *data, int size, IPaddress ip)
{
	printf("crossConnectionSecondMessage\n");

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
		printf("Dangerous crossConnectionSecondMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoin::stillCrossConnectingConfirmation(IPaddress ip)
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

void MultiplayersJoin::crossConnectionsAchievedConfirmation(IPaddress ip)
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

void MultiplayersJoin::serverAskForBeginning(char *data, int size, IPaddress ip)
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

void MultiplayersJoin::treatData(char *data, int size, IPaddress ip)
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

void MultiplayersJoin::onTimer(Uint32 tick)
{
	sendingTime();

	if ((waitingState!=WS_TYPING_SERVER_NAME) && socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		if (SDLNet_UDP_Recv(socket, packet)==1)
		{
			printf("recieved packet (%d)\n", packet->data[0]);
			//printf("packet=%d\n", (int)packet);
			//printf("packet->channel=%d\n", packet->channel);
			//printf("packet->len=%d\n", packet->len);
			//printf("packet->maxlen=%d\n", packet->maxlen);
			//printf("packet->status=%d\n", packet->status);
			//printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);

			//printf("packet->data=%s\n", packet->data);

			treatData((char *)(packet->data), packet->len, packet->address);
		}

		SDLNet_FreePacket(packet);
	}

	if (waitingState==WS_SERVER_START_GAME)
	{
		if (--startGameTimeCounter<0)
		{
			printf("MultiplayersJoin::Lets quit this screen and start game!\n");
		}
	}
}

void MultiplayersJoin::sendingTime()
{
	if ((waitingState!=WS_TYPING_SERVER_NAME)&&(--waitingTimeout<0))
	{
		if (--waitingTOTL<0)
		{
			printf("Last TOTL spent, server has left\n");
			waitingState=WS_TYPING_SERVER_NAME;
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
			MultiplayersCrossConnectable::tryCrossConnections();
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

bool MultiplayersJoin::sendSessionInfoRequest()
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
	strncpy((char *)(packet->data+4), playerName, 16);

	memset(packet->data+20, 0, 8);

	Uint32 netHost=SDL_SwapBE32((Uint32)serverIP.host);
	Uint32 netPort=(Uint32)SDL_SwapBE16(serverIP.port);
	printf("sendSessionInfoRequest() host=%x, port=%x, netHost=%x netPort=%x\n", serverIP.host, serverIP.port, netHost, netPort);
	addUint32(packet->data, netHost, 20);
	addUint32(packet->data, netPort, 24);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		printf("succeded to send session request packet\n");
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

bool MultiplayersJoin::send(const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(4);

	assert(packet);

	packet->channel=channel;
	packet->len=4;
	packet->data[0]=v;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->address=serverIP;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
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
		printf("failed to send packet (v=%d) (channel=%d)\n", v, channel);
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
	packet->len=8;
	packet->data[0]=u;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->data[4]=v;
	packet->data[5]=0;
	packet->data[6]=0;
	packet->data[7]=0;
	packet->address=serverIP;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
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
		printf("failed to send packet (v=%d) (channel=%d)\n", v, channel);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}

bool MultiplayersJoin::tryConnection()
{
	quitThisGame();

	socket=SDLNet_UDP_Open(ANY_PORT);

	if (socket)
	{
		printf("Socket opened at port.\n");
	}
	else
	{
		printf("failed to open a socket.\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	if (SDLNet_ResolveHost(&serverIP, serverName, SERVER_PORT)==0)
	{
		printf("found serverIP.host=%x(%d)\n", serverIP.host, serverIP.host);
	}
	else
	{
		printf("failed to find adresse\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	channel=SDLNet_UDP_Bind(socket, -1, &serverIP);

	if (channel != -1)
	{
		printf("suceeded to bind socket (socket=%x) (channel=%d)\n", (int)socket, channel);

		printf("serverIP.host=%x(%d)\n", serverIP.host, serverIP.host);
		printf("serverIP.port=%x(%d)\n", serverIP.port, serverIP.port);
	}
	else
	{
		printf("failed to bind socket\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	waitingTOTL=DEFAULT_NETWORK_TOTL-1;
	return sendSessionInfoRequest();
}

void MultiplayersJoin::quitThisGame()
{
	printf("quitThisGame() (this=%x)(socket=%x).\n", (int)this, (int)socket);
	unCrossConnectSessionInfo();

	if (socket)
	{
		if (channel!=-1)
		{
			printf("Unbinding socket.\n");
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			printf("Socket unbinded.\n");
		}
		printf("Closing socket.\n");
		SDLNet_UDP_Close(socket);
		socket=NULL;
		printf("Socket closed.\n");
	}
	
	waitingState=WS_TYPING_SERVER_NAME;
}
