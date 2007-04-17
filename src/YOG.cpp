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
 
// TODO:remove useless includes

/**
 * global todo list for this file
 *
 * @todo remove useless includes
 * @todo anything that has fix1 is a spelling mistake, but needs to be
 *		 verified that if it's removed it won't kill the program
 * @todo logging states var verbosisty = INFO or DEBUG or WARN, etc.
 * @todo tell Kyle what YMT and YSS stand for
 */

#include "../gnupg/sha1.c"


#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL.h>
#include <SDL_endian.h>
#include <SDL_image.h>
#include <SDL_net.h>
#else
#include <Types.h>
#endif
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

#include <FormatableString.h>
#include <StringTable.h>

// If you don't have SDL_net 1.2.5 some features won't be available.
#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST (SDL_SwapBE32(0x7F000001))
#endif

YOG::YOG()
{
	assert(globalContainer);
	
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
	newSelectedGameinfoAvailable=false;
	selectedGameinfoValid=false;
	selectedGameinfoTimeout=0;
	selectedGameinfoTOTL=0;
	
	uid=0;
	logFile=globalContainer->logFileManager->getFile("YOG.log");
	if (logFile==NULL)
		logFile=stdout;
	fprintf(logFile, "new YOG\n");
	
	enableLan=true;
	unjoining=false;
	unjoiningConfirmed=true;
	
	connectionLost=false;
	
	externalStatusState=YESTS_CREATED;
	
	memset(userName, 0, 32);
	memset(passWord, 0, 32);
	memset(xorpassw, 0, 32);
}

/**
 * @todo check connection with YOGServer
 */
YOG::~YOG()
{
	if (socket)
		SDLNet_UDP_Close(socket);
}

/**
 * Takes all he lovely little UDP data and sends it off!
 * @param	Uint8 *data 
 * @param	int size
 */
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

