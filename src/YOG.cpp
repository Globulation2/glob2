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
#include "LogFileManager.h"

// If you don't have SDL_net 1.2.5 some features won't be aviable.
#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST (SDL_SwapBE32(0x7F000001))
#endif

YOG::YOG(LogFileManager *logFileManager)
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
	isConnectedToGameHost=false;
	
	isSelectedGame=false;
	newSelectedGameinfoAviable=false;
	selectedGameinfoValid=false;
	selectedGameinfoTimeout=0;
	selectedGameinfoTOTL=0;
	
	uid=0;
	
	if (logFileManager)
	{
		logFile=logFileManager->getFile("YOG.log");
		assert(logFile);
	}
	else
		logFile=stdout;
	fprintf(logFile, "new YOG");
	
	enableLan=true;
	unjoining=false;
	
	connectionLost=false;
}

YOG::~YOG()
{
	if (socket)
		SDLNet_UDP_Close(socket);
	
	//TODO: check connection with YOGServer.
}

void YOG::send(Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);
	assert(packet);
	packet->len=size;
	memcpy((char *)packet->data, data, size);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logFile, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOG::send(YOGMessageType v, Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size+4);
	assert(packet);
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
	assert(packet);
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
	assert(packet);

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
	assert(packet);

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
	assert(packet);

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=serverIP;
	packet->channel=-1;
	int rv=SDLNet_UDP_Send(socket, -1, packet);
	if (rv!=1)
		fprintf(logFile, "Failed to send the packet!\n");
	SDLNet_FreePacket(packet);
}

