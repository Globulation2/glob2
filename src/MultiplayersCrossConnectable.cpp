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

#include "MultiplayersCrossConnectable.h"
#include "Utilities.h"
#include "LogFileManager.h"
#include "GlobalContainer.h"
#include "NetConsts.h"
#include "Order.h"
#include "YOG.h"

MultiplayersCrossConnectable::MultiplayersCrossConnectable()
:SessionConnection()
{
	logFile=globalContainer->logFileManager->getFile("MultiplayersCrossConnectable.log");
	assert(logFile);
	
	serverIP.host=0;
	serverIP.port=0;
	
	messageID=0;
	
	serverNickName[0]=0;
	
	isServer=false;
}

void MultiplayersCrossConnectable::tryCrossConnections(void)
{
	bool success=true;
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
				if (sessionInfo.players[j].channel==-1)
					if (!sessionInfo.players[j].bind(socket))
					{
						fprintf(logFile, "Player %d with ip %s is not bindable!\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
						sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
						success=false;
						break;
					}

				if (!sessionInfo.players[j].send(data, 8))
				{
					fprintf(logFile, "Player %d with ip %s is not sendable!\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
					sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
					success=false;
					break;
				}
				sessionInfo.players[j].netState=BasePlayer::PNS_SENDING_FIRST_PACKET;
				fprintf(logFile, "We send player %d with ip(%s) the PLAYER_CROSS_CONNECTION_FIRST_MESSAGE\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
			}
			else
				fprintf(logFile, "We wait for nat resolution of player %d.\n", j);
}

bool MultiplayersCrossConnectable::sameip(IPaddress ip)
{
	return ((serverIP.host==ip.host)&&(serverIP.port==ip.port));
}

bool MultiplayersCrossConnectable::send(Uint8 *data, int size)
{
	assert(!isServer);
	UDPpacket *packet=SDLNet_AllocPacket(size);
	assert(packet);
	memcpy(packet->data, data, size);
	packet->channel=-1;
	packet->address=serverIP;
	packet->len=size;
	if (SDLNet_UDP_Send(socket, -1, packet)!=1)
	{
		fprintf(logFile, "failed to send packet (size=%d)\n", size);
		SDLNet_FreePacket(packet);
		return false;
	}
	SDLNet_FreePacket(packet);
	return true;
}

bool MultiplayersCrossConnectable::send(const Uint8 u, const Uint8 v)
{
	Uint8 data[8];
	data[0]=u;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=v;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	return send(data, 8);
}

void MultiplayersCrossConnectable::sendingTime()
{
	for (std::list<Message>::iterator mit=sendingMessages.begin(); mit!=sendingMessages.end(); ++mit)
		if (mit->timeout--<=0)
		{
			int uSize=Utilities::strmlen(mit->userName, 32);
			int sSize=Utilities::strmlen(mit->text, 256);
			int size=4+2+uSize+sSize;
			
			VARARRAY(Uint8,data,size);
			data[0]=ORDER_TEXT_MESSAGE;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			data[4]=mit->messageID;
			data[5]=mit->messageType;
			strncpy((char *)data+6, mit->userName, uSize);
			strncpy((char *)data+6+uSize, mit->text, sSize);
			
			bool stillSomeone=false;
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
				if (!mit->received[j])
				{
					sessionInfo.players[j].send(data, size);
					fprintf(logFile, "messageID=(%d) sent to player (%d), ip (%s)\n", mit->messageID, j, Utilities::stringIP(sessionInfo.players[j].ip));
					stillSomeone=true;
				}
			
			if (!mit->received[32])
			{
				if (isServer)
				{
					Message m;
					m.messageID=mit->messageID;
					m.messageType=MessageOrder::NORMAL_MESSAGE_TYPE;
					strncpy(m.userName, mit->userName, 32);
					strncpy(m.text, mit->text, 256);
					m.timeout=0;
					m.TOTL=3;
					m.guiPainted=false;
					receivedMessages.push_back(m);
					fprintf(logFile, "messageID=(%d) copied directly to local queue.\n", mit->messageID);
					mit->received[32]=true;
				}
				else 
				{
					send(data, size);
					fprintf(logFile, "messageID=(%d) sent to host ip (%s)\n", mit->messageID, Utilities::stringIP(serverIP));
					stillSomeone=true;
				}
			}
			
			if (stillSomeone)
			{
				mit->TOTL--;
				mit->timeout=DEFAULT_NETWORK_TIMEOUT;
			}
			else
			{
				fprintf(logFile, "messageID=(%d) fully transmited\n", mit->messageID);
				sendingMessages.erase(mit);
				break;
			}
		}
	for (std::list<Message>::iterator mit=sendingMessages.begin(); mit!=sendingMessages.end(); ++mit)
		if (mit->TOTL<=0)
		{
			fprintf(logFile, "messageID=(%d) erased!\n", mit->messageID);
			sendingMessages.erase(mit);
			break;
		}
}

void MultiplayersCrossConnectable::confirmedMessage(Uint8 *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		fprintf(logFile, "received a confirmation message with bad size=(%d) from ip=(%s)\n", size, Utilities::stringIP(ip));
		return;
	}
	
	Uint8 messageID=data[4];
	fprintf(logFile, "received a confirmation messageID=(%d) from ip=(%s)\n", messageID, Utilities::stringIP(ip));
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].sameip(ip))
			for (std::list<Message>::iterator mit=sendingMessages.begin(); mit!=sendingMessages.end(); ++mit)
				if (messageID==mit->messageID)
					if (!mit->received[j])
					{
						if (mit->messageType==MessageOrder::PRIVATE_MESSAGE_TYPE)
						{
							Message m;
							m.messageID=mit->messageID;
							m.messageType=MessageOrder::PRIVATE_RECEIPT_TYPE;
							strncpy(m.userName, sessionInfo.players[j].name, 32);
							strncpy(m.text, mit->text, 256);
							m.timeout=0;
							m.TOTL=3;
							m.guiPainted=false;
							receivedMessages.push_back(m);
						}
						mit->received[j]=true;
					}
	
	if (sameip(ip) && serverNickName[0])
		for (std::list<Message>::iterator mit=sendingMessages.begin(); mit!=sendingMessages.end(); ++mit)
			if (!mit->received[32])
			{
				if (mit->messageType==MessageOrder::PRIVATE_MESSAGE_TYPE)
				{
					Message m;
					m.messageID=mit->messageID;
					m.messageType=MessageOrder::PRIVATE_RECEIPT_TYPE;
					strncpy(m.userName, serverNickName, 32);
					strncpy(m.text, mit->text, 256);
					m.timeout=0;
					m.TOTL=3;
					m.guiPainted=false;
					receivedMessages.push_back(m);
				}
				mit->received[32]=true;
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
	m.messageType=data[5];
	int at=6;
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
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].sameip(ip))
			sessionInfo.players[j].send(ORDER_TEXT_MESSAGE_CONFIRMATION, m.messageID);
	if (sameip(ip))
		send(ORDER_TEXT_MESSAGE_CONFIRMATION, m.messageID);
	
	bool allready=false;
	for (std::list<Message>::iterator mit=receivedMessages.begin(); mit!=receivedMessages.end(); ++mit)
		if (strncmp(mit->userName, m.userName, 32)==0 && mit->messageID==m.messageID)
		{
			allready=true;
			break;
		}
	if (!allready)
	{
		fprintf(logFile, "new messageID=%d received from ip=(%s) (%s) (%s)\n", m.messageID, Utilities::stringIP(ip), m.userName, m.text);
		receivedMessages.push_back(m);
		if (receivedMessages.size()>64)
			receivedMessages.pop_front();
	}
}