/**
 * Takes all the lovely little UDP data and sends it off!
 *
 * @param YOGMessageType v 
 * @param Uint8 *data 
 * @param int size
 */
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
		if (size>(20+64))
			printf("Warning, bad YMT_GAME_INFO_FROM_HOST packet received from ip=(%s)\n", Utilities::stringIP(ip));
		else
			for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
				if (game->uid==uid)
				{
					game->numberOfPlayer=(int)(Sint8)data[8];
					game->numberOfTeam=(int)(Sint8)data[9];
					game->fileIsAMap=(bool)data[10];
					game->mapGenerationMethode=(int)(Sint8)data[11];
					if (data[12]!=0 || data[13]!=0 || data[14]!=0)
						printf("Warning, bad pad YMT_GAME_INFO_FROM_HOST packet received from ip=(%s)\n", Utilities::stringIP(ip));
					game->netProtocolVersion=(int)(Sint8)data[15];
					game->configCheckSum=getUint32(data, 16);
					memcpy(game->mapName, data+20, size-20);
					game->mapName[63]=0;
					if (isSelectedGame && selectedGame==uid)
					{
						newSelectedGameinfoAvailable=true;
						selectedGameinfoValid=true;
					}
					fprintf(logFile, "new game->mapName=%s\n", game->mapName);
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
				fprintf(logFile, "Message (%s) already received, (messageID=%d)\n", mit->text, messageID);
				break;
			}
		if (!already)
		{
			Message m;
			m.messageID=messageID;
			m.messageType=(YOGClientMessageType)data[0];
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
				if ((mit->messageID==messageID) && (strncmp(m.text, mit->text, m.textLength)==0))
				{
					already=true;
					fprintf(logFile, "Message (%s) already recently received, (messageID=%d)\n", mit->text, messageID);
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
		fprintf(logFile, "YMT_PRIVATE_RECEIPT packet receiptID=%d, messageID=%d, sizeAddrs=%d\n", receiptID, messageID, sizeAddrs);
		if (size-8>(2+64)*sizeAddrs)
		{
			fprintf(logFile, "bad size for a YMT_PRIVATE_RECEIPT packet (size=%d), (ip=%s)\n", size, Utilities::stringIP(ip));
			break;
		}
		send(YMT_PRIVATE_RECEIPT, receiptID);
		
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
					int index=8;
					for (int i=0; i<sizeAddrs; i++)
					{
						int unl=data[index++];
						bool away=data[index++];
						Message m;
						m.messageID=messageID;
						m.timeout=0;
						m.TOTL=3;
						m.gameGuiPainted=false;
						
						int l=unl;
						if (l<32)
							l++;
						memcpy(m.userName, mit->text+3, l);
						m.userName[l-1]=0;
						m.userNameLength=l;
						
						if (away)
						{
							m.messageType=YCMT_PRIVATE_RECEIPT_BUT_AWAY;
							
							char *text=(char *)data+index;
							int l=Utilities::strmlen(text, 64);
							memcpy(m.text, text, l);
							if (m.text[l-1]!=0)
								fprintf(logFile, "warning, non-zero ending away message!\n");
							m.text[63]=0;
							m.textLength=l;
							index+=l;
						}
						else
						{
							m.messageType=YCMT_PRIVATE_RECEIPT;
							
							char *text=mit->text+unl+4;
							int l=Utilities::strmlen(text, 256);
							memcpy(m.text, text, l);
							if (m.text[l-1]!=0)
								fprintf(logFile, "warning, non-zero ending text message!\n");
							m.text[255]=0;
							m.textLength=l;
						}

						fprintf(logFile, "new YMT_PRIVATE_RECEIPT (%d) message:%s:%s\n", away, m.userName, m.text);
						receivedMessages.push_back(m);
					}
					assert(index==size);
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
				fprintf(logFile, "Warning, message (%d) confirmed, but message (%d) is being sent!\n", messageID, mit->messageID);
		}
	}
	break;
	case YMT_CONNECTING:
	{
		if (size==36)
		{
			yogGlobalState=YGS_AUTHENTICATING;
			fprintf(logFile, "connected (uid=%d)\n", uid);
			memcpy(xorpassw, data+4, 32);
			connectionTimeout=0;
			connectionTOTL=3;
		}
		else
			fprintf(logFile, "bad YOG-connected packet\n");
	}
	break;
	case YMT_AUTHENTICATING:
	{
		if (size==8)
		{
			yogGlobalState=YGS_CONNECTED;
			uid=getUint32(data, 4);
			fprintf(logFile, "authenticated (uid=%d)\n", uid);
		}
		else
			fprintf(logFile, "bad YOG-connected packet\n");
	}
	break;
	case YMT_CONNECTION_REFUSED:
	{
		if (yogGlobalState==YGS_CONNECTING || yogGlobalState==YGS_AUTHENTICATING)
		{
			fprintf(logFile, "connection refused! (param=%d, YOG_PROTOCOL_VERSION=%d, PROTOCOL_VERSION=%d)\n",
				data[4], data[5], YOG_PROTOCOL_VERSION);
			
			if (data[5]!=YOG_PROTOCOL_VERSION)
				externalStatusState=YESTS_CONNECTION_REFUSED_PROTOCOL_TOO_OLD;
			else if (data[4]==YCRT_PROTOCOL_TOO_OLD)
				externalStatusState=YESTS_CONNECTION_REFUSED_PROTOCOL_TOO_OLD;
			else if (data[4]==YCRT_USERNAME_ALLREADY_USED)
				externalStatusState=YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED;
			else if (data[4]==YCRT_BAD_PASSWORD)
				externalStatusState=YESTS_CONNECTION_REFUSED_BAD_PASSWORD;
			else if (data[4]==YCRT_BAD_PASSWORD_NON_ZERO)
				externalStatusState=YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO;
			else if (data[4]==YCRT_ALREADY_PASSWORD)
				externalStatusState=YESTS_CONNECTION_REFUSED_ALREADY_PASSWORD;
			else if (data[4]==YCRT_NOT_CONNECTED_YET)
				externalStatusState=YESTS_CONNECTION_REFUSED_NOT_CONNECTED_YET;
			else if (data[4]==YCRT_ALREADY_AUTHENTICATED)
				externalStatusState=YESTS_CONNECTION_REFUSED_ALREADY_AUTHENTICATED;
			else
				externalStatusState=YESTS_CONNECTION_REFUSED_UNEXPLAINED;
			
			yogGlobalState=YGS_NOT_CONNECTING;
			globalContainer->popUserName();
		}
		else
			fprintf(logFile, "Warning, ignored connection refused packet. (param=%d, YOG_PROTOCOL_VERSION=%d, PROTOCOL_VERSION=%d)\n",
				data[4], data[5], YOG_PROTOCOL_VERSION);
	}
	break;
	case YMT_DECONNECTING:						//fix1
	{
		fprintf(logFile, "disconnected\n");
		yogGlobalState=YGS_NOT_CONNECTING;
		globalContainer->popUserName();
		externalStatusState=YESTS_DECONNECTED;	//fix1
	}
	break;
	case YMT_SHARING_GAME:
	{
		selectedGame=getUint32(data, 4);
		if (selectedGame)
			isSelectedGame=true;
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
		isSelectedGame=false;
	}
	break;
	case YMT_STOP_PLAYING_GAME:
	{
		fprintf(logFile, "game is unjoined\n");
		unjoining=false;
		unjoiningConfirmed=true;
		isSelectedGame=false;
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
			game.hostip.host=SDL_SwapLE32(getUint32(data, index));
			index+=4;
			game.hostip.port=SDL_SwapLE16(getUint16(data, index));
			index+=2;
			game.uid=getUint32(data, index);
			index+=4;
			
			Uint32 huid=getUint32(data, index);
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
			
			int l=Utilities::strmlen((char *)data+index, 64);
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
			game.netProtocolVersion=0;
			game.configCheckSum=0;
			memset(game.mapName, 0, 64);
			game.natSolved=false;
			games.push_back(game);
			fprintf(logFile, "index=%d.\n", index);
			fprintf(logFile, "game no=%d uid=%d name=%s host=%s ip=%s\n", i, game.uid, game.name, game.userName, Utilities::stringIP(game.hostip));
		}
		assert(index==size);
		if (isAnyCompleteNewGame)
			newGameListAvailable=true;
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
						newSelectedGameinfoAvailable=selectedGameinfoValid;
					}
					games.erase(game);
					break;
				}
		}
		newGameListAvailable=true;
		send(YMT_UNSHARED_LIST, nbUnshared);
	}
	break;
	case YMT_CONNECTION_PRESENCE:
	{
		presenceTOTL=3;
		presenceTimeout=LONG_NETWORK_TIMEOUT;
		if (connectionLost)
		{
			yogGlobalState=YGS_CONNECTING;
			globalContainer->pushUserName(this->userName);
			connectionTimeout=8;
			connectionTOTL=3;
		}
		connectionLost=false;
	}
	break;
	case YMT_HOST_GAME_SOCKET:
	{
		hostGameSocketTimeout=MAX_NETWORK_TIMEOUT;
		hostGameSocketTOTL=3;
		hostGameSocketReceived=true;
		fprintf(logFile, "hostGameSocketReceived\n");
	}
	break;
	case YMT_JOIN_GAME_SOCKET:
	{
		joinGameSocketTimeout=MAX_NETWORK_TIMEOUT;
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
		bool firstTime=(clients.size()==0);
		for (int i=0; i<nbClients; i++)
		{
			Client client;
			Uint32 cuid=getUint32(data, index);
			client.uid=cuid;
			index+=4;
			int l=Utilities::strmlen((char *)data+index, 32);
			memcpy(client.userName, data+index, l);
			client.userName[l-1]=0;
			index+=l;
			client.playing=(bool)data[index];
			index++;
			client.away=(bool)data[index];
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
				
				if (!firstTime)
				{
					Message message;
					message.gameGuiPainted=false;
					message.messageType=YCMT_EVENT_MESSAGE;
					FormatableString tmp(Toolkit::getStringTable()->getString("[The player %0 has joined YOG]"));
					tmp.arg(client.userName);
					strncpy(message.text, tmp.c_str(), 512);
					receivedMessages.push_back(message);
				}
				
				for (std::list<GameInfo>::iterator game=games.begin(); game!=games.end(); ++game)
					if (game->userName[0]==0 && game->huid==cuid)
					{
						strncpy(game->userName, client.userName, 32);
						newGameListAvailable=true;
						fprintf(logFile, "Game (%s) from (%s) newly available!\n", game->name, game->userName);
						break;
					}
			}
			fprintf(logFile, "client uid=%d name=%s\n", client.uid, client.userName);
		}
		newClientListAvailable=true;
		
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
					if (change & CUP_LEFT)
					{
						fprintf(logFile, "left client uid=%d name=%s\n", client->uid, client->userName);
						Message message;
						message.gameGuiPainted=false;
						message.messageType=YCMT_EVENT_MESSAGE;
						FormatableString tmp(Toolkit::getStringTable()->getString("[The player %0 has left YOG]"));
						tmp.arg(client->userName);
						strncpy(message.text, tmp.c_str(), 512);
						receivedMessages.push_back(message);
						clients.erase(client);
					}
					else
					{
						if (change & CUP_PLAYING)
						{
							fprintf(logFile, "client uid=%d name=%s playing\n", client->uid, client->userName);
							client->playing=true;
						}
						else if (change & CUP_NOT_PLAYING)
						{
							fprintf(logFile, "client uid=%d name=%s not playing\n", client->uid, client->userName);
							client->playing=false;
						}
						if (change & CUP_AWAY)
						{
							fprintf(logFile, "client uid=%d name=%s away\n", client->uid, client->userName);
							if (client->uid==this->uid && !client->away)
							{
								Message message;
								message.gameGuiPainted=false;
								message.messageType=YCMT_EVENT_MESSAGE;
								strncpy(message.text, Toolkit::getStringTable()->getString("[You are now marked as away]"), 512);
								receivedMessages.push_back(message);
							}
							client->away=true;
						}
						else if (change & CUP_NOT_AWAY)
						{
							fprintf(logFile, "client uid=%d name=%s not away\n", client->uid, client->userName);
							if (client->uid==this->uid && client->away)
							{
								Message message;
								message.gameGuiPainted=false;
								message.messageType=YCMT_EVENT_MESSAGE;
								strncpy(message.text, Toolkit::getStringTable()->getString("[You are no more marked as away]"), 512);
								receivedMessages.push_back(message);
							}
							client->away=false;
						}
					}
					break;
				}
			
		}
		newClientListAvailable=true;
		
		Uint8 data[2];
		addUint8(data, (Uint8)nbClients, 0);
		addUint8(data, clientsUpdatePacketID, 1);
		send(YMT_UPDATE_CLIENTS_LIST, data, 2);
	}
	break;
	case YMT_CLOSE_YOG:
	{
		fprintf(logFile, " YOG is dead (killed)!\n");
		externalStatusState=YESTS_YOG_KILLED;
		yogGlobalState=YGS_NOT_CONNECTING;
		globalContainer->popUserName();
		connectionLost=true;
	}
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
		VARARRAY(Uint8,sendData,sendSize);
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
			for (std::list<Joiner>::iterator ji=joiners.begin(); ji!=joiners.end(); ++ji)
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

