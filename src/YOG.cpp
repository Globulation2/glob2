/*
 *  Ysagoon Online Gaming
 *  Meta Server with chat for Ysagoon game (first is glob2)
 *  (c) 2002 Luc-Olivier de Charrière <nuage@ysagoon.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
// TODO:remove useless includes
#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include "YOGConsts.h"
#include "YOG.h"
#include "Marshaling.h"
#include "Utilities.h"
#include "GlobalContainer.h"

YOG::YOG()
{
	socket=NULL;
	yogGlobalState=YGS_NOT_CONNECTING;
	lastMessageID=0;
	
	yogSharingState=YSS_NOT_SHARING_GAME;
	sharingGameName[0]=0;
}

YOG::~YOG()
{
	if (socket)
		SDLNet_UDP_Close(socket);
	
	//TODO: check connection with YOGServer.
}

void YOG::send(YOGMessageType v, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
		printf("Failed to alocate packet!\n");
	{
		Uint8 data[4];
		data[0]=v;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		memcpy((char *)packet->data, data, 4);
	}
	packet->len=size+4;
	memcpy((char *)packet->data+4, data, size);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		printf("Failed to send the packet!\n");
}

void YOG::send(YOGMessageType v, Uint8 id, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
		printf("Failed to alocate packet!\n");
	{
		Uint8 data[4];
		data[0]=v;
		data[1]=id;
		data[2]=0;
		data[3]=0;
		memcpy((char *)packet->data, data, 4);
	}
	packet->len=size+4;
	memcpy((char *)packet->data+4, data, size);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		printf("Failed to send the packet!\n");
}

void YOG::send(YOGMessageType v)
{
	Uint8 data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	UDPpacket *packet=SDLNet_AllocPacket(4);
	if (packet==NULL)
		printf("Failed to alocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		printf("Failed to send the packet!\n");
}

void YOG::send(YOGMessageType v, Uint8 id)
{
	Uint8 data[4];
	data[0]=v;
	data[1]=id;
	data[2]=0;
	data[3]=0;
	UDPpacket *packet=SDLNet_AllocPacket(4);
	if (packet==NULL)
		printf("Failed to alocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		printf("Failed to send the packet!\n");
}

void YOG::treatPacket(Uint32 ip, Uint16 port, Uint8 *data, int size)
{
	printf("YOG::packet received by ip=%d.%d.%d.%d port=%d\n", (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, (ip>>0)&0xFF, port);
	if (data[2]!=0 || data[3]!=0)
	{
		printf("bad packet.\n");
		return;
	}
	printf("YOG::data=[%d.%d.%d.%d]\n", data[0], data[1], data[2], data[3]);
	switch (data[0])
	{
	case YMT_BAD:
		printf("YOG::bad packet.\n");
	break;
	case YMT_MESSAGE:
	{
		Uint8 messageID=data[1];
		send(YMT_MESSAGE, messageID);
		
		bool already=false;
		for (std::list<Message>::iterator mit=receivedMessages.begin(); mit!=receivedMessages.end(); ++mit)
			if (mit->messageID==messageID)
			{
				already=true;
				break;
			}
		if (!already)
		{
			Message m;
			strncpy(m.text, (char *)data+4, 256);
			if (m.text[size-4]!=0)
				printf("YOG::warning, non-zero ending string!\n");
			assert(size-4<256);
			m.text[size-4]=0;
			m.textLength=strlen(m.text)+1;
			//printf("client:%s\n", s);
			m.messageID=messageID;
			m.timeout=0;
			m.TOTL=3;
			strncpy(m.userName, (char *)data+4+m.textLength, 32);
			if (m.userName[31]!=0)
				printf("YOG::warning, non-zero ending userName!\n");
			m.userName[31]=0;
			m.userNameLength=strlen(m.text)+1;
			printf("YOG:new message:%s:%s\n", m.userName, m.text);
			receivedMessages.push_back(m);
		}
	}
	break;
	case YMT_SEND_MESSAGE:
	{
		if (sendingMessages.size()>0)
		{
			Uint8 messageID=data[1];
			std::list<Message>::iterator mit=sendingMessages.begin();
			if (mit->messageID==messageID)
			{
				printf("YOG::Message (%d) has arrived (%s)\n", messageID, mit->text);
				sendingMessages.erase(mit);
				break;
			}
			else
				printf("YOG::Warning, message (%d) confirmed, but message (%d) is being sended!\n", messageID, mit->messageID);
		}
	}
	break;
	case YMT_CONNECTING:
	{
		printf("YOG:connected\n");
		yogGlobalState=YGS_CONNECTED;
	}
	break;
	case YMT_DECONNECTING:
	{
		printf("YOG:deconnected\n");
		yogGlobalState=YGS_NOT_CONNECTING;
	}
	break;
	case YMT_SHARING_GAME:
	{
		printf("YOG:game %s is shared\n", sharingGameName);
		yogSharingState=YSS_SHARED_GAME;
	}
	break;
	case YMT_STOP_SHARING_GAME:
	{
		printf("YOG:game %s is unshared\n", sharingGameName);
		yogSharingState=YSS_NOT_SHARING_GAME;
		
		gamesTimeout=SECOND_TIMEOUT; // We wait 1 sec before requesting the game list, only to use less brandwith.
		gamesTOTL=3;
	}
	break;
	case YMT_REQUEST_SHARED_GAMES_LIST:
	{
		int nbGames=(int)getUint32(data, 4);
		if (size>48+(4+2+32+128)*nbGames)
		{
			printf("YOG::we received a bad game list (size=%d!<=%d)\n", size, 8+(4+2+32+128)*nbGames);
			break;
		}
		printf("YOG:we received the %d games list\n", nbGames);
		games.clear();
		int index=8;
		for (int i=0; i<nbGames; i++)
		{
			GameInfo game;
			game.ip.host=getUint32(data, index);
			index+=4;
			game.ip.port=getUint16(data, index);
			index+=2;
			int l;
			l=Utilities::strnlen((char *)data+index, 32)+1;
			//printf("l=%d.\n", l);
			memcpy(game.userName, data+index, l);
			//printf("game.userName=%s.\n", game.userName);
			//game.userName[31]=0;
			index+=l;
			l=Utilities::strnlen((char *)data+index, 128)+1;
			//printf("l=%d.\n", l);
			memcpy(game.name, data+index, l);
			//printf("game.name=%s.\n", game.name);
			game.name[127]=0;
			index+=l;
			assert(index<=size);
			games.push_back(game);
			printf("YOG::game %d name %s host = %s\n", i, game.name, game.userName);
		}
		assert(index==size);
		gamesTimeout=LONG_NETWORK_TIMEOUT;
		gamesTOTL=3;
		newGameListAviable=true;
	}
	break;
	case YMT_CONNECTION_PRESENCE:
	{
		presenceTOTL=3;
	}
	break;
	case YMT_CLOSE_YOG:
		printf("YOG:: YOG is dead !\n"); //TODO: create a deconnected method
	break;
	}

}

bool YOG::enableConnection(const char *userName)
{
	memset(this->userName, 0, 32);
	strncpy(this->userName, userName, 32);
	this->userName[31]=0;
	
	if (socket)
		SDLNet_UDP_Close(socket);
	
	socket=SDLNet_UDP_Open(0);
	if (!socket)
	{
		printf("YOG::failed to open a socket!\n");
		return false;
	}
	
	int rv=SDLNet_ResolveHost(&serverIP, YOG_SERVER_IP, YOG_SERVER_PORT);
	if (rv==-1)
	{
		printf("YOG::failed to resolve YOG host name!\n");
		return false;
	}
	
	globalContainer->pushUserName(this->userName);
	
	yogGlobalState=YGS_CONNECTING;
	sendingMessages.clear();
	receivedMessages.clear();
	lastMessageID=0;
	
	connectionTimeout=2;//2 instead of 0 to share brandwith with others timouts
	connectionTOTL=3;
	
	games.clear();
	gamesTimeout=4;//4 instead of 0 to share brandwith with others timouts
	gamesTOTL=3;
	
	presenceTimeout=6;//5 instead of 0 to share brandwith with others timouts
	presenceTOTL=3;
	
	printf("YOG::enableConnection(%s)\n", userName);
	
	return true;
}

void YOG::deconnect()
{
	globalContainer->popUserName();
	
	if (yogGlobalState>=YGS_CONNECTING)
		yogGlobalState=YGS_DECONNECTING;
	else
		yogGlobalState=YGS_NOT_CONNECTING;;
	connectionTimeout=0;
	connectionTOTL=3;
	
	if (yogSharingState>=YSS_SHARING_GAME)
	{
		yogSharingState=YSS_STOP_SHARING_GAME;
		sharingGameTimeout=0;
		sharingGameTOTL=3;
	}
}

void YOG::step()
{
	if (socket)
	{
		switch(yogGlobalState)
		{
		case YGS_DECONNECTING:
		{
			if (yogSharingState<=YSS_NOT_SHARING_GAME && connectionTimeout--<=0)
				if (connectionTOTL--<=0)
				{
					yogGlobalState=YGS_NOT_CONNECTING;
					printf("YOG::unable to deconnect!\n");
				}
				else
				{
					printf("YOG::sending deconnection request...\n");
					send(YMT_DECONNECTING);
					connectionTimeout=DEFAULT_NETWORK_TIMEOUT;
				}
		}
		break;
		case YGS_CONNECTING:
		{
			if (connectionTimeout--<=0)
				if (connectionTOTL--<=0)
				{
					yogGlobalState=YGS_UNABLE_TO_CONNECT;
					printf("YOG::unable to connect!\n");
				}
				else
				{
					printf("YOG::sending connection request...\n");
					send(YMT_CONNECTING, (Uint8 *)userName, 32);
					connectionTimeout=DEFAULT_NETWORK_TIMEOUT;
				}
		}
		break;
		case YGS_CONNECTED:
		{
			switch (yogSharingState)
			{
			case YSS_NOT_SHARING_GAME:
				if (gamesTimeout--<=0)
				{
					if (gamesTOTL--<=0)
					{
						printf("YOG:Failed to get the shared games list!\n");
						gamesTimeout=MAX_NETWORK_TIMEOUT;
						gamesTOTL=3;
					}
					else
					{
						printf("YOG:Requesting the shared games list...\n");
						send(YMT_REQUEST_SHARED_GAMES_LIST);
						gamesTimeout=LONG_NETWORK_TIMEOUT;
					}
				}
			break;
			case YSS_STOP_SHARING_GAME:
				if (sharingGameTimeout--<=0)
				{
					if (sharingGameTOTL--<=0)
					{
						printf("YOG::failed to unshare game!\n");
						yogSharingState=YSS_NOT_SHARING_GAME;
					}
					else
					{
						send(YMT_STOP_SHARING_GAME);
						sharingGameTimeout=DEFAULT_NETWORK_TIMEOUT;
					}
				}
			break;
			case YSS_SHARING_GAME:
				if (sharingGameTimeout--<=0)
				{
					if (sharingGameTOTL--<=0)
					{
						printf("YOG::failed to share game!\n");
						yogSharingState=YSS_NOT_SHARING_GAME;
					}
					else
					{
						printf("YOG::sending share game info... (%s)\n", sharingGameName);
						sharingGameTimeout=DEFAULT_NETWORK_TIMEOUT;
						send(YMT_SHARING_GAME, (Uint8 *)sharingGameName, strlen(sharingGameName)+1);
					}
				}
			break;
			case YSS_SHARED_GAME:
				//cool
			break;
			default:
				printf("YOG::warning, bad yogSharingState!\n");
			break;
			} // end switch yogSharingState
			
			if (sendingMessages.size()>0)
			{
				std::list<Message>::iterator mit=sendingMessages.begin();
				if (mit->timeout--<=0)
					if (mit->TOTL--<=0)
					{
						printf("YOG::failed to send a message!\n");
						sendingMessages.erase(mit);
						//break;
					}
					else
					{
						mit->timeout=DEFAULT_NETWORK_TIMEOUT;
						send(YMT_SEND_MESSAGE, mit->messageID, (Uint8 *)mit->text, mit->textLength);
					}
			}
		} // end case YGS_CONNECTED
		case YGS_PLAYING:
		{
			if (presenceTimeout--<=0)
			{
				if (presenceTOTL--<=0)
				{
					printf("YOG::Connection lost to YOG!\n"); //TODO!
				}
				else
				{
					send(YMT_CONNECTION_PRESENCE);
					presenceTimeout=LONG_NETWORK_TIMEOUT;
				}
			}
		}
		break;
		default:

		break;
		}
		
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(YOG_MAX_PACKET_SIZE);
		assert(packet);
		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			treatPacket(packet->address.host, packet->address.port, packet->data, packet->len);
			/*printf("Packet received.\n");
			printf("packet=%d\n", (int)packet);
			printf("packet->channel=%d\n", packet->channel);
			printf("packet->len=%d\n", packet->len);
			printf("packet->maxlen=%d\n", packet->maxlen);
			printf("packet->status=%d\n", packet->status);
			printf("packet->address=%x,%d\n", packet->address.host, packet->address.port);
			printf("SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));
			printf("packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);*/
		}
		SDLNet_FreePacket(packet);

		


	}
}

