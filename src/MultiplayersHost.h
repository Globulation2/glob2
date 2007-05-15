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

#ifndef __MULTIPLAYERHOST_H
#define __MULTIPLAYERHOST_H

#include "MultiplayersCrossConnectable.h"
#include "GAG.h"
class MultiplayersJoin;

class MultiplayersHost: public MultiplayersCrossConnectable
{
public:

	enum HostGlobalState
	{
		HGS_BAD=0,
		HGS_SHARING_SESSION_INFO=2,
		HGS_WAITING_CROSS_CONNECTIONS=3,
		HGS_ALL_PLAYERS_CROSS_CONNECTED=4,
		HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE=5,
		HGS_GAME_START_SENDED=6,
		HGS_PLAYING_COUNTER=7 // the counter 5-4-3-2-1-0 is playing
	};

	enum{SECONDS_BEFORE_START_GAME=5};
	
	enum{PACKET_SLOTS=1024};
	struct PacketSlot
	{
		Uint32 index;
		bool sent, received;
		int brandwidth;
		int time;
	};
	struct PlayerFileTransmission
	{
		bool wantsFile;
		bool receivedFile;
		Uint32 unreceivedIndex;
		int brandwidth;
		int lastNbPacketsLost;
		PacketSlot packetSlot[PACKET_SLOTS];
		int time;
		int latency;
		int totalSent;
		int totalLost;
		int totalReceived;
	};
	PlayerFileTransmission playerFileTra[32];
	
public:

	bool firstDraw;
	bool shareOnYOG;
	
	HostGlobalState hostGlobalState;
	SessionInfo *savedSessionInfo;
	
	//! A stream to the map (or saved game) file.
	GAGCore::InputStream *stream;
	Uint32 fileSize;
	Uint32 mapFileCheckSum;

public:
	MultiplayersHost(SessionInfo *sessionInfo, bool shareOnYOG, SessionInfo *savedSessionInfo);
	virtual ~MultiplayersHost();

public:
	int newTeamIndice();
	void initHostGlobalState(void);
	void reinitPlayersState();
	void stepHostGlobalState(void);
	void switchPlayerTeam(int p, int newTeamNumber);
	void kickPlayer(int p);
	void removePlayer(int p);
	void removePlayer(Uint8 *data, int size, IPaddress ip);
	void yogClientRequestsGameInfo(Uint8 *data, int size, IPaddress ip);
	void newPlayerPresence(Uint8 *data, int size, IPaddress ip);
	void playerWantsSession(Uint8 *data, int size, IPaddress ip);
	void playerWantsFile(Uint8 *data, int size, IPaddress ip);
	void addAI(AI::ImplementitionID aiImplementationId);
	void confirmPlayer(Uint8 *data, int size, IPaddress ip);
	void confirmStartCrossConnection(Uint8 *data, int size, IPaddress ip);
	void confirmStillCrossConnecting(Uint8 *data, int size, IPaddress ip);
	void confirmCrossConnectionAchieved(Uint8 *data, int size, IPaddress ip);
	void confirmPlayerStartGame(Uint8 *data, int size, IPaddress ip);
	void broadcastRequest(Uint8 *data, int size, IPaddress ip);
	void treatData(Uint8 *data, int size, IPaddress ip);
	void onTimer(Uint32 tick, MultiplayersJoin *multiplayersJoin);
	void sendingTime();
	bool send(const int v);
	bool send(const int u, const int v);
	void sendBroadcastLanGameHosting(Uint16 port, bool create);
	void stopHosting(void);
	void startGame(void);

private:
	FILE *logFile;
	FILE *logFileDownload;
};

#endif