/**
 * Opens up a port on the host computer and listens for connections.
 *
 * @param const char *username 
 * @param const char *passWord 
 * @param bool newYogPassword
 * @return boolean
 * @todo write other functions to allow profiles which includes email addresses
 *		 for lost passwords
 */
bool YOG::enableConnection(const char *userName, const char *passWord, bool newYogPassword)
{
	memset(this->userName, 0, 32);
	strncpy(this->userName, userName, 31);
	memset(this->passWord, 0, 32);
	strncpy(this->passWord, passWord, 31);
	this->newYogPassword=newYogPassword;

	if (socket)
		SDLNet_UDP_Close(socket);
	
	socket=SDLNet_UDP_Open(0);
	if (!socket)
	{
		fprintf(logFile, "failed to open a socket!\n");
		return false;
	}
	
	const char *yogHostname;
	if (globalContainer->yogHostName.length() > 0)
		yogHostname = globalContainer->yogHostName.c_str();
	else
		yogHostname = YOG_SERVER_IP;
	fprintf(logFile, "\nresolving YOG host name %s\n", yogHostname);
	int rv = SDLNet_ResolveHost(&serverIP, (char *)yogHostname, YOG_SERVER_PORT);
	if (rv==-1)
	{
		fprintf(logFile, "failed to resolve YOG host name %s!\n", yogHostname);
		return false;
	}
	
	yogGlobalState=YGS_CONNECTING;
	globalContainer->pushUserName(this->userName);
	externalStatusState=YESTS_CONNECTING;
	sendingMessages.clear();
	recentlySentMessages.clear();
	receivedMessages.clear();
	recentlyReceivedMessages.clear();
	lastMessageID=0;
	
	connectionTimeout=0+4;//4 instead of 0 to share brandwith with others timouts
	connectionTOTL=3;
	connectionLost=false;
	
	games.clear();
	newGameListAvailable=false;
	
	presenceTimeout=0+8;//8 instead of 0 to share brandwith with others timouts
	presenceTOTL=3;
	
	hostGameSocket=NULL;
	hostGameSocketReceived=false;
	joinGameSocket=NULL;
	joinGameSocketReceived=false;
	joiners.clear();
	
	clients.clear();
	newClientListAvailable=false;
	
	uid=0;
	
	fprintf(logFile, "enableConnection(), userName=%s\n", userName);
	
	return true;
}