void MultiplayersCrossConnectable::sendMessage(const char *s)
{
	assert(s);
	if (*s==0)
		return;
	
	char message[256];
	strncpy(message, s, 256);
	bool foundLocal=false;
	if (yog)
		yog->handleMessageAliasing(message, 256);
	if (strncmp(message, "/m ", 3)==0)
	{
		bool sentToHost=false;
		if (serverNickName[0])
		{
			int l=Utilities::strnlen(serverNickName, 32);
			if ((strncmp(serverNickName, message+3, l)==0)&&(message[3+l]==' '))
			{
				Message m;
				m.messageID=messageID++;
				m.messageType=MessageOrder::PRIVATE_MESSAGE_TYPE;
				strncpy(m.userName, globalContainer->getUsername().c_str(), 32);
				m.userName[31] = 0;
				strncpy(m.text, message+4+l, 256);
				m.timeout=0;
				m.TOTL=3;
				for (int p=0; p<33; p++)
					m.received[p]=true;
				m.received[32]=false;
				sendingMessages.push_back(m);
				foundLocal=true;
				
				sentToHost=true;
			}
		}
		if (!sentToHost)
			for (int i=0; i<sessionInfo.numberOfPlayer; i++)
			{
				char *name=sessionInfo.players[i].name;
				assert(name);
				int l=Utilities::strnlen(name, 32);
				if ((strncmp(name, message+3, l)==0)&&(message[3+l]==' '))
				{
					Message m;
					m.messageID=messageID++;
					m.messageType=MessageOrder::PRIVATE_MESSAGE_TYPE;
					strncpy(m.userName, globalContainer->getUsername().c_str(), 32);
					m.userName[31] = 0;
					strncpy(m.text, message+4+l, 256);
					m.timeout=0;
					m.TOTL=3;
					for (int p=0; p<33; p++)
						m.received[p]=true;
					m.received[i]=false;
					sendingMessages.push_back(m);
					foundLocal=true;
				}
			}
		
		if (!foundLocal)
		{
			if (yog)
				yog->sendMessage(message);
		}
	}
	else if (message[0]=='/')
	{
		if (yog)
			yog->sendMessage(message);
	}
	else
	{
		Message m;
		m.messageID=messageID++;
		m.messageType=MessageOrder::NORMAL_MESSAGE_TYPE;
		strncpy(m.userName, globalContainer->getUsername().c_str(), 32);
		m.userName[31] = 0;
		strncpy(m.text, s, 256);
		m.timeout=0;
		m.TOTL=3;
		for (int i=0; i<33; i++)
			m.received[i]=false;

		fprintf(logFile, " pushing new messageID=%d (%s) (%s)\n", m.messageID, m.userName, m.text);
		sendingMessages.push_back(m);
	}
}
