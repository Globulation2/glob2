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

#ifndef __MULTIPLAYERJOIN_H
#define __MULTIPLAYERJOIN_H

#include "MultiplayersCrossConnectable.h"
#include "LANBroadcast.h"
#include "YOG.h"
#include <list>

class MultiplayersJoin:public MultiplayersCrossConnectable
{
public:
	enum WaitingState
	{
		WS_BAD=0,
		WS_TYPING_SERVER_NAME,
		WS_WAITING_FOR_PRESENCE,
		WS_WAITING_FOR_SESSION_INFO,
		WS_WAITING_FOR_CHECKSUM_CONFIRMATION,
		WS_OK,

		WS_CROSS_CONNECTING,
		WS_CROSS_CONNECTING_START_CONFIRMED,
		WS_CROSS_CONNECTING_ACHIEVED,

		WS_CROSS_CONNECTING_SERVER_HEARD,

		WS_SERVER_START_GAME
	};

	SDL_RWops *downloadStream;
	int startDownloadTimeout;
	
	enum{NET_WINDOW_SIZE=1024};
	struct NetWindowSlot
	{
		//NetWindowState state;
		Uint32 index;
		bool received;
		int packetSize;
	};
	NetWindowSlot netWindow[NET_WINDOW_SIZE];
	Uint32 unreceivedIndex;
	Uint32 endOfFileIndex;
	int duplicatePacketFile;
	
private:
	LANBroadcast lan;
	enum BroadcastState
	{
		BS_BAD=0,
		
		BS_ENABLE_LAN, //In this state we are looking for local shared games
		BS_JOINED_LAN,
		
		BS_ENABLE_YOG, //In this statem we are looking for games shared on yog, but maybe hidden by NAT.
		BS_JOINED_YOG,
		BS_DISABLE_YOG
	};
	BroadcastState broadcastState;
	int broadcastTimeout;
	
public:
	bool ipFromNAT; //This means: did I found the game host server's ip from a broadcasting ?
	
public:
	bool listHasChanged;
	struct LANHost
	{
		Uint32 ip; // This is in network endianess
		char gameName[32];
		char serverNickName[32];
		int timeout;
	};
	
	std::list<LANHost> LANHosts;
	//char gameName[32];
	
private:
	bool shareOnYOG;
	const YOG::GameInfo *yogGameInfo;

public:
	char serverNameMemory[128];
	char *serverName;
	char playerName[32];
	char serverNickName[32];

	WaitingState waitingState;
	int waitingTimeout;
	int waitingTimeoutSize;
	int waitingTOTL;
	
	bool kicked;

	Uint16 localPort;
public:
	MultiplayersJoin(bool shareOnYOG);
	virtual ~MultiplayersJoin();
private:
	void init(bool shareOnYOG);

public:
	void dataPresenceRecieved(char *data, int size, IPaddress ip);
	void dataSessionInfoRecieved(char *data, int size, IPaddress ip);
	void dataFileRecieved(char *data, int size, IPaddress ip);
	void checkSumConfirmationRecieved(char *data, int size, IPaddress ip);
	

	void unCrossConnectSessionInfo(void);
	void tryCrossConnections(void);
	void startCrossConnections(void);
	void crossConnectionFirstMessage(char *data, int size, IPaddress ip);
	void checkAllCrossConnected(void);
	void crossConnectionSecondMessage(char *data, int size, IPaddress ip);
	void stillCrossConnectingConfirmation(IPaddress ip);
	void crossConnectionsAchievedConfirmation(IPaddress ip);

	void serverAskForBeginning(char *data, int size, IPaddress ip);
	void treatData(char *data, int size, IPaddress ip);

	void receiveTime();
	void onTimer(Uint32 tick);
	char *getStatusString();

	void sendingTime();
	bool sendPresenceRequest();
	bool sendSessionInfoRequest();
	bool sendSessionInfoConfirmation();
	bool send(char *data, int size);
	bool send(const int v);
	bool send(const int u, const int v);
	
	bool tryConnection();
	bool tryConnection(YOG::GameInfo *yogGameInfo);
	
	void quitThisGame();
	
	Uint16 findLocalPort(UDPsocket socket);
	bool isFileMapDownload(double &progress);

private:
	FILE *logFile;
	FILE *logFileDownload;
};

#endif
