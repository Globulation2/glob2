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

class LogFileManager;

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
		IPaddress hostip;
		char userName[32];
		char name[128];
		Uint32 uid;
		int numberOfPlayer;
		int numberOfTeam;
		bool fileIsAMap;
		int mapGenerationMethode;
		char mapName[128];
		bool natSolved;
		Uint32 huid; //the uid of the game host
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
		Uint8 playing;
	};
	
	struct Joiner
	{
		int timeout;
		int TOTL;
		IPaddress ip;
		Uint32 uid;
		bool connected;
	};
	
public:
	YOG(LogFileManager *logFileManager);
	virtual ~YOG();
	void send(Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 id, Uint8 *data, int size);
	void send(YOGMessageType v);
	void send(YOGMessageType v, UDPsocket socket);
	void send(YOGMessageType v, Uint8 id);
	void send(UDPsocket socket, IPaddress ip, Uint8 v);
	
	void treatPacket(IPaddress ip, Uint8 *data, int size);
	
	bool enableConnection(const char *userName);
	void step();
	
	void sendGameinfoRequest();
	
	void shareGame(const char *gameName);
	void unshareGame();
	
	void joinGame();
	void unjoinGame();
	bool joinedGame;
	
	void sendMessage(const char *message);
	
	bool newGameList(bool reset);
	bool newPlayerList(bool reset);
	
	bool selectGame(Uint32 uid);
	bool selectedGameinfoUpdated(bool reset);
	GameInfo *getSelectedGameInfo();
	
	void gameStarted();
	void gameEnded();
	
	void setHostGameSocket(UDPsocket socket);
	bool hostGameSocketSet();
	void setJoinGameSocket(UDPsocket socket);
	bool joinGameSocketSet();
	
	void joinerConnected(IPaddress ip); // Call this if you host a game, and someone has joined your game.
	void connectedToGameHost(); // Call this if you are trying to join a game, and the host responded.
	
	IPaddress ipFromUserName(char userName[32]);
	
	bool isConnectedToGameHost;
	
	YOGGlobalState yogGlobalState;
	int connectionTimeout;
	int connectionTOTL;
	
	UDPsocket socket;
	UDPsocket hostGameSocket;
	UDPsocket joinGameSocket;
	IPaddress serverIP;
	Uint8 lastMessageID;
	
	void deconnect();
	char userName[32];
	Uint32 uid;
	
	std::list<Message> sendingMessages;
	std::list<Message> receivedMessages;
	
	YOGSharingState yogSharingState;
	char sharingGameName[128];
	int sharingGameTimeout;
	int sharingGameTOTL;
	
	std::list<GameInfo> games;
	bool newGameListAviable;
	
	Uint32 selectedGame;
	bool isSelectedGame;
	bool selectedGameinfoValid;
	bool newSelectedGameinfoAviable;
	int selectedGameinfoTimeout;
	int selectedGameinfoTOTL;
	
	int presenceTimeout;
	int presenceTOTL;
	
	int hostGameSocketTimeout;
	int hostGameSocketTOTL;
	bool hostGameSocketReceived;
	int joinGameSocketTimeout;
	int joinGameSocketTOTL;
	bool joinGameSocketReceived;
	
	std::list<Client> clients;
	bool newClientListAviable;
	
	std::list<Joiner> joiners;
	
	bool unjoining;
	int unjoinTimeout;
	
	bool connectionLost;
private:
	bool enableLan;
	
public:
	FILE *logFile;
};

#endif
