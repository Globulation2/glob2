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
	
	gameSocket=NULL;
	gameSocketReceived=false;
	
	joinedGame=false;
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
	SDLNet_FreePacket(packet);
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
	SDLNet_FreePacket(packet);
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
	SDLNet_FreePacket(packet);
}

void YOG::send(YOGMessageType v, UDPsocket socket)
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
	SDLNet_FreePacket(packet);
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
	SDLNet_FreePacket(packet);
}

void YOG::treatPacket(Uint32 ip, Uint16 port, Uint8 *data, int size)
{
	printf("YOG::packet received by ip=%d.%d.%d.%d port=%d\n", (ip>>0)&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF, port);
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
			m.messageID=messageID;
			m.timeout=0;
			m.TOTL=3;
			int l;
			
			l=Utilities::strmlen((char *)data+4, 256);
			memcpy(m.text, (char *)data+4, l);
			if (m.text[l-1]!=0)
				printf("YOG::warning, non-zero ending text message!\n");
			m.text[255]=0;
			m.textLength=l-1;
			
			l=Utilities::strmlen((char *)data+4+m.textLength, 32);
			memcpy(m.userName, (char *)data+4+m.textLength, l);
			if (m.userName[l-1]!=0)
				printf("YOG::warning, non-zero ending userName!\n");
			m.userName[31]=0;
			m.userNameLength=l-1;
			
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
	}
	break;
	case YMT_GAMES_LIST:
	{
		int nbGames=(int)getUint32(data, 4);
		if (size>48+(4+2+4+32+128)*nbGames)
		{
			printf("YOG::we received a bad game list (size=%d!<=%d)\n", size, 8+(4+2+32+128)*nbGames);
			break;
		}
		printf("YOG:we received a %d games list (size=%d)\n", nbGames, size);
		int index=8;
		for (int i=0; i<nbGames; i++)
		{
			GameInfo game;
			game.ip.host=getUint32(data, index);
			index+=4;
			game.ip.port=getUint16(data, index);
			index+=2;
			game.uid=getUint32(data, index);
			index+=4;
			int l;
			l=Utilities::strmlen((char *)data+index, 32);
			memcpy(game.userName, data+index, l);
			if (game.userName[l-1]!=0)
				printf("YOG::warning, non-zero ending userName!\n");
			game.userName[l-1]=0;
			index+=l;
			l=Utilities::strmlen((char *)data+index, 128);
			memcpy(game.name, data+index, l);
			if (game.name[l-1]!=0)
				printf("YOG::warning, non-zero ending game name!\n");
			game.name[l-1]=0;
			index+=l;
			assert(index<=size);
			games.push_back(game);
			printf("index=%d.\n", index);
			printf("YOG::game no=%d uid=%d name=%s host=%s\n", i, game.uid, game.name, game.userName);
		}
		assert(index==size);
		newGameListAviable=true;
		send(YMT_GAMES_LIST, nbGames);
	}
	break;
	case YMT_UNSHARED_LIST:
	{
		int nbUnshared=(int)getUint32(data, 4);
		if (size!=8+4*nbUnshared)
		{
			printf("YOG::we received a bad unshared list (size=%d!=%d)\n", size, 8+4*nbUnshared);
			break;
		}
		printf("YOG:we received a %d unshared list\n", nbUnshared);
		int index=8;
		for (int i=0; i<nbUnshared; i++)
		{
			Uint32 uid=getUint32(data, index);
			index+=4;
			for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
				if (game->uid==uid)
				{
					games.erase(game);
					break;
				}
		}
		newGameListAviable=true;
		send(YMT_UNSHARED_LIST, nbUnshared);
	}
	break;
	case YMT_CONNECTION_PRESENCE:
	{
		presenceTOTL=3;
	}
	break;
	case YMT_GAME_SOCKET:
	{
		gameSocketTimeout=LONG_NETWORK_TIMEOUT;
		gameSocketTOTL=3;
		gameSocketReceived=true;
		printf("YOG::gameSocketReceived\n");
	}
	break;
	case YMT_CLIENTS_LIST:
	{
		int nbClients=(int)getUint32(data, 4);
		if (size>8+(4+32)*nbClients)
		{
			printf("YOG::we received a bad clients list (size=%d!<=%d)\n", size, 8+(4+32)*nbClients);
			break;
		}
		printf("YOG:we received a %d clients list\n", nbClients);
		int index=8;
		for (int i=0; i<nbClients; i++)
		{
			Client client;
			client.uid=getUint32(data, index);
			index+=4;
			int l=Utilities::strmlen((char *)data+index, 32);
			memcpy(client.userName, data+index, l);
			client.userName[l-1]=0;
			index+=l;
			assert(index<=size);
			clients.push_back(client);
			printf("YOG::client uid=%d name=%s\n", client.uid, client.userName);
		}
		newClientListAviable=true;
		send(YMT_CLIENTS_LIST, nbClients);
	}
	break;
	case YMT_LEFT_CLIENTS_LIST:
	{
		int nbClients=(int)getUint32(data, 4);
		if (size>8+4*nbClients)
		{
			printf("YOG::we received a bad left clients list (size=%d!<=%d)\n", size, 8+4*nbClients);
			break;
		}
		printf("YOG:we received a %d left clients list\n", nbClients);
		int index=8;
		for (int i=0; i<nbClients; i++)
		{
			Uint32 uid=getUint32(data, index);
			index+=4;
			for (std::list<Client>::iterator client=clients.begin(); client!=clients.end(); ++client)
				if (client->uid==uid)
				{
					printf("YOG::left client uid=%d name=%s\n", client->uid, client->userName);
					clients.erase(client);
					break;
				}
		}
		newClientListAviable=true;
		send(YMT_LEFT_CLIENTS_LIST, nbClients);
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
	
	connectionTimeout=0+4;//4 instead of 0 to share brandwith with others timouts
	connectionTOTL=3;
	
	games.clear();
	newGameListAviable=false;
	
	presenceTimeout=0+8;//8 instead of 0 to share brandwith with others timouts
	presenceTOTL=3;
	
	gameSocket=NULL;
	gameSocketReceived=false;
	
	clients.clear();
	newClientListAviable=false;
	
	printf("YOG::enableConnection(%s)\n", userName);
	
	return true;
}

