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

#ifndef __YOG_H
#define __YOG_H

#include "Header.h"
#include "YOGConsts.h"
#include <list>

class YOG
{
public:
	enum {MAX_MESSAGE_SIZE=256};
	
	enum YOGGlobalState
	{
		YGS_BAD=0,
		YGS_UNABLE_TO_CONNECT=1,
		YGS_NOT_CONNECTING=2,
		YGS_DECONNECTING=3,
		YGS_CONNECTING=4,
		YGS_CONNECTED=5,
		YGS_PLAYING=6
	};
	
	enum YOGSharingState
	{
		YSS_BAD=0,
		YSS_NOT_SHARING_GAME=1,
		YSS_STOP_SHARING_GAME=2,
		YSS_SHARING_GAME=3,
		YSS_SHARED_GAME=4,
		YSS_HAS_GAME_SOCKET=5
	};
	
	struct GameInfo
	{
		IPaddress ip;
		char userName[32];
		char name[128];
		Uint32 uid;
	};
	
	struct Message
	{
		char text[256];
		int textLength;
		int timeout;
		int TOTL;
		bool received;
		char userName[32];
		int userNameLength;
		YOGMessageType messageType;
		Uint8 messageID;
		Uint8 pad1;
		Uint8 pad2;
		Uint8 pad3;//We need to pad to support memory alignement
	};
	
	struct Client
	{
		char userName[32];
		Uint32 uid;
	};
	
public:
	YOG();
	virtual ~YOG();
	void send(Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 id, Uint8 *data, int size);
	void send(YOGMessageType v);
	void send(YOGMessageType v, UDPsocket socket);
	void send(YOGMessageType v, Uint8 id);
	
	void treatPacket(IPaddress ip, Uint8 *data, int size);
	
	bool enableConnection(const char *userName);
	void step();
	
	void shareGame(const char *gameName);
	void unshareGame();
	
	void joinGame();
	void unjoinGame();
	bool joinedGame;
	
	void sendMessage(const char *message);
	
	bool newGameList(bool reset);
	
	bool newPlayerList(bool reset);
	
	void gameStarted();
	void gameEnded();
	
	void setGameSocket(UDPsocket socket);
	bool gameSocketSet();
	
	YOGGlobalState yogGlobalState;
	int connectionTimeout;
	int connectionTOTL;
	
	UDPsocket socket;
	UDPsocket gameSocket;
	IPaddress serverIP;
	Uint8 lastMessageID;
	
	void deconnect();
	char userName[32];
	
	std::list<Message> sendingMessages;
	std::list<Message> receivedMessages;
	
	YOGSharingState yogSharingState;
	char sharingGameName[128];
	int sharingGameTimeout;
	int sharingGameTOTL;
	
	std::list<GameInfo> games;
	bool newGameListAviable;
	
	int presenceTimeout;
	int presenceTOTL;
	
	int gameSocketTimeout;
	int gameSocketTOTL;
	bool gameSocketReceived;
	
	std::list<Client> clients;
	bool newClientListAviable;
	
public:
	FILE *logFile;
};

#endif
