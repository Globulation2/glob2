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
	
	enum{NET_WINDOW_SIZE=1024};
	enum{MAX_WINDOW_SIZE=256};
	struct NetWindowSlot
	{
		//NetWindowState state;
		Uint32 index;
		bool sent;
		bool received;
		int time;
		int packetSize;
	};
	struct PlayerFileTransmission
	{
		bool wantsFile;
		bool receivedFile;
		NetWindowSlot window[NET_WINDOW_SIZE];
		int packetSize;
		int windowSize;
		Uint32 unreceivedIndex;
		
		int totalLost;
		int totalSent;
		int totalReceived;
		int windowstats[MAX_WINDOW_SIZE];
		int windowlosts[MAX_WINDOW_SIZE];
		int onlyWaited;
	};
	PlayerFileTransmission playerFileTra[32];
public:

	bool firstDraw;
	bool shareOnYOG;
	
	HostGlobalState hostGlobalState;
	SessionInfo *savedSessionInfo;
	
	//! A stream to the map (or saved game) file.
	SDL_RWops *stream;
	Uint32 fileSize;

public:
	MultiplayersHost(SessionInfo *sessionInfo, bool shareOnYOG, SessionInfo *savedSessionInfo);
	virtual ~MultiplayersHost();

public:
	int newTeamIndice();
	void initHostGlobalState(void);
	void reinitPlayersState();
	void stepHostGlobalState(void);
	void switchPlayerTeam(int p);
	void kickPlayer(int p);
	void removePlayer(int p);
	void removePlayer(char *data, int size, IPaddress ip);
	void yogClientRequestsGameInfo(char *data, int size, IPaddress ip);
	void newPlayerPresence(char *data, int size, IPaddress ip);
	void playerWantsSession(char *data, int size, IPaddress ip);
	void playerWantsFile(char *data, int size, IPaddress ip);
	void addAI();
	void confirmPlayer(char *data, int size, IPaddress ip);
	void confirmStartCrossConnection(char *data, int size, IPaddress ip);
	void confirmStillCrossConnecting(char *data, int size, IPaddress ip);
	void confirmCrossConnectionAchieved(char *data, int size, IPaddress ip);
	void confirmPlayerStartGame(char *data, int size, IPaddress ip);
	void broadcastRequest(char *data, int size, IPaddress ip);
	void treatData(char *data, int size, IPaddress ip);
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