void YOG::send(UDPsocket socket, IPaddress ip, Uint8 v)
{
	Uint8 data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	UDPpacket *packet=SDLNet_AllocPacket(4);
	assert(packet);

	packet->len=4;
	memcpy((char *)packet->data, data, 4);
	packet->address=ip;
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
		if (size>(4+4+4+64))
			printf("Warning, bad YMT_GAME_INFO_FROM_HOST packet received from ip=(%s)\n", Utilities::stringIP(ip));
		else
			for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
				if (game->uid==uid)
				{
					game->numberOfPlayer=(int)getSint8(data, 8);
					game->numberOfTeam=(int)getSint8(data, 9);
					game->fileIsAMap=(bool)getSint8(data, 10);
					game->mapGenerationMethode=(int)getSint8(data, 11);
					memcpy(game->mapName, data+12, size-12);
					game->mapName[63]=0;
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
			m.gameGuiPainted=false;
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
			
			for (std::list<Message>::iterator mit=recentlyReceivedMessages.begin(); mit!=recentlyReceivedMessages.end(); ++mit)
				if (mit->messageID==messageID && (strncmp(m.text, mit->text, m.textLength)==0))
				{
					already=true;
					break;
				}
			if (!already)
			{
				fprintf(logFile, "new message:%s:%s\n", m.userName, m.text);
				receivedMessages.push_back(m);
				recentlyReceivedMessages.push_back(m);
				if (recentlyReceivedMessages.size()>64)
					recentlyReceivedMessages.pop_front();
			}
		}
	}
	break;
	case YMT_PRIVATE_RECEIPT:
	{
		if (size<8)
		{
			fprintf(logFile, "bad size for a YMT_PRIVATE_RECEIPT packet (size=%d)\n", size);
			break;
		}
		Uint8 receiptID=data[4];
		Uint8 messageID=data[5];
		Uint8 sizeAddrs=data[6];
		assert(size-8==sizeAddrs);
		send(YMT_PRIVATE_RECEIPT, receiptID);
		
		fprintf(logFile, "YMT_PRIVATE_RECEIPT packet receiptID=%d, messageID=%d, sizeAddrs=%d\n", receiptID, messageID, sizeAddrs);
		
		if (sizeAddrs==0)
		{
			for (std::list<Message>::iterator mit=recentlySentMessages.begin(); mit!=recentlySentMessages.end(); ++mit)
				if (mit->messageID==messageID)
				{
					fprintf(logFile, "Message (%s) not delivered!\n", mit->text);
					//TODO: create a message for this
					recentlySentMessages.erase(mit);
					break;
				}
		}
		else
			for (std::list<Message>::iterator mit=recentlySentMessages.begin(); mit!=recentlySentMessages.end(); ++mit)
				if (mit->messageID==messageID)
				{
					for (int i=0; i<sizeAddrs; i++)
					{
						Message m;
						m.messageID=messageID;
						m.messageType=YMT_PRIVATE_RECEIPT;
						m.timeout=0;
						m.TOTL=3;
						m.gameGuiPainted=false;
						int l;
						char *text=mit->text+data[8+i]+4;
						l=Utilities::strmlen(text, 256);
						memcpy(m.text, text, l);
						if (m.text[l-1]!=0)
							fprintf(logFile, "warning, non-zero ending text message!\n");
						m.text[255]=0;
						m.textLength=l;

						l=data[8+i];
						if (l<32)
							l++;
						char *userName=mit->text+3;
						memcpy(m.userName, userName, l);
						m.userName[l-1]=0;
						m.userNameLength=l;

						fprintf(logFile, "new YMT_PRIVATE_RECEIPT message:%s:%s\n", m.userName, m.text);
						receivedMessages.push_back(m);
					}
					recentlySentMessages.erase(mit);
					fprintf(logFile, "Message (%d) removed from recentlySentMessages\n", messageID);
					break;
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
				recentlySentMessages.push_back(*mit);
				if (recentlySentMessages.size()>64)
					recentlySentMessages.pop_front();
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
		if (size==8)
		{
			yogGlobalState=YGS_CONNECTED;
			uid=getUint32(data, 4);
			fprintf(logFile, "connected (uid=%d)\n", uid);
		}
		else
			fprintf(logFile, "bad YOG-connected packet\n");
	}
	break;
	case YMT_CONNECTION_REFUSED:
	{
		fprintf(logFile, "connection refused! (sameName=%d, YOG_PROTOCOL_VERSION=%d, PROTOCOL_VERSION=%d)\n", data[4], data[5], YOG_PROTOCOL_VERSION);
		yogGlobalState=YGS_NOT_CONNECTING;
	}
	break;
	case YMT_DECONNECTING:
	{
		fprintf(logFile, "deconnected\n");
		yogGlobalState=YGS_NOT_CONNECTING;
	}
	break;
	case YMT_SHARING_GAME:
	{
		selectedGame=getUint32(data, 4);
		if (yogSharingState==YSS_SHARING_GAME)
		{
			fprintf(logFile, "game %s is shared (selectedGame=%d)\n", sharingGameName, selectedGame);
			yogSharingState=YSS_SHARED_GAME;
		}
		else
			fprintf(logFile, "Warning, YMT_SHARING_GAME message (selectedGame=%d), while in a bad state (yss=%d)\n", selectedGame, yogSharingState);
	}
	break;
	case YMT_STOP_SHARING_GAME:
	{
		fprintf(logFile, "game %s is unshared\n", sharingGameName);
		yogSharingState=YSS_NOT_SHARING_GAME;
	}
	break;
	case YMT_STOP_PLAYING_GAME:
	{
		fprintf(logFile, "game is unjoined\n");
		unjoining=false;
	}
	break;
	case YMT_GAMES_LIST:
	{
		int nbGames=(int)getUint32(data, 4);
		if (size>8+(4+2+4+4+32+64)*nbGames)
		{
			fprintf(logFile, "we received a bad game list (size=%d!<=%d)\n", size, 8+(4+2+4+4+32+64)*nbGames);
			break;
		}
		fprintf(logFile, "we received a %d games list (size=%d)\n", nbGames, size);
		int index=8;
		bool isAnyCompleteNewGame=false; // Used to detect if we don't have the game-host-uid in our current (YOG-)clients list.
		for (int i=0; i<nbGames; i++)
		{
			GameInfo game;
			game.hostip.host=SDL_SwapLE32(getUint32safe(data, index));
			index+=4;
			game.hostip.port=SDL_SwapLE16(getUint16safe(data, index));
			index+=2;
			game.uid=getUint32safe(data, index);
			index+=4;
			
			Uint32 huid=getUint32safe(data, index);
			index+=4;
			
			game.huid=huid;
			game.userName[0]=0;
			for (std::list<Client>::iterator clienti=clients.begin(); clienti!=clients.end(); ++clienti)
				if (clienti->uid==huid)
				{
					memcpy(game.userName, clienti->userName, 32);
					game.userName[31]=0;
					isAnyCompleteNewGame=true;
					break;
				}
			
			int l=Utilities::strmlen((char *)data+index, 64);//TODO: set game's name's length to 64 everywhere !
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
			memset(game.mapName, 0, 64);
			game.natSolved=false;
			games.push_back(game);
			fprintf(logFile, "index=%d.\n", index);
			fprintf(logFile, "game no=%d uid=%d name=%s host=%s\n", i, game.uid, game.name, game.userName);
		}
		assert(index==size);
		if (isAnyCompleteNewGame)
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
		fprintf(logFile, "we received a %d unshared list\n", nbUnshared);
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
		if (connectionLost)
		{
			yogGlobalState=YGS_CONNECTING;
			connectionTimeout=8;
			connectionTOTL=3;
		}
		connectionLost=false;
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
		int nbClients=(int)getUint8(data, 4);
		if (size>5+(4+32+2)*nbClients)
		{
			fprintf(logFile, "we received a bad clients list (size=%d!<=%d)\n", size, 8+(4+32+1)*nbClients);
			break;
		}
		Uint8 clientsPacketID=getUint8(data, 5);
		fprintf(logFile, "we received a %d clients list, cpid=%d\n", nbClients, clientsPacketID);
		int index=6;
		for (int i=0; i<nbClients; i++)
		{
			Client client;
			Uint32 cuid=getUint32safe(data, index);
			client.uid=cuid;
			index+=4;
			int l=Utilities::strmlen((char *)data+index, 32);
			memcpy(client.userName, data+index, l);
			client.userName[l-1]=0;
			index+=l;
			client.playing=data[index];
			index++;
			assert(index<=size);
			bool allready=false;
			for (std::list<Client>::iterator clienti=clients.begin(); clienti!=clients.end(); ++clienti)
				if (clienti->uid==client.uid)
				{
					allready=true;
					break;
				}
			if (!allready)
			{
				clients.push_back(client);
				
				for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
					if (game->userName[0]==0 && game->huid==cuid)
					{
						strncpy(game->userName, client.userName, 32);
						newGameListAviable=true;
						fprintf(logFile, "Game (%s) from (%s) newly aviable!\n", game->name, game->userName);
						break;
					}
			}
			fprintf(logFile, "client uid=%d name=%s\n", client.uid, client.userName);
		}
		newClientListAviable=true;
		
		Uint8 data[2];
		addUint8(data, (Uint8)nbClients, 0);
		addUint8(data, clientsPacketID, 1);
		send(YMT_CLIENTS_LIST, data, 2);
	}
	break;
	case YMT_UPDATE_CLIENTS_LIST:
	{
		int nbClients=(int)getUint8(data, 4);
		if (size>6+6*nbClients)
		{
			fprintf(logFile, "we received a bad update clients list (size=%d!<=%d)\n", size, 8+4*nbClients);
			break;
		}
		
		Uint8 clientsUpdatePacketID=getUint8(data, 5);
		fprintf(logFile, "we received a %d update clients list, cupid=%d\n", nbClients, clientsUpdatePacketID);
		
		int index=6;
		for (int i=0; i<nbClients; i++)
		{
			Uint32 uid=getUint32(data, index);
			index+=4;
			Uint16 change=getUint16(data, index);
			index+=2;
			for (std::list<Client>::iterator client=clients.begin(); client!=clients.end(); ++client)
				if (client->uid==uid)
				{
					if (change==CUP_LEFT)
					{
						fprintf(logFile, "left client uid=%d name=%s\n", client->uid, client->userName);
						clients.erase(client);
					}
					else if (change==CUP_PLAYING)
					{
						fprintf(logFile, "client uid=%d name=%s playing\n", client->uid, client->userName);
						client->playing=true;
					}
					else if (change==CUP_NOT_PLAYING)
					{
						fprintf(logFile, "client uid=%d name=%s not playing\n", client->uid, client->userName);
						client->playing=false;
					}
					else
						assert(false);
					break;
				}
			
		}
		newClientListAviable=true;
		
		Uint8 data[2];
		addUint8(data, (Uint8)nbClients, 0);
		addUint8(data, clientsUpdatePacketID, 1);
		send(YMT_UPDATE_CLIENTS_LIST, data, 2);
	}
	break;
	case YMT_CLOSE_YOG:
		fprintf(logFile, " YOG is dead (killed)!\n"); //TODO: create a deconnected method
	break;
	case YMT_PLAYERS_WANTS_TO_JOIN:
	{
		int n=(size-4)/10;
		if (n*10+4!=size)
		{
			fprintf(logFile, "warning, bad YMT_PLAYERS_WANTS_TO_JOIN packet!\n");
			break;
		}
		int index=4;
		int sendSize=4*n+4;
		int sendIndex=4;
		Uint8 sendData[sendSize];
		sendData[0]=YMT_PLAYERS_WANTS_TO_JOIN;
		sendData[1]=0;
		sendData[2]=0;
		sendData[3]=0;
		for (int i=0; i<n; i++)
		{
			Joiner joiner;
			joiner.uid=getUint32(data, index);
			index+=4;
			joiner.ip.host=SDL_SwapLE32(getUint32(data, index));
			index+=4;
			joiner.ip.port=SDL_SwapLE16(getUint16(data, index));
			index+=2;
			joiner.timeout=i;
			joiner.TOTL=3;
			joiner.connected=false;
			bool already=false;
			for (std::list<Joiner>::iterator ji=joiners.begin(); ji!=joiners.end(); ji++)
				if (ji->uid==joiner.uid)
				{
					ji->timeout=i;
					ji->TOTL=3;
					already=true;
				}
			if (!already)
			{
				joiners.push_back(joiner);
				fprintf(logFile, "received a new joinerip (%s) uid=(%d)\n", Utilities::stringIP(joiner.ip), joiner.uid);
			}
			else
				fprintf(logFile, "received a useless joinerip (%s)\n", Utilities::stringIP(joiner.ip));
			
			addUint32(sendData, joiner.uid, sendIndex);
			sendIndex+=4;
		}
		assert(index==size);
		assert(sendIndex==sendSize);
		if (n>0)
			send(sendData, sendSize);
	}
	break;
	case YMT_BROADCAST_RESPONSE_LAN:
	case YMT_BROADCAST_RESPONSE_YOG:
	{
		if (size!=68)
		{
			fprintf(logFile, "Warning, bad size for a gameHostBroadcastResponse (%d).\n", size);
			return;
		}
		int v=data[0];
		char gameName[64];
		char serverNickName[32];
		
		strncpy(gameName, (char *)&data[4], 32);
		strncpy(serverNickName, (char *)&data[36], 32);

		fprintf(logFile, "received broadcast response v=(%d), gameName=(%s), serverNickName=(%s).\n", v, gameName, serverNickName);
		for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
			if ((strncmp(gameName, game->name, 64)==0)
				&& (strncmp(serverNickName, game->userName, 32)==0))
			{
				fprintf(logFile, "Solved a NAT from (%s) to (%s).\n", Utilities::stringIP(game->hostip), Utilities::stringIP(ip));
				game->hostip=ip;
				if (game->hostip.port!=SDL_SwapBE16(GAME_SERVER_PORT))
					fprintf(logFile, "Warning, the server port is not the standard port! Should not happen! (%d)\n", serverIP.port);
				game->natSolved=true;
				if (isSelectedGame && game->uid==selectedGame)
				{
					selectedGameinfoTOTL++;
					selectedGameinfoTimeout=2;
				}
			}
			
		
	}
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
	joiners.clear();
	
	clients.clear();
	newClientListAviable=false;
	
	uid=0;
	
	fprintf(logFile, "enableConnection(%s)\n", userName);
	
	return true;
}