/*
 * disconnects on closing of the metaserver, or so it seems
 * @todo check my description
 */
void YOG::deconnect() //fix1
{
	if (connectionLost)
	{
		yogGlobalState=YGS_NOT_CONNECTING;
		globalContainer->popUserName();
	}
	else if (yogGlobalState>=YGS_CONNECTED)
	{
		yogGlobalState=YGS_DECONNECTING;
		externalStatusState=YESTS_DECONNECTING;
		connectionTimeout=0;
		connectionTOTL=3;
	}
	else if (yogGlobalState>=YGS_CONNECTING)
	{
		yogGlobalState=YGS_DECONNECTING;
		externalStatusState=YESTS_DECONNECTING;
		connectionTimeout=0;
		connectionTOTL=1;
	}
	
	if (yogSharingState>=YSS_SHARING_GAME)
	{
		yogSharingState=YSS_STOP_SHARING_GAME;
		sharingGameTimeout=0;
		sharingGameTOTL=3;
	}
	fprintf(logFile, "disconnect() yogGlobalState=%d\n", yogGlobalState);
}

/**
 * @todo document
 */
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
					globalContainer->popUserName();
					connectionLost=true;
					externalStatusState=YESTS_DECONNECTED;
					fprintf(logFile, "unable to disconnect!\n");
				}
				else
				{
					fprintf(logFile, "sending disconnection request...\n");
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
					globalContainer->popUserName();
					externalStatusState=YESTS_UNABLE_TO_CONNECT;
					fprintf(logFile, "unable to connect!\n");
				}
				else
				{
					fprintf(logFile, "sending connection request...\n");
					int unl=Utilities::strmlen(userName, 32);
					UDPpacket *packet=SDLNet_AllocPacket(8+unl);
					assert(packet);
					packet->data[0]=YMT_CONNECTING;
					packet->data[1]=0;
					packet->data[2]=0;
					packet->data[3]=0;
					addUint32(packet->data, YOG_PROTOCOL_VERSION, 4);
					memcpy(packet->data+8, (Uint8 *)userName, unl);
					packet->len=8+unl;
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
		case YGS_AUTHENTICATING:
		{
			if (connectionTimeout--<=0)
				if (connectionTOTL--<=0)
				{
					yogGlobalState=YGS_UNABLE_TO_CONNECT;
					globalContainer->popUserName();
					externalStatusState=YESTS_UNABLE_TO_CONNECT;
					fprintf(logFile, "unable to connect!\n");
				}
				else
				{
					if (verbose)
						printf("sending authentication info...userName=(%s), newYogPassword=%d\n", userName, newYogPassword);
					UDPpacket *packet;
					unsigned char xored[32];
					for (int i=0; i<32; i++)
						xored[i]=passWord[i]^xorpassw[i];
					//printf(" passWord=[%2x %2x %2x %2x]\n", passWord[0], passWord[1], passWord[2], passWord[3]);
					//printf(" xorpassw=[%2x %2x %2x %2x]\n", xorpassw[0], xorpassw[1], xorpassw[2], xorpassw[3]);
					//printf(" xored   =[%2x %2x %2x %2x]\n", xored[0], xored[1], xored[2], xored[3]);
					if (newYogPassword)
					{
						packet=SDLNet_AllocPacket(36);
						assert(packet);
						packet->data[0]=YMT_AUTHENTICATING;
						packet->data[1]=0;
						packet->data[2]=0;
						packet->data[3]=0;
						memcpy(packet->data+4, (Uint8 *)xored, 32);
						packet->len=36;
					}
					else
					{
						packet=SDLNet_AllocPacket(24);
						assert(packet);
						packet->data[0]=YMT_AUTHENTICATING;
						packet->data[1]=2;
						packet->data[2]=0;
						packet->data[3]=0;
						
						unsigned char computedDigest[20];
						SHA1_CTX context;
						SHA1Init(&context);
						SHA1Update(&context, xored, 32);
						SHA1Final(computedDigest, &context);
						
						//printf("passWord=[%2x], xorpassw=[%2x], xored=[%2x], computedDigest=[%2x]\n",
						//	passWord[0], xorpassw[0], xored[0], computedDigest[0]);
						
						memcpy(packet->data+4, (Uint8 *)computedDigest, 20);
						packet->len=24;
					}
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
				fprintf(logFile, "Connection lost to YOG!\n");
				externalStatusState=YESTS_CONNECTION_LOST;
				connectionLost=true;
			}
			else
			{
				fprintf(logFile, "Sending YMT_CONNECTION_PRESENCE.\n");
				send(YMT_CONNECTION_PRESENCE);
			}
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
				hostGameSocketTimeout=MAX_NETWORK_TIMEOUT;
			else
				hostGameSocketTimeout=DEFAULT_NETWORK_TIMEOUT;
				
			if (isSelectedGame)
				fprintf(logFile, "Sending the hostGameSocket to YOG (selectedGame=%d)...\n", selectedGame);
			else
				fprintf(logFile, "Sending the hostGameSocket to YOG ...\n");
			UDPpacket *packet=SDLNet_AllocPacket(8);
			assert(packet);
			packet->len=8;
			Uint8 data[8];
			data[0]=YMT_HOST_GAME_SOCKET;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addUint32(data, uid, 4);
			memcpy(packet->data, data, 8);
			packet->address=serverIP;
			packet->channel=-1;
			bool success=SDLNet_UDP_Send(hostGameSocket, -1, packet)==1;
			if (!success)
				fprintf(logFile, "failed to send the hostGameSocket to YOG!\n");
			SDLNet_FreePacket(packet);
		}
		
		if (joinGameSocket && !joinGameSocketReceived && joinGameSocketTimeout--<=0 && isSelectedGame && uid)
		{
			if (joinGameSocketTOTL--<=0)
				fprintf(logFile, "Unable to deliver the joinGameSocket to YOG!\n"); // TODO!
			if (joinGameSocketReceived || joinGameSocketTOTL<=0)
				joinGameSocketTimeout=MAX_NETWORK_TIMEOUT;
			else
				joinGameSocketTimeout=DEFAULT_NETWORK_TIMEOUT;
			fprintf(logFile, "Sending the joinGameSocket to YOG (selectedGame=%d)...\n", selectedGame);
			UDPpacket *packet=SDLNet_AllocPacket(12);
			assert(packet);
			packet->len=12;
			Uint8 data[12];
			data[0]=YMT_JOIN_GAME_SOCKET;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addUint32(data, uid, 4);
			addUint32(data, selectedGame, 8);
			memcpy(packet->data, data, 12);
			packet->address=serverIP;
			packet->channel=-1;
			bool success=SDLNet_UDP_Send(joinGameSocket, -1, packet)==1;
			if (!success)
				fprintf(logFile, "failed to send the joinGameSocket to YOG!\n");
			SDLNet_FreePacket(packet);
		}
		
		if (isSelectedGame && !newSelectedGameinfoAvailable && selectedGameinfoTimeout--<0 && selectedGameinfoTOTL-->0)
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

/**
 * sends the info for a LAN game by the looks of it
 * @todo am I correct?
 */
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
			Uint8 data[8];
			data[0]=YOG_CLIENT_REQUESTS_GAME_INFO;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			addSint32(data, game->uid, 4);
			memcpy(packet->data, data, 8);
			packet->address=game->hostip;
			packet->channel=-1;
			bool success=SDLNet_UDP_Send(socket, -1, packet)==1;
			if (!success)
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
				
				//TODO: send broadcasting for any game, not only for selected games. broadcastTimeout=DEFAULT_NETWORK_TIMEOUT;
				if (SDLNet_UDP_Send(socket, -1, packet)==1)
					fprintf(logFile, "Successed to send a BROADCAST_REQUEST packet\n");
				else
				{
					enableLan=false;
					fprintf(logFile, "failed to send a BROADCAST_REQUEST packet\n");
				}
				
				SDLNet_FreePacket(packet);
			}
			else
				fprintf(logFile, "No broadcasting now, game->natSolved=%d, enableLan=%d.\n", game->natSolved, enableLan);
			break;
		}
	
	
}

