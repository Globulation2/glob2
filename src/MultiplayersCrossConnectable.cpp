/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "MultiplayersCrossConnectable.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"

MultiplayersCrossConnectable::MultiplayersCrossConnectable()
:SessionConnection()
{
	logFile=globalContainer->logFileManager->getFile("MultiplayersCrossConnectable.log");
	assert(logFile);
	
	serverIP.host=0;
	serverIP.port=0;
	
	messageID=0;
}

void MultiplayersCrossConnectable::tryCrossConnections(void)
{
	bool sucess=true;
	Uint8 data[8];
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
			if (!sessionInfo.players[j].waitForNatResolution)
			{
				if (crossPacketRecieved[j]<3) // NOTE: is this still usefull ?
				{
					if (sessionInfo.players[j].netState<BasePlayer::PNS_BINDED)
					{
						int freeChannel=getFreeChannel();
						if (!sessionInfo.players[j].bind(socket, freeChannel))
						{
							fprintf(logFile, "Player %d with ip %s is not bindable!\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
							sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
							sucess=false;
							break;
						}
					}
					sessionInfo.players[j].netState=BasePlayer::PNS_BINDED;

					if (!sessionInfo.players[j].send(data, 8))//&&(sessionInfo.players[j].netState<=BasePlayer::PNS_SENDING_FIRST_PACKET)*/
					{
						fprintf(logFile, "Player %d with ip %s is not sendable!\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
						sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
						sucess=false;
						break;
					}
					sessionInfo.players[j].netState=BasePlayer::PNS_SENDING_FIRST_PACKET;
					fprintf(logFile, "We send player %d with ip(%s) the PLAYER_CROSS_CONNECTION_FIRST_MESSAGE\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
				}
			}
			else
				fprintf(logFile, "We wait for nat resolution of player %d.\n", j);
}

int MultiplayersCrossConnectable::getFreeChannel()
{
	for (int channel=1; channel<SDLNET_MAX_UDPCHANNELS; channel++) // By glob2 convention, channel 0 is reserved for game host
	{
		bool good=true;
		for (int i=0; i<32; i++)
		{
			if (sessionInfo.players[i].channel==channel)
				good=false;
		}
		if (good)
		{
			fprintf(logFile, "good free channel=%d\n", channel);
			return channel;
		}
	}
	assert(false);
	return -1;
}

bool MultiplayersCrossConnectable::send(Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	assert(packet);
	memcpy(packet->data, data, size);
	packet->channel=-1;
	packet->address=serverIP;
	if (SDLNet_UDP_Send(socket, -1, packet)!=1)
	{
		fprintf(logFile, "failed to send packet (size=%d)\n", size);
		SDLNet_FreePacket(packet);
		return false;
	}
	SDLNet_FreePacket(packet);
	return true;
}

void MultiplayersCrossConnectable::sendingTime()
{
	for (std::list<Message>::iterator mit=sendingMessages.begin(); mit!=sendingMessages.end(); ++mit)
		if (mit->timeout--<=0)
		{
			int uSize=Utilities::strmlen(mit->userName, 32);
			int sSize=Utilities::strmlen(mit->text, 256);
			int size=4+1+uSize+sSize;
			
			VARARRAY(Uint8,data,size);
			data[0]=ORDER_TEXT_MESSAGE;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			data[4]=mit->messageID;
			strncpy((char *)data+5, mit->userName, uSize);
			strncpy((char *)data+5+uSize, mit->text, sSize);
			
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
				sessionInfo.players[j].send(data, size);
			send(data, size);
			
			mit->TOTL--;
			mit->timeout=DEFAULT_NETWORK_TIMEOUT; //TODO: have a confirmation system!
		}
	for (std::list<Message>::iterator mit=sendingMessages.begin(); mit!=sendingMessages.end(); ++mit)
		if (mit->TOTL<=0)
		{
			sendingMessages.erase(mit);
			break;
		}
}

void MultiplayersCrossConnectable::receivedMessage(Uint8 *data, int size, IPaddress ip)
{
	if (size<9)
	{
		fprintf(logFile, "received a message with size=(%d) too short from ip=(%s)\n", size, Utilities::stringIP(ip));
		return;
	}
	
	Message m;
	m.messageID=data[4];
	int at=5;
	int uStart=at;
	int ul=1;
	while(data[at] && at<size)
	{
		at++;
		ul++;
	}
	if (ul>32)
		ul=32;
	strncpy(m.userName, (char *)data+uStart, ul);
	at++;
	int mStart=at;
	int ml=1;
	while(data[at] && at<size)
	{
		at++;
		ml++;
	}
	if (ml>256)
		ml=256;
	strncpy(m.text, (char *)data+mStart, ml);
	m.guiPainted=false;
	
	printf("received from (%s) message (%s).\n", m.userName, m.text);
	bool allready=false;
	for (std::list<Message>::iterator mit=receivedMessages.begin(); mit!=receivedMessages.end(); ++mit)
		if (strncmp(mit->userName, m.userName, 32)==0 && mit->messageID==m.messageID)
		{
			allready=true;
			break;
		}
	if (!allready)
		receivedMessages.push_back(m);
}

void MultiplayersCrossConnectable::sendMessage(const char *s)
{
	Message m;
	m.messageID=messageID++;
	strncpy(m.userName, globalContainer->userName, 32);
	strncpy(m.text, s, 256);
	m.timeout=0;
	m.TOTL=3;
	
	sendingMessages.push_back(m);
	printf("sending as (%s) message (%s).\n", m.userName, m.text);
}
