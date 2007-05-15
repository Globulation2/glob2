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

#ifndef __YOG_H
#define __YOG_H

#include "Header.h"
#include "YOGConsts.h"
#include <list>
#include <string>

class LogFileManager;

//! Ysagoon Online Game connector and session handler
class YOG
{
	static const bool verbose = false;
public:
	enum YOGGlobalState
	{
		YGS_BAD=0,
		YGS_UNABLE_TO_CONNECT=1,
		YGS_NOT_CONNECTING=2,
		YGS_DECONNECTING=3,
		YGS_CONNECTING=4,
		YGS_AUTHENTICATING=5,
		YGS_CONNECTED=6,
		YGS_PLAYING=7
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
	
	enum ExternalStatusState
	{
		YESTS_BAD=0,
		YESTS_UNABLE_TO_CONNECT=10,
		YESTS_DECONNECTED=20,
		YESTS_CREATED=30,
		YESTS_CONNECTING=40,
		YESTS_DECONNECTING=50,
		YESTS_YOG_KILLED=60,
		YESTS_CONNECTION_LOST=70,
		YESTS_CONNECTION_REFUSED_PROTOCOL_TOO_OLD=80,
		YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED=90,
		YESTS_CONNECTION_REFUSED_BAD_PASSWORD=100,
		YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO=104,
		YESTS_CONNECTION_REFUSED_ALREADY_PASSWORD=110,
		YESTS_CONNECTION_REFUSED_ALREADY_AUTHENTICATED=120,
		YESTS_CONNECTION_REFUSED_NOT_CONNECTED_YET=130,
		YESTS_CONNECTION_REFUSED_UNEXPLAINED=140
	};
	
	struct GameInfo
	{
		IPaddress hostip;
		char userName[32];
		char name[64];
		Uint32 uid;
		int numberOfPlayer;
		int numberOfTeam;
		bool fileIsAMap;
		int mapGenerationMethode;
		int netProtocolVersion;
		Uint32 configCheckSum;
		char mapName[64];
		bool natSolved;
		Uint32 huid; //the uid of the game host
	};
	struct Message
	{
		char text[512];
		int textLength;
		int timeout;
		int TOTL;
		bool received;
		char userName[32];
		int userNameLength;
		YOGClientMessageType messageType;
		bool gameGuiPainted;
		Uint8 messageID;
	};
	
	struct Client
	{
		char userName[32];
		Uint32 uid;
		bool playing;
		bool away;
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
	YOG();
	virtual ~YOG();
	void send(Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 id, Uint8 *data, int size);
	void send(YOGMessageType v);
	void send(YOGMessageType v, UDPsocket socket);
	void send(YOGMessageType v, Uint8 id);
	void send(UDPsocket socket, IPaddress ip, Uint8 v);
	
	void treatPacket(IPaddress ip, Uint8 *data, int size);
	
	bool enableConnection(const char *userName, const char *passWord, bool newYogPassword);
	void step();
	
	void sendGameinfoRequest();
	
	void shareGame(const char *gameName);
	void unshareGame();
	
	void joinGame();
	void unjoinGame(bool strict = true, const char *reason = NULL);
	void joinGameRefused();
	bool joinedGame;
	
	void sendMessage(const char *message);
	// migration, const char * have to die at some point
	void sendMessage(const std::string &message) { sendMessage(message.c_str()); }
	
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
	char *userNameFromUID(Uint32 uid);
	
	void addEventMessage(const char *msg);
	
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
	char passWord[32];
	unsigned char xorpassw[32];
	bool newYogPassword;
	Uint32 uid;
	
	std::list<Message> sendingMessages;
	std::list<Message> recentlySentMessages;
	std::list<Message> receivedMessages;
	std::list<Message> recentlyReceivedMessages;
	
	YOGSharingState yogSharingState;
	char sharingGameName[64];
	int sharingGameTimeout;
	int sharingGameTOTL;
	
	std::list<GameInfo> games;
	bool newGameListAvailable;
	
	Uint32 selectedGame;
	bool isSelectedGame;
	bool selectedGameinfoValid;
	bool newSelectedGameinfoAvailable;
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
	bool newClientListAvailable;
	
	std::list<Joiner> joiners;
	
	bool unjoining;
	bool unjoiningConfirmed;
	int unjoinTimeout;
	
	bool connectionLost;
private:
	bool enableLan;
	
public:
	FILE *logFile;
	
public:
	ExternalStatusState externalStatusState;
	char *getStatusString(ExternalStatusState externalStatusState);
	char *getStatusString();
	
	// This methode modiffy the "message" to replace "/msg " by "/m ", because YOG only understand "/m".
	// This also allow GameGUI to make simpler tests.
	void handleMessageAliasing(char *message, int maxSize);
	
	// Handle the "/help" message.
	bool handleLocalMessageTreatment(const char *message);
};

extern YOG *yog;

#endif
