/*
� � Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�re
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

� � This program is free software; you can redistribute it and/or modify
� � it under the terms of the GNU General Public License as published by
� � the Free Software Foundation; either version 2 of the License, or
� � (at your option) any later version.

� � This program is distributed in the hope that it will be useful,
� � but WITHOUT ANY WARRANTY; without even the implied warranty of
� � MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. �See the
� � GNU General Public License for more details.

� � You should have received a copy of the GNU General Public License
� � along with this program; if not, write to the Free Software
� � Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA �02111-1307 �USA
*/

#ifndef __PLAYER_H
#define __PLAYER_H

#include "GAG.h"
#include "AI.h"
#include "Team.h"
#include <queue>

class BasePlayer: public Order
{
public:
 	enum PlayerType
	{
		P_NONE=0, // NOTE : we don't need any more because null player are not created
		P_LOST_A, // Player will be droped in any cases, but we still have to exchange orders
		P_LOST_B, // Player is no longer considered, may be later changed to P_AI
		P_AI,
		P_IP,
		P_LOCAL
	};

	enum {MAX_NAME_LENGTH = 32};
	
	PlayerType type;
	
	Sint32 number;
	Uint32 numberMask;
	char name[MAX_NAME_LENGTH];
	Sint32 teamNumber;
	Uint32 teamNumberMask;

	IPaddress ip;
	UDPsocket socket;
	int channel;
	
	bool quitting; // forNetGame: true while the player is fairly quitting.
	int quitStep;
	enum PlayerNetworkState
	{
		PNS_BAD=0,
		
		PNS_PLAYER_SILENT,
		
		// host choose a state for joiners between :
		
		PNS_PLAYER_SEND_PRESENCE_REQUEST,
		PNS_PLAYER_SEND_SESSION_REQUEST,
		PNS_PLAYER_SEND_FILE_REQUEST,
		PNS_PLAYER_SEND_CHECK_SUM,
		
		PNS_OK,
		
		PNS_SERVER_SEND_CROSS_CONNECTION_START,
		PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START,
		PNS_PLAYER_FINISHED_CROSS_CONNECTION,
		
		PNS_CROSS_CONNECTED,
		
		PNS_SERVER_SEND_START_GAME,
		PNS_PLAYER_CONFIRMED_START_GAME,
		PNS_PLAYER_PLAYS,
		
		// joiners choose a state for joiners between :
		
		PNS_BINDED,
		PNS_SENDING_FIRST_PACKET,
		PNS_HOST,
		
		// ai :
		
		PNS_AI
	};
	
	PlayerNetworkState netState;
	int netTimeout; // time before next action to repeat.
	int netTimeoutSize;
	int netTOTL; // Number of timeout allowed. TimeOut To Live
	
	bool wantsFile;
	enum WindowState
	{
		WS_BAD=0,
		
		WS_LOST=1,
		WS_UNSENT=2,
		WS_SENT=3,
		WS_RECEIVED=4
	};
	enum {NETWORK_BETA = 512};
	WindowState window[512];
	int windowSize;
	int windowIndex;

private:
	char data[28+MAX_NAME_LENGTH];
public:
	
	BasePlayer(void);
	BasePlayer(Sint32 number, const char name[MAX_NAME_LENGTH], Sint32 teamn, PlayerType type);
	void init();
	virtual ~BasePlayer(void);
	void close(void);
	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	bool load(SDL_RWops *stream, Sint32 versionMinor);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	virtual Sint32 checkSum();
	
	void setip(Uint32 host, Uint16 port);
	void setip(IPaddress ip);
	void BasePlayer::printip(char s[32]);
	bool sameip(IPaddress ip);
	bool bind(UDPsocket socket, int channel);
	void unbind();
	
	bool send(char *data, int size);
	bool send(const int v);
	bool send(const int u, const int v);

public:
	bool destroyNet;
	bool disableRecursiveDestruction;

public:
	void printNetState(char s[128]);
};

class Player:public BasePlayer
{
public:
	Player();
	Player(SDL_RWops *stream, Team *teams[32], Sint32 versionMinor);
	Player(Sint32 number, const char name[MAX_NAME_LENGTH], Team *team, PlayerType type);
	virtual ~Player(void);

	void setBasePlayer(const BasePlayer *initial, Team *teams[32]);
	
	void makeItAI();
	
	bool load(SDL_RWops *stream, Team *teams[32], Sint32 versionMinor);
	void save(SDL_RWops *stream);
	
public:
	Sint32 startPositionX, startPositionY;

	Team *team;
	AI *ai;

public:
	Sint32 checkSum();
};

#endif
 