void YOG::deconnect()
{
	globalContainer->popUserName();
	
	if (connectionLost)
	{
		yogGlobalState=YGS_NOT_CONNECTING;
	}
	else if (yogGlobalState>=YGS_CONNECTED)
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
					
					int tl=Utilities::strmlen(userName, 32);
					
					UDPpacket *packet=SDLNet_AllocPacket(8+tl);
					assert(packet);
					Uint8 sdata[8+tl];
					sdata[0]=YMT_CONNECTING;
					sdata[1]=0;
					sdata[2]=0;
					sdata[3]=0;
					addUint32(sdata, YOG_PROTOCOL_VERSION, 4);
					memcpy(sdata+8, (Uint8 *)userName, tl);
					memcpy((char *)packet->data, sdata, 8+tl);
					packet->len=8+tl;
					packet->address=serverIP;
					packet->channel=-1;
					int rv=SDLNet_UDP_Send(socket, -1, packet);
					if (rv!=1)
						fprintf(logFile, "Failed to send the packet!\n");
					SDLNet_FreePacket(packet);
					
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
						send(YMT_SHARING_GAME, (Uint8 *)sharingGameName, Utilities::strmlen(sharingGameName, 64));
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
			
			if (unjoining && unjoinTimeout--<0)
			{
				fprintf(logFile, "sending YMT_STOP_PLAYING_GAME to YOG.\n");
				send(YMT_STOP_PLAYING_GAME);
				unjoinTimeout=DEFAULT_NETWORK_TIMEOUT;
			}
			
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
			{
				fprintf(logFile, "Connection lost to YOG!\n"); //TODO!
				connectionLost=true;
			}
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
		
		if (hostGameSocket && yogGlobalState<YGS_PLAYING && hostGameSocketTimeout--<=0 && uid)
		{
			if (hostGameSocketTOTL--<=0)
				fprintf(logFile, "Unable to deliver the hostGameSocket to YOG!\n"); // TODO!
			if (hostGameSocketReceived || hostGameSocketTOTL<=0)
				hostGameSocketTimeout=LONG_NETWORK_TIMEOUT;
			else
				hostGameSocketTimeout=DEFAULT_NETWORK_TIMEOUT;
				
			if (isSelectedGame)
				fprintf(logFile, "Sending the hostGameSocket to YOG (selectedGame=%d)...\n", selectedGame);
			else
				fprintf(logFile, "Sending the hostGameSocket to YOG ...\n");
			UDPpacket *packet=SDLNet_AllocPacket(8);
			assert(packet);
			packet->len=8;
			char data[8];
			data[0]=YMT_HOST_GAME_SOCKET;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addUint32(data, uid, 4);
			memcpy(packet->data, data, 8);
			packet->address=serverIP;
			packet->channel=-1;
			bool sucess=SDLNet_UDP_Send(hostGameSocket, -1, packet)==1;
			if (!sucess)
				fprintf(logFile, "failed to send the hostGameSocket to YOG!\n");
			SDLNet_FreePacket(packet);
		}
		
		if (joinGameSocket && !joinGameSocketReceived && joinGameSocketTimeout--<=0 && isSelectedGame && uid)
		{
			if (joinGameSocketTOTL--<=0)
				fprintf(logFile, "Unable to deliver the joinGameSocket to YOG!\n"); // TODO!
			if (joinGameSocketReceived || joinGameSocketTOTL<=0)
				joinGameSocketTimeout=LONG_NETWORK_TIMEOUT;
			else
				joinGameSocketTimeout=DEFAULT_NETWORK_TIMEOUT;
			fprintf(logFile, "Sending the joinGameSocket to YOG (selectedGame=%d)...\n", selectedGame);
			UDPpacket *packet=SDLNet_AllocPacket(12);
			assert(packet);
			packet->len=12;
			char data[12];
			data[0]=YMT_JOIN_GAME_SOCKET;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addUint32(data, uid, 4);
			addUint32(data, selectedGame, 8);
			memcpy(packet->data, data, 12);
			packet->address=serverIP;
			packet->channel=-1;
			bool sucess=SDLNet_UDP_Send(joinGameSocket, -1, packet)==1;
			if (!sucess)
				fprintf(logFile, "failed to send the joinGameSocket to YOG!\n");
			SDLNet_FreePacket(packet);
		}
		
		if (isSelectedGame && !newSelectedGameinfoAviable && selectedGameinfoTimeout--<0 && selectedGameinfoTOTL-->0)
		{
			selectedGameinfoTimeout=DEFAULT_NETWORK_TIMEOUT;
			sendGameinfoRequest();
		}
		
		if (hostGameSocket && joiners.size()>0)
			for (std::list<Joiner>::iterator joiner=joiners.begin(); joiner!=joiners.end(); ++joiner)
				if (!joiner->connected && joiner->timeout--<0 && joiner->TOTL-->0)
				{
					send(hostGameSocket, joiner->ip, SERVER_FIREWALL_EXPOSED);
					fprintf(logFile, "hostGameSocket send SERVER_FIREWALL_EXPOSED to (%s), TOTL=(%d)\n", Utilities::stringIP(joiner->ip), joiner->TOTL);
					joiner->timeout=DEFAULT_NETWORK_TIMEOUT;
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
				fprintf(logFile, "sendGameinfoRequest() to ip=%s\n",  Utilities::stringIP(game->hostip));
			SDLNet_FreePacket(packet);
			if (!game->natSolved && enableLan)
			{
				UDPpacket *packet=SDLNet_AllocPacket(4);

				assert(packet);

				packet->channel=-1;
				packet->address.host=INADDR_BROADCAST;
				packet->address.port=SDL_SwapBE16(GAME_SERVER_PORT);
				packet->len=4;
				packet->data[0]=YMT_BROADCAST_REQUEST;
				packet->data[1]=0;
				packet->data[2]=0;
				packet->data[3]=0;
				if (SDLNet_UDP_Send(socket, -1, packet)==1)
				{
					fprintf(logFile, "Successed to send a BROADCAST_REQUEST packet.\n");
					//TODO: send broadcasting for any game, not only for selected games. broadcastTimeout=DEFAULT_NETWORK_TIMEOUT;
				}
				else
				{
					enableLan=false;
					fprintf(logFile, "failed to send a BROADCAST_REQUEST packet!\n");
				}
				SDLNet_FreePacket(packet);
			}
			else
				fprintf(logFile, "No broadcasting now, game->natSolved=%d, enableLan=%d.\n", game->natSolved, enableLan);
			break;
		}
	
	
}