void YOG::shareGame(const char *gameName)
{
	yogSharingState=YSS_SHARING_GAME;
	strncpy(sharingGameName, gameName, 128);
	sharingGameName[127]=0;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
	printf("YOG::shareGame\n");
}

void YOG::unshareGame()
{
	yogSharingState=YSS_STOP_SHARING_GAME;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
}

bool YOG::isMessage(void)
{
	return (receivedMessages.size()>0);
}

const char *YOG::getMessage(void)
{
	return receivedMessages.begin()->text;
}

const char *YOG::getMessageSource(void)
{
	return receivedMessages.begin()->userName;
}

void YOG::freeMessage(void)
{
	receivedMessages.erase(receivedMessages.begin());
}

void YOG::sendMessage(const char *message)
{
	lastMessageID++;
	Message m;
	m.messageID=lastMessageID;
	strncpy(m.text, message, MAX_MESSAGE_SIZE);
	m.text[MAX_MESSAGE_SIZE-1]=0;
	m.timeout=0;
	m.TOTL=3;
	m.textLength=strlen(m.text)+1;
	m.userName[0]=0;
	m.userNameLength=0;
	sendingMessages.push_back(m);
}

bool YOG::newGameList(bool reset)
{
	if (newGameListAviable)
	{
		if (reset)
			newGameListAviable=false;
		return true;
	}
	else
		return false;
}

void YOG::gameStarted()
{
	if (yogGlobalState==YGS_CONNECTED)
		yogGlobalState=YGS_PLAYING;
	else
		printf("YOG::Warning gameStarted() in a bay state!\n");
}

void YOG::gameEnded()
{
	if (yogGlobalState==YGS_PLAYING)
		yogGlobalState=YGS_CONNECTED;
	else
		printf("YOG::Warning gameEnded() in a bay state!\n");
}