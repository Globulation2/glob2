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

#ifndef __MULTIPLAYERJOIN_H
#define __MULTIPLAYERJOIN_H

#include "MultiplayersCrossConnectable.h"
#include "YOG.h"
#include <list>

class MultiplayersJoin:public MultiplayersCrossConnectable
{
	static const bool verbose = false;
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

	std::string filename;
	SDL_RWops *downloadStream;
	
	enum{PACKET_SLOTS=1024};
	struct PacketSlot
	{
		Uint32 index;
		bool received;
		int brandwidth;
	};
	PacketSlot packetSlot[PACKET_SLOTS];
	Uint32 unreceivedIndex;
	Uint32 endOfFileIndex;
	int totalReceived;
	int duplicatePacketFile;
	int startDownloadTimeout;
	int brandwidth;
	int receivedCounter;
	
private:
	enum BroadcastState
	{
		BS_BAD=0,
		
		BS_ENABLE_LAN=1, //In this state we are looking for local shared games
		BS_JOINED_LAN=2,
		
		BS_ENABLE_YOG=3, //In this statem we are looking for games shared on yog, but maybe hidden by NAT.
		BS_JOINED_YOG=4,
		BS_DISABLE_YOG=5
	};
	BroadcastState broadcastState;
	int broadcastTimeout;
	
public:
	bool ipFromNAT; //This means: did I found the game host server's ip from a broadcasting
	
public:
	bool listHasChanged;
	struct LANHost
	{
		IPaddress ip;
		char gameName[64];
		char serverNickName[32];
		int timeout;
	};
	
	std::list<LANHost> lanHosts;
	
private:
	bool shareOnYog;
	const YOG::GameInfo *yogGameInfo;

public:
	char serverName[256];

	char playerName[32];

	WaitingState waitingState;
	int waitingTimeout;
	int waitingTimeoutSize;
	int waitingTOTL;
	
	bool kicked;

	Uint16 localPort;
public:
	MultiplayersJoin(bool shareOnYog);
	virtual ~MultiplayersJoin();
private:
	void init(bool shareOnYog);
	void closeDownload();

public:
	void dataPresenceRecieved(Uint8 *data, int size, IPaddress ip);
	void dataSessionInfoRecieved(Uint8 *data, int size, IPaddress ip);
	void dataFileRecieved(Uint8 *data, int size, IPaddress ip);
	void checkSumConfirmationRecieved(Uint8 *data, int size, IPaddress ip);
	

	void unCrossConnectSessionInfo();
	void tryCrossConnections();
	void startCrossConnections();
	void crossConnectionFirstMessage(Uint8 *data, int size, IPaddress ip);
	void checkAllCrossConnected();
	void crossConnectionSecondMessage(Uint8 *data, int size, IPaddress ip);
	void stillCrossConnectingConfirmation(Uint8 *data, int size, IPaddress ip);
	void crossConnectionsAchievedConfirmation(IPaddress ip);

	void serverAskForBeginning(Uint8 *data, int size, IPaddress ip);
	
	void serverBroadcastResponse(Uint8 *data, int size, IPaddress ip);
	void serverBroadcastStopHosting(Uint8 *data, int size, IPaddress ip);
	void joinerBroadcastRR(Uint8 *data, int size, IPaddress ip, const char *rrName);
	void joinerBroadcastRequest(Uint8 *data, int size, IPaddress ip);
	void joinerBroadcastResponse(Uint8 *data, int size, IPaddress ip);
	
	void treatData(Uint8 *data, int size, IPaddress ip);

	void onTimer(Uint32 tick);
	char *getStatusString();

	void sendingTime();
	bool sendPresenceRequest();
	bool sendSessionInfoRequest();
	bool sendSessionInfoConfirmation();
	bool send(Uint8 *data, int size);
	bool send(const int v);
	bool send(const int u, const int v);
	void sendBroadcastRequest(IPaddress ip);
	void sendBroadcastRequest(Uint16 port);
	
	bool tryConnection(bool isHostToo);
	bool tryConnection(YOG::GameInfo *yogGameInfo);
	
	void quitThisGame();
	
	Uint16 findLocalPort(UDPsocket socket);
	bool isFileMapDownload(double &progress);

private:
	FILE *logFile;
	FILE *logFileDownload;
};

#endif