void YOG::shareGame(const char *gameName)
{
	yogSharingState=YSS_SHARING_GAME;
	strncpy(sharingGameName, gameName, 64);
	sharingGameName[63]=0;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
	fprintf(logFile, "shareGame(%s)\n", gameName);
}

void YOG::unshareGame()
{
	yogSharingState=YSS_STOP_SHARING_GAME;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
	
	hostGameSocket=NULL;
	hostGameSocketReceived=false;
	joiners.clear();
	
	unjoinTimeout=0;
	unjoining=true;
	fprintf(logFile, "unshareGame()\n");
}

void YOG::joinGame()
{
	fprintf(logFile, "joinGame() was=%d\n", joinedGame);
	assert(!unjoining);
	joinedGame=true;
}

void YOG::unjoinGame()
{
	assert(joinedGame);
	joinedGame=false;
	
	joinGameSocketReceived=false;
	
	unjoinTimeout=0;
	unjoining=true;
	fprintf(logFile, "unjoinGame()\n");
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
	assert(isConnectedToGameHost);
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
	isConnectedToGameHost=false;
}

void YOG::setHostGameSocket(UDPsocket socket)
{
	fprintf(logFile, "setHostGameSocket()\n");
	if (yogSharingState<YSS_SHARING_GAME)
		fprintf(logFile, "Warning setHostGameSocket() in a bad yogSharingState=%d!\n", yogSharingState);
	hostGameSocket=socket;
	hostGameSocketTimeout=0;
	hostGameSocketTOTL=3;
	unjoining=false;
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
	unjoining=false;
}