/**
 * well, it shares the game, duh!
 *
 * @param const char *gameName
 */
void YOG::shareGame(const char *gameName)
{
	//wtf is YSS?
	yogSharingState=YSS_SHARING_GAME;
	strncpy(sharingGameName, gameName, 64);
	sharingGameName[63]=0;
	sharingGameTimeout=0;
	sharingGameTOTL=3;
	fprintf(logFile, "shareGame(%s)\n", gameName);
	unjoiningConfirmed=false;
}

/**
 * stops sharing the game with others
 * @todo check usage, don't see unshareGame in the logs anywhere
 */
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
	unjoiningConfirmed=false;
	fprintf(logFile, "unshareGame()\n");
}

/**
 * @todo is this function ever used? I don't see joinGame() anywhere in the logs?
 */
void YOG::joinGame()
{
	fprintf(logFile, "joinGame() was=%d\n", joinedGame);
	assert(!unjoining);
	joinedGame=true;
	unjoiningConfirmed=false;
}

/**
 * @param bool strict 
 * @param const char *reason
 * @todo proper english should be leaveGame, rename everything
 */
void YOG::unjoinGame(bool strict, const char *reason)
{
	fprintf(logFile, "unjoinGame(%d) yogGlobalState=%d\n", strict, yogGlobalState);
	assert(yogGlobalState == YGS_CONNECTED);
	if (strict)
		assert(joinedGame);
	
	if (reason)
		addEventMessage(reason);
	joinedGame=false;
	
	joinGameSocketReceived=false;
	
	unjoinTimeout=0;
	unjoining=true;
	unjoiningConfirmed=false;
}

