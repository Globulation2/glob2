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

#include "MultiplayersCrossConnectable.h"

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
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].type==BasePlayer::P_IP)
		{
			if (crossPacketRecieved[j]<3) // NOTE: is this still usefull ?
			{
				if (sessionInfo.players[j].netState<BasePlayer::PNS_BINDED)
				{
					int channel=getFreeChannel();
					if (!sessionInfo.players[j].bind(socket, channel))
					{
						printf("Player %d with ip(%x, %d) is not bindable!\n", j, sessionInfo.players[j].ip.host, sessionInfo.players[j].ip.port);
						sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
						sucess=false;
						break;
					}
				}
				sessionInfo.players[j].netState=BasePlayer::PNS_BINDED;

				if (!sessionInfo.players[j].send(data, 8))//&&(sessionInfo.players[j].netState<=BasePlayer::PNS_SENDING_FIRST_PACKET)*/
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

int MultiplayersCrossConnectable::getFreeChannel()
{
	for (int channel=1; channel<SDLNET_MAX_UDPCHANNELS; channel++)
	{
		bool good=true;
		for (int i=0; i<32; i++)
		{
			if (sessionInfo.players[i].channel==channel)
				good=false;
		}
		if (good)
			return channel;
	}
	assert(false);
	return -1;
}

bool MultiplayersCrossConnectable::broadcast(char *data, int size)
{
	bool sucess=false;
	UDPsocket socket=SDLNet_UDP_Open(0);
	if (socket)
	{
		UDPpacket *packet=SDLNet_AllocPacket(size);
		if (packet==NULL)
			return false;
		packet->len=size;

		memcpy((char *)packet->data, data, size);

		//packet->address.host=0xFFFFFFFF;
		//packet->address.port=7007;
		packet->address.host=INADDR_NONE;
		packet->address.port=SERVER_PORT;
		//packet->address=serverIP;
		packet->channel=-1;

		sucess=SDLNet_UDP_Send(socket, -1, packet)==1;

		SDLNet_FreePacket(packet);

		printf("broadcast sucess=%d.\n", sucess);
		
		SDLNet_UDP_Close(socket);
	}
	return sucess;
}
bool MultiplayersCrossConnectable::broadcast(const int v)
{
	char data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	return broadcast(data, 4);
}