bool YOG::joinGameSocketSet()
{
	return joinGameSocketReceived;
}

void YOG::joinerConnected(IPaddress ip)
{
	for (unsigned i=0; i<joiners.size(); i++)
		for (std::list<Joiner>::iterator joiner=joiners.begin(); joiner!=joiners.end(); ++joiner)
			if (joiner->ip.host==ip.host && joiner->ip.port==ip.port)
			{
				joiner->connected=true;
				fprintf(logFile, "joiner(%s) connected\n", Utilities::stringIP(joiner->ip));
			}
}

void YOG::connectedToGameHost()
{
	isConnectedToGameHost=true;
	fprintf(logFile, "connectedToGameHost()\n");
}

IPaddress YOG::ipFromUserName(char userName[32])
{
	bool found=false;
	Uint32 uid=0;
	fprintf(logFile, "ipFromUserName(%s)..\n", userName);
	for (std::list<Client>::iterator client=clients.begin(); client!=clients.end(); ++client)
		if (strncmp(client->userName, userName, 32)==0)
		{
			found=true;
			uid=client->uid;
			break;
		}
	fprintf(logFile, "ipFromUserName found=(%d), uid=(%d)\n", found, uid);
	for (std::list<Joiner>::iterator joiner=joiners.begin(); joiner!=joiners.end(); ++joiner)
		if (joiner->uid==uid)
		{
			fprintf(logFile, "ipFromUserName=(%s)\n", Utilities::stringIP(joiner->ip));
			return joiner->ip;
		}
	IPaddress ip;
	ip.host=0;
	ip.port=0;
	return ip;
}