void YOG::sendMessage(const char *message)
{
	if (yogGlobalState>=YGS_CONNECTED)
		if (!handleLocalMessageTreatment(message))
		{
			lastMessageID++;
			Message m;
			m.messageID=lastMessageID;
			strncpy(m.text, message, 256);
			m.text[255]=0;
			handleMessageAliasing(m.text, 256);
			m.timeout=0;
			m.TOTL=3;
			m.textLength=Utilities::strmlen(m.text, 256);
			m.userName[0]=0;
			m.userNameLength=0;
			sendingMessages.push_back(m);
		}
}

bool YOG::newGameList(bool reset)
{
	if (newGameListAvailable)
	{
		if (reset)
			newGameListAvailable=false;
		return true;
	}
	else
		return false;
}

bool YOG::newPlayerList(bool reset)
{
	if (newClientListAvailable)
	{
		if (reset)
			newClientListAvailable=false;
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
			newSelectedGameinfoAvailable=false;
			selectedGameinfoValid=false;
			selectedGameinfoTimeout=2;
			selectedGameinfoTOTL=3;
			return true;
		}
	
	selectedGame=uid;
	isSelectedGame=false;
	newSelectedGameinfoAvailable=false;
	selectedGameinfoValid=false;
	return false;
}

bool YOG::selectedGameinfoUpdated(bool reset)
{
	bool rv=newSelectedGameinfoAvailable;
	if (reset)
		newSelectedGameinfoAvailable=false;
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
	unjoiningConfirmed=false;
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
	unjoiningConfirmed=false;
	joinGameSocketReceived=false;
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

char *YOG::userNameFromUID(Uint32 uid)
{
	for (std::list<Client>::iterator client=clients.begin(); client!=clients.end(); ++client)
		if (uid==client->uid)
			return client->userName;
	return NULL;
}

char *YOG::getStatusString(ExternalStatusState externalStatusState)
{
	const char *s="internal error";
	switch (externalStatusState)
	{
	case YESTS_BAD:
		s=Toolkit::getStringTable()->getString("[YESTS_BAD]");
	break;
	case YESTS_UNABLE_TO_CONNECT:
		s=Toolkit::getStringTable()->getString("[YESTS_UNABLE_TO_CONNECT]");
	break;
	case YESTS_DECONNECTED:
	case YESTS_CREATED:
		s=Toolkit::getStringTable()->getString("[YESTS_CREATED]");
	break;
	case YESTS_DECONNECTING:
		s=Toolkit::getStringTable()->getString("[YESTS_DECONNECTING]");
	break;
	case YESTS_CONNECTING:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTING]");
	break;
	case YESTS_YOG_KILLED:
		s=Toolkit::getStringTable()->getString("[YESTS_YOG_KILLED]");
	break;
	case YESTS_CONNECTION_LOST:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_LOST]");
	break;
	case YESTS_CONNECTION_REFUSED_PROTOCOL_TOO_OLD:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_PROTOCOL_TOO_OLD]");
	break;
	case YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED]");
	break;
	case YESTS_CONNECTION_REFUSED_BAD_PASSWORD:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_BAD_PASSWORD]");
	break;
	case YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO]");
	break;
	case YESTS_CONNECTION_REFUSED_ALREADY_PASSWORD:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_ALREADY_PASSWORD]");
	break;
	case YESTS_CONNECTION_REFUSED_ALREADY_AUTHENTICATED:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_ALREADY_AUTHENTICATED]");
	break;
	case YESTS_CONNECTION_REFUSED_NOT_CONNECTED_YET:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_NOT_CONNECTED_YET]");
	break;
	case YESTS_CONNECTION_REFUSED_UNEXPLAINED:
		s=Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_UNEXPLAINED]");
	break;
	default:
		printf("externalStatusState=%d\n", externalStatusState);
		assert(false);
	break;
	}
	size_t l=strlen(s)+1;
	char *t=new char[l];
	strncpy(t, s, l);
	return t;
}