void YOG::deconnect()
{
	globalContainer->popUserName();
	
	if (yogGlobalState>=YGS_CONNECTING)
	{
		yogGlobalState=YGS_DECONNECTING;
		connectionTimeout=0;
		connectionTOTL=3;
	}
	else if (yogGlobalState!=YGS_DECONNECTING)
	{
		yogGlobalState=YGS_DECONNECTING;
		connectionTimeout=0;
		connectionTOTL=3;
		printf("YOG::deconnect() in a bad state (yogGlobalState=%d).\n", yogGlobalState);
		assert(false);
	}
	
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
					send(YMT_CONNECTING, (Uint8 *)userName, Utilities::strmlen(userName, 32));
					connectionTimeout=DEFAULT_NETWORK_TIMEOUT;
				}
		}
		break;
		case YGS_CONNECTED:
		{
			switch (yogSharingState)
			{
			case YSS_NOT_SHARING_GAME:
				//zzz trace joinedGame gamesTimeout gamesTOTL
			break;
			case YSS_STOP_SHARING_GAME:
				// We do stop sharing game also if we are decinnecting
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
			
			
		} // end case YGS_CONNECTED
		case YGS_PLAYING:
		break;
		default:

		break;
		}
		
		if (yogGlobalState==YGS_CONNECTED || yogGlobalState==YGS_PLAYING)
		{
			if (sendingMessages.size()>0)
			{
				std::list<Message>::iterator mit=sendingMessages.begin();
				if (mit->text[0]==0)
					sendingMessages.erase(mit);
				else if (mit->timeout--<=0)
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
		}
		
		if (joinedGame)
		{
			assert(yogGlobalState==YGS_CONNECTED);
			assert(yogSharingState==YSS_NOT_SHARING_GAME);
		}
		
		if (yogGlobalState>=YGS_CONNECTED && presenceTimeout--<=0)
		{
			if (presenceTOTL--<=0)
				printf("YOG::Connection lost to YOG!\n"); //TODO!
			else
				send(YMT_CONNECTION_PRESENCE);
			presenceTimeout=LONG_NETWORK_TIMEOUT;
		}
		
		if (yogSharingState==YSS_STOP_SHARING_GAME && sharingGameTimeout--<=0)
		{
			if (sharingGameTOTL--<=0)
			{
				printf("YOG::failed to unshare game!\n");
				yogSharingState=YSS_NOT_SHARING_GAME;
			}
			else
			{
				printf("YOG::Sending a stop sharing game ...!\n");
				send(YMT_STOP_SHARING_GAME);
				sharingGameTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
		}
		
		if (gameSocket && gameSocketTimeout--<=0)
		{
			if (gameSocketReceived)
			{
				send(YMT_GAME_SOCKET, gameSocket);
				gameSocketTimeout=LONG_NETWORK_TIMEOUT;
			}
			else if (gameSocketTOTL--<=0)
			{
				printf("YOG::Unable to deliver the gameSocket to YOG!\n"); // TODO!
				gameSocketTimeout=LONG_NETWORK_TIMEOUT;
			}
			else
			{
				printf("YOG::Sending the game socket to YOG ...\n");
				send(YMT_GAME_SOCKET, gameSocket);
				gameSocketTimeout=LONG_NETWORK_TIMEOUT;
			}
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
	
	gameSocket=NULL;
	gameSocketReceived=false;
}

void YOG::joinGame()
{
	joinedGame=true;
}

void YOG::unjoinGame()
{
	assert(joinedGame);
	joinedGame=false;
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

bool YOG::newPlayerList(bool reset)
{
	if (newClientListAviable)
	{
		if (reset)
			newClientListAviable=false;
		return true;
	}
	else
		return false;
}

void YOG::gameStarted()
{
	assert(!joinedGame);
	printf("YOG::gameStarted()\n");
	if (yogGlobalState==YGS_CONNECTED)
		yogGlobalState=YGS_PLAYING;
	else
		printf("YOG::Warning gameStarted() in a bad yogGlobalState=%d!\n", yogGlobalState);
}

void YOG::gameEnded()
{
	printf("YOG::gameEnded()\n");
	if (yogGlobalState==YGS_PLAYING)
		yogGlobalState=YGS_CONNECTED;
	else
		printf("YOG::Warning gameEnded() in a bad yogGlobalState=%d!\n", yogGlobalState);
}

void YOG::setGameSocket(UDPsocket socket)
{
	printf("YOG::setGameSocket()\n");
	if (yogSharingState<YSS_SHARING_GAME)
		printf("YOG::Warning setGameSocket() in a bad yogSharingState=%d!\n", yogSharingState);
	gameSocket=socket;
	gameSocketTimeout=0;
	gameSocketTOTL=3;
}

bool YOG::gameSocketSet()
{
	return gameSocketReceived;
}
