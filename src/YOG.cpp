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
#include "NetConsts.h"

YOG::YOG()
{
	socket=NULL;
	yogGlobalState=YGS_NOT_CONNECTING;
	lastMessageID=0;
	
	yogSharingState=YSS_NOT_SHARING_GAME;
	sharingGameName[0]=0;
	
	hostGameSocket=NULL;
	hostGameSocketReceived=false;
	joinGameSocket=NULL;
	joinGameSocketReceived=false;
	
	joinedGame=false;
	
	isSelectedGame=false;
	newSelectedGameinfoAviable=false;
	selectedGameinfoValid=false;
	selectedGameinfoTimeout=0;
	selectedGameinfoTOTL=0;
	
	enableLan=lan.enable(SERVER_PORT);
	//printf("enableLan=%d.\n", enableLan);
	
	// Funny, LogFileManager is not initialised !
	//logFile=globalContainer->logFileManager.getFile("YOG.log");
	//assert(logFile);
	logFile=stdout;
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
		fprintf(logFile, "Failed to alocate packet!\n");
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
		fprintf(logFile, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOG::send(YOGMessageType v, Uint8 id, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	if (packet==NULL)
		fprintf(logFile, "Failed to alocate packet!\n");
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
		fprintf(logFile, "Failed to send the packet!\n");
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
		fprintf(logFile, "Failed to alocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logFile, "Failed to send the packet!\n");
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
		fprintf(logFile, "Failed to alocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logFile, "Failed to send the packet!\n");
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
		fprintf(logFile, "Failed to alocate packet!\n");

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logFile, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOG::treatPacket(IPaddress ip, Uint8 *data, int size)
{
	fprintf(logFile, "packet received by ip=%s\n", Utilities::stringIP(ip));
	if (data[2]!=0 || data[3]!=0)
	{
		fprintf(logFile, "bad packet.\n");
		return;
	}
	fprintf(logFile, "data=[%d.%d.%d.%d]\n", data[0], data[1], data[2], data[3]);
	switch (data[0])
	{
	case YMT_BAD:
		fprintf(logFile, "bad packet.\n");
	break;
	case YMT_GAME_INFO_FROM_HOST:
	{
		Uint32 uid=getUint32(data, 4);
		for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
			if (game->uid==uid)
			{
				assert(size-12<128); // TODO: have a secure test.
				
				game->numberOfPlayer=(int)getSint8(data, 8);
				game->numberOfTeam=(int)getSint8(data, 9);
				game->fileIsAMap=(bool)getSint8(data, 10);
				game->mapGenerationMethode=(int)getSint8(data, 11);
				memcpy(game->mapName, data+12, size-12);
				game->mapName[127]=0;
				if (isSelectedGame && selectedGame==uid)
				{
					newSelectedGameinfoAviable=true;
					selectedGameinfoValid=true;
				}
				printf("new game->mapName=%s\n", game->mapName);
			}
	}
	break;
	case YMT_MESSAGE:
	case YMT_PRIVATE_MESSAGE:
	case YMT_ADMIN_MESSAGE:
	case YMT_PRIVATE_RECEIPT:
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
			m.messageType=(YOGMessageType)data[0];
			m.timeout=0;
			m.TOTL=3;
			int l;
			
			l=Utilities::strmlen((char *)data+4, 256);
			memcpy(m.text, (char *)data+4, l);
			if (m.text[l-1]!=0)
				fprintf(logFile, "warning, non-zero ending text message!\n");
			m.text[255]=0;
			m.textLength=l;
			
			l=Utilities::strmlen((char *)data+4+m.textLength, 32);
			memcpy(m.userName, (char *)data+4+m.textLength, l);
			if (m.userName[l-1]!=0)
				fprintf(logFile, "warning, non-zero ending userName!\n");
			m.userName[31]=0;
			m.userNameLength=l;
			
			fprintf(logFile, "YOG:new message:%s:%s\n", m.userName, m.text);
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
				fprintf(logFile, "Message (%d) has arrived (%s)\n", messageID, mit->text);
				sendingMessages.erase(mit);
				break;
			}
			else
				fprintf(logFile, "Warning, message (%d) confirmed, but message (%d) is being sended!\n", messageID, mit->messageID);
		}
	}
	break;
	case YMT_CONNECTING:
	{
		fprintf(logFile, "YOG:connected\n");
		yogGlobalState=YGS_CONNECTED;
	}
	break;
	case YMT_CONNECTION_REFUSED:
	{
		fprintf(logFile, "YOG:connection refused!\n");
		yogGlobalState=YGS_NOT_CONNECTING;
	}
	break;
	case YMT_DECONNECTING:
	{
		fprintf(logFile, "YOG:deconnected\n");
		yogGlobalState=YGS_NOT_CONNECTING;
	}
	break;
	case YMT_SHARING_GAME:
	{
		fprintf(logFile, "YOG:game %s is shared\n", sharingGameName);
		yogSharingState=YSS_SHARED_GAME;
	}
	break;
	case YMT_STOP_SHARING_GAME:
	{
		fprintf(logFile, "YOG:game %s is unshared\n", sharingGameName);
		yogSharingState=YSS_NOT_SHARING_GAME;
	}
	break;
	case YMT_GAMES_LIST:
	{
		int nbGames=(int)getUint32(data, 4);
		if (size>48+(4+2+4+2+4+32+128)*nbGames)
		{
			fprintf(logFile, "we received a bad game list (size=%d!<=%d)\n", size, 8+(4+2+32+128)*nbGames);
			break;
		}
		fprintf(logFile, "YOG:we received a %d games list (size=%d)\n", nbGames, size);
		int index=8;
		for (int i=0; i<nbGames; i++)
		{
			GameInfo game;
			game.hostip.host=SDL_SwapLE32(getUint32safe(data, index));
			index+=4;
			game.hostip.port=SDL_SwapLE16(getUint16safe(data, index));
			index+=2;
			game.joinip.host=SDL_SwapLE32(getUint32safe(data, index));
			index+=4;
			game.joinip.port=SDL_SwapLE16(getUint16safe(data, index));
			index+=2;
			game.uid=getUint32safe(data, index);
			index+=4;
			int l;
			l=Utilities::strmlen((char *)data+index, 32);
			memcpy(game.userName, data+index, l);
			if (game.userName[l-1]!=0)
				fprintf(logFile, "warning, non-zero ending userName!\n");
			game.userName[l-1]=0;
			index+=l;
			l=Utilities::strmlen((char *)data+index, 128);
			memcpy(game.name, data+index, l);
			if (game.name[l-1]!=0)
				fprintf(logFile, "warning, non-zero ending game name!\n");
			game.name[l-1]=0;
			index+=l;
			assert(index<=size);
			
			game.numberOfPlayer=0;
			game.numberOfTeam=0;
			game.fileIsAMap=false;
			game.mapGenerationMethode=0xFF;
			memset(game.mapName, 0, 128);
			game.natSolved=false;
			games.push_back(game);
			fprintf(logFile, "index=%d.\n", index);
			fprintf(logFile, "game no=%d uid=%d name=%s host=%s\n", i, game.uid, game.name, game.userName);
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
			fprintf(logFile, "we received a bad unshared list (size=%d!=%d)\n", size, 8+4*nbUnshared);
			break;
		}
		fprintf(logFile, "YOG:we received a %d unshared list\n", nbUnshared);
		int index=8;
		for (int i=0; i<nbUnshared; i++)
		{
			Uint32 uid=getUint32(data, index);
			index+=4;
			for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
				if (game->uid==uid)
				{
					if (isSelectedGame && uid==selectedGame)
					{
						isSelectedGame=false;
						newSelectedGameinfoAviable=selectedGameinfoValid;
					}
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
	case YMT_HOST_GAME_SOCKET:
	{
		hostGameSocketTimeout=LONG_NETWORK_TIMEOUT;
		hostGameSocketTOTL=3;
		hostGameSocketReceived=true;
		fprintf(logFile, "hostGameSocketReceived\n");
	}
	break;
	case YMT_JOIN_GAME_SOCKET:
	{
		joinGameSocketTimeout=LONG_NETWORK_TIMEOUT;
		joinGameSocketTOTL=3;
		joinGameSocketReceived=true;
		fprintf(logFile, "joinGameSocketReceived\n");
	}
	break;
	case YMT_CLIENTS_LIST:
	{
		int nbClients=(int)getUint32(data, 4);
		if (size>8+(4+32)*nbClients)
		{
			fprintf(logFile, "we received a bad clients list (size=%d!<=%d)\n", size, 8+(4+32)*nbClients);
			break;
		}
		fprintf(logFile, "YOG:we received a %d clients list\n", nbClients);
		int index=8;
		for (int i=0; i<nbClients; i++)
		{
			Client client;
			client.uid=getUint32safe(data, index);
			index+=4;
			int l=Utilities::strmlen((char *)data+index, 32);
			memcpy(client.userName, data+index, l);
			client.userName[l-1]=0;
			index+=l;
			assert(index<=size);
			clients.push_back(client);
			fprintf(logFile, "client uid=%d name=%s\n", client.uid, client.userName);
		}
		newClientListAviable=true;
		send(YMT_CLIENTS_LIST, nbClients);
	}
	break;
	case YMT_LEFT_CLIENTS_LIST:
	{
		int nbClients=(int)getUint32(data, 4);
		if (size>12+4*nbClients)
		{
			fprintf(logFile, "we received a bad left clients list (size=%d!<=%d)\n", size, 8+4*nbClients);
			break;
		}
		fprintf(logFile, "YOG:we received a %d left clients list\n", nbClients);
		
		Uint32 leftClientPacketID=getUint32(data, 8);
		
		int index=12;
		for (int i=0; i<nbClients; i++)
		{
			Uint32 uid=getUint32(data, index);
			index+=4;
			for (std::list<Client>::iterator client=clients.begin(); client!=clients.end(); ++client)
				if (client->uid==uid)
				{
					fprintf(logFile, "left client uid=%d name=%s\n", client->uid, client->userName);
					clients.erase(client);
					break;
				}
		}
		newClientListAviable=true;
		
		Uint8 data[4];
		addUint32(data, leftClientPacketID, 0);
		send(YMT_LEFT_CLIENTS_LIST, nbClients, data, 4);
	}
	break;
	case YMT_CLOSE_YOG:
		fprintf(logFile, " YOG is dead (killed)!\n"); //TODO: create a desconnected method
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
		fprintf(logFile, "failed to open a socket!\n");
		return false;
	}
	
	fprintf(logFile, "resolving YOG host name...\n");
	int rv=SDLNet_ResolveHost(&serverIP, YOG_SERVER_IP, YOG_SERVER_PORT);
	if (rv==-1)
	{
		fprintf(logFile, "failed to resolve YOG host name!\n");
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
	
	hostGameSocket=NULL;
	hostGameSocketReceived=false;
	joinGameSocket=NULL;
	joinGameSocketReceived=false;
	
	clients.clear();
	newClientListAviable=false;
	
	fprintf(logFile, "enableConnection(%s)\n", userName);
	
	return true;
}

void YOG::deconnect()
{
	globalContainer->popUserName();
	
	if (yogGlobalState>=YGS_CONNECTED)
	{
		yogGlobalState=YGS_DECONNECTING;
		connectionTimeout=0;
		connectionTOTL=3;
	}
	else if (yogGlobalState>=YGS_CONNECTING)
	{
		yogGlobalState=YGS_DECONNECTING;
		connectionTimeout=0;
		connectionTOTL=1;
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
					fprintf(logFile, "unable to deconnect!\n");
				}
				else
				{
					fprintf(logFile, "sending deconnection request...\n");
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
					fprintf(logFile, "unable to connect!\n");
				}
				else
				{
					fprintf(logFile, "sending connection request...\n");
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
						fprintf(logFile, "failed to share game!\n");
						yogSharingState=YSS_NOT_SHARING_GAME;
					}
					else
					{
						fprintf(logFile, "sending share game info... (%s)\n", sharingGameName);
						sharingGameTimeout=DEFAULT_NETWORK_TIMEOUT;
						send(YMT_SHARING_GAME, (Uint8 *)sharingGameName, Utilities::strmlen(sharingGameName, 128));
					}
				}
			break;
			case YSS_SHARED_GAME:
				//cool
			break;
			default:
				fprintf(logFile, "warning, bad yogSharingState!\n");
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
						fprintf(logFile, "failed to send a message!\n");
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
				fprintf(logFile, "Connection lost to YOG!\n"); //TODO!
			else
				send(YMT_CONNECTION_PRESENCE);
			presenceTimeout=LONG_NETWORK_TIMEOUT;
		}
		
		if (yogSharingState==YSS_STOP_SHARING_GAME && sharingGameTimeout--<=0)
		{
			if (sharingGameTOTL--<=0)
			{
				fprintf(logFile, "failed to unshare game!\n");
				yogSharingState=YSS_NOT_SHARING_GAME;
			}
			else
			{
				fprintf(logFile, "Sending a stop sharing game ...!\n");
				send(YMT_STOP_SHARING_GAME);
				sharingGameTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
		}
		
		if (hostGameSocket && hostGameSocketTimeout--<=0)
		{
			//hostGameSocketReceived no more used.
			if (hostGameSocketTOTL--<=0)
				fprintf(logFile, "Unable to deliver the hostGameSocket to YOG!\n"); // TODO!
			hostGameSocketTimeout=LONG_NETWORK_TIMEOUT;
			fprintf(logFile, "Sending the hostGameSocket to YOG ...\n");
			UDPpacket *packet=SDLNet_AllocPacket(36);
			assert(packet);
			packet->len=36;
			char data[36];
			data[0]=YMT_HOST_GAME_SOCKET;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			strncpy(data+4, userName, 32);
			data[35]=0;
			memcpy(packet->data, data, 36);//TODO: minimise size
			packet->address=serverIP;
			packet->channel=-1;
			bool sucess=SDLNet_UDP_Send(socket, -1, packet)==1;
			if (!sucess)
				fprintf(logFile, "failed to send the hostGameSocket to YOG!\n");
			SDLNet_FreePacket(packet);
		}
		
		if (joinGameSocket && joinGameSocketTimeout--<=0)
		{
			//joinGameSocketReceived no more used
			if (joinGameSocketTOTL--<=0)
				fprintf(logFile, "Unable to deliver the joinGameSocket to YOG!\n"); // TODO!
			joinGameSocketTimeout=LONG_NETWORK_TIMEOUT;
			fprintf(logFile, "Sending the joinGameSocket to YOG ...\n");
			UDPpacket *packet=SDLNet_AllocPacket(36);
			assert(packet);
			packet->len=36;
			char data[36];
			data[0]=YMT_JOIN_GAME_SOCKET;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			strncpy(data+4, userName, 32);
			data[35]=0;
			memcpy(packet->data, data, 36);//TODO: minimise size
			packet->address=serverIP;
			packet->channel=-1;
			bool sucess=SDLNet_UDP_Send(joinGameSocket, -1, packet)==1;
			if (!sucess)
				fprintf(logFile, "failed to send the joinGameSocket to YOG!\n");
			SDLNet_FreePacket(packet);
		}
		
		if (isSelectedGame && !newSelectedGameinfoAviable && selectedGameinfoTimeout--<0 && selectedGameinfoTOTL-->0)
		{
			sendGameinfoRequest();
			selectedGameinfoTimeout=DEFAULT_NETWORK_TIMEOUT;
		}
		if (isSelectedGame)
		{
			int v;
			char gameName[128];
			char serverNickName[32];
			if (lan.receive(&v, gameName, serverNickName))
			{
				fprintf(logFile, "received broadcast response v=(%d), gameName=(%s), serverNickName=(%s).\n", v, gameName, serverNickName);
				for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
					if ((strncmp(gameName, game->name, 128)==0)
						&& (strncmp(serverNickName, game->userName, 32)==0))
					{
						fprintf(logFile, "Solved a NAT from %s to %s.\n", Utilities::stringIP(game->hostip), Utilities::stringIP(lan.getSenderIP()));
						game->hostip.host=lan.getSenderIP();
						game->hostip.port=SDL_SwapBE16(SERVER_PORT);
						game->natSolved=true;
						if (game->uid==selectedGame)
						{
							selectedGameinfoTOTL++;
							selectedGameinfoTimeout=2;
						}
					}
			}
		}
		
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(YOG_MAX_PACKET_SIZE);
		assert(packet);
		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			treatPacket(packet->address, packet->data, packet->len);
			/*fprintf(logFile, "Packet received.\n");
			fprintf(logFile, "packet=%d\n", (int)packet);
			fprintf(logFile, "packet->channel=%d\n", packet->channel);
			fprintf(logFile, "packet->len=%d\n", packet->len);
			fprintf(logFile, "packet->maxlen=%d\n", packet->maxlen);
			fprintf(logFile, "packet->status=%d\n", packet->status);
			fprintf(logFile, "packet->address=%x,%d\n", packet->address.host, packet->address.port);
			fprintf(logFile, "SDLNet_ResolveIP(ip)=%s\n", SDLNet_ResolveIP(&packet->address));
			fprintf(logFile, "packet->data=[%d.%d.%d.%d]\n", packet->data[0], packet->data[1], packet->data[2], packet->data[3]);*/
		}
		SDLNet_FreePacket(packet);
	}
}

void YOG::sendGameinfoRequest()
{
	assert(isSelectedGame);
	
	for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
		if (game->uid==selectedGame)
		{
			if (game->hostip.host==0)
				return;
			UDPpacket *packet=SDLNet_AllocPacket(8);
			if (packet==NULL)
				return;
			packet->len=8;
			char data[8];
			data[0]=YOG_CLIENT_REQUESTS_GAME_INFO;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addSint32(data, game->uid, 4);
			memcpy(packet->data, data, 8);
			packet->address=game->hostip;
			packet->channel=-1;
			bool sucess=SDLNet_UDP_Send(socket, -1, packet)==1;
			if (!sucess)
				fprintf(logFile, "failed to send packet!\n");
			else
				printf("sendGameinfoRequest() to ip=%s\n",  Utilities::stringIP(game->hostip));
			SDLNet_FreePacket(packet);
			
			if (!game->natSolved && enableLan)
			{
				lan.send(BROADCAST_REQUEST);
				printf("BROADCAST_REQUEST\n");
			}
			break;
		}
	
	
}

void YOG::shareGame(const char *gameName)
{
	yogSharingState=YSS_SHARING_GAME;
	strncpy(sharingGameName, gameName, 128);
	sharingGameName[127]=0;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
	fprintf(logFile, "shareGame\n");
}

void YOG::unshareGame()
{
	yogSharingState=YSS_STOP_SHARING_GAME;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
	
	hostGameSocket=NULL;
	hostGameSocketReceived=false;
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

void YOG::sendMessage(const char *message)
{
	lastMessageID++;
	Message m;
	m.messageID=lastMessageID;
	strncpy(m.text, message, MAX_MESSAGE_SIZE);
	m.text[MAX_MESSAGE_SIZE-1]=0;
	m.timeout=0;
	m.TOTL=3;
	m.textLength=Utilities::strmlen(m.text, 256);
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

bool YOG::selectGame(Uint32 uid)
{
	for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
		if (game->uid==uid)
		{
			selectedGame=uid;
			isSelectedGame=true;
			newSelectedGameinfoAviable=false;
			selectedGameinfoValid=false;
			selectedGameinfoTimeout=2;
			selectedGameinfoTOTL=3;
			return true;
		}
	
	selectedGame=uid;
	isSelectedGame=false;
	newSelectedGameinfoAviable=false;
	selectedGameinfoValid=false;
	return false;
}

bool YOG::selectedGameinfoUpdated(bool reset)
{
	bool rv=newSelectedGameinfoAviable;
	if (reset)
		newSelectedGameinfoAviable=false;
	return rv;
}

YOG::GameInfo *YOG::getSelectedGameInfo()
{
	if (isSelectedGame)
		for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
			if (game->uid==selectedGame)
				return &*game;
	return NULL;
}

void YOG::gameStarted()
{
	assert(!joinedGame);
	fprintf(logFile, "gameStarted()\n");
	if (yogGlobalState==YGS_CONNECTED)
		yogGlobalState=YGS_PLAYING;
	else
		fprintf(logFile, "Warning gameStarted() in a bad yogGlobalState=%d!\n", yogGlobalState);
}

void YOG::gameEnded()
{
	fprintf(logFile, "gameEnded()\n");
	if (yogGlobalState==YGS_PLAYING)
		yogGlobalState=YGS_CONNECTED;
	else
		fprintf(logFile, "Warning gameEnded() in a bad yogGlobalState=%d!\n", yogGlobalState);
	joinGameSocket=NULL;
	joinGameSocketReceived=false;
}

void YOG::setHostGameSocket(UDPsocket socket)
{
	fprintf(logFile, "setHostGameSocket()\n");
	if (yogSharingState<YSS_SHARING_GAME)
		fprintf(logFile, "Warning setHostGameSocket() in a bad yogSharingState=%d!\n", yogSharingState);
	hostGameSocket=socket;
	hostGameSocketTimeout=0;
	hostGameSocketTOTL=3;
}

bool YOG::hostGameSocketSet()
{
	return hostGameSocketReceived;
}

void YOG::setJoinGameSocket(UDPsocket socket)
{
	fprintf(logFile, "setJoinGameSocket(), %d\n", yogSharingState);
	if (yogSharingState<YSS_SHARING_GAME && yogSharingState!=YSS_NOT_SHARING_GAME)
		fprintf(logFile, "Warning setJoinGameSocket() in a bad yogSharingState=%d!\n", yogSharingState);
	joinGameSocket=socket;
	joinGameSocketTimeout=0;
	joinGameSocketTOTL=3;
}

bool YOG::joinGameSocketSet()
{
	return joinGameSocketReceived;
}