char *YOG::getStatusString()
{
	return getStatusString(externalStatusState);
}

void YOG::handleMessageAliasing(char *message, int maxSize)
{
	if (strncmp("/msg ", message, 5)==0)
		memmove(message+2, message+4, Utilities::strmlen(message+4, maxSize-4));
	else if (strncmp("/whisper ", message, 9)==0)
		memmove(message+2, message+8, Utilities::strmlen(message+8, maxSize-8));
	else if (strncmp("/away ", message, 6)==0)
		memmove(message+2, message+5, Utilities::strmlen(message+5, maxSize-5));
}

/**
 * @param const char *message
 * @return boolean
 */
bool YOG::handleLocalMessageTreatment(const char *message)
{
	const char *s=NULL;
	if (strncmp("/help", message, 6)==0 || strncmp("/h", message, 3)==0)
		s=Toolkit::getStringTable()->getString("[YOG_HELP]");
	else if (strncmp("/help away", message, 11)==0 || strncmp("/h a", message, 5)==0)
		s=Toolkit::getStringTable()->getString("[YOG_HELP_AWAY]");
	else if (strncmp("/help msg", message, 10)==0 || strncmp("/h m", message, 5)==0)
		s=Toolkit::getStringTable()->getString("[YOG_HELP_MSG]");
	if (s)
	{
		Message m;
		m.messageID=0;
		m.messageType=YCMT_EVENT_MESSAGE;
		m.timeout=0;
		m.TOTL=3;
		m.gameGuiPainted=false;
		int l=Utilities::strmlen(s, 512);
		memcpy(m.text, s, l);
		m.text[511]=0;
		m.textLength=l;
		m.userName[0]=0;
		m.userNameLength=1;
		receivedMessages.push_back(m);
		return true;
	}
	else
		return false;
}

/**
 * @todo Add an event message with msg text to the event queue
 */
void YOG::addEventMessage(const char *msg)
{
	Message message;
	message.gameGuiPainted = false;
	message.messageType = YCMT_EVENT_MESSAGE;
	strncpy(message.text, msg, sizeof(message.text));
	message.text[sizeof(message.text) - 1] = 0;
	receivedMessages.push_back(message);
}
