/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __PLAYER_H
#define __PLAYER_H

#include "Header.h"
#include "AI.h"
#include "Team.h"
#include <queue>
#include <SDL/SDL_net.h>

class BasePlayer: public Order
{
public:
 	enum PlayerType
	{
		P_NONE=0, // NOTE : we don't need any more because null player are not created
		P_LOST_DROPPING=1, // Player will be droped in any cases, but we still have to exchange orders
		P_LOST_FINAL=2, // Player is no longer considered, may be later changed to P_AI. All orders are NULLs.
		P_AI=3,
		P_IP=4,
		P_LOCAL=5
	};

	enum {MAX_NAME_LENGTH = 32};
	
	PlayerType type;
	
	Sint32 number;
	Uint32 numberMask;
	char name[MAX_NAME_LENGTH];
	Sint32 teamNumber;
	Uint32 teamNumberMask;

	IPaddress ip;
	IPaddress yogip;
	UDPsocket socket;
	int channel;
	bool ipFromNAT;
	bool ipFirewallClean;
	bool waitForNatResolution;
	
	bool quitting; // We have executed the quitting order of player, but we did not freed all his orders.
	int quitStep;
	enum PlayerNetworkState
	{
		PNS_BAD=0,
		
		PNS_PLAYER_SILENT=1,
		
		// host choose a state for joiners between :
		
		PNS_PLAYER_SEND_PRESENCE_REQUEST=2,
		PNS_PLAYER_SEND_SESSION_REQUEST=3,
		PNS_PLAYER_SEND_CHECK_SUM=4,
		
		PNS_OK=5,
		
		PNS_SERVER_SEND_CROSS_CONNECTION_START=6,
		PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START=7,
		PNS_PLAYER_FINISHED_CROSS_CONNECTION=8,
		
		PNS_CROSS_CONNECTED=9,
		
		PNS_SERVER_SEND_START_GAME=10,
		PNS_PLAYER_CONFIRMED_START_GAME=11,
		PNS_PLAYER_PLAYS=12,
		
		// joiners choose a state for joiners between :
		
		PNS_SENDING_FIRST_PACKET=22,
		PNS_HOST=23,
		
		// ai :
		
		PNS_AI=30
	};
	
	PlayerNetworkState netState;
	int netTimeout; // time before next action to repeat.
	int netTimeoutSize;
	int netTOTL; // Number of timeout allowed. TimeOut To Live

private:
	Uint8 data[32+MAX_NAME_LENGTH];
	
public:
	
	BasePlayer(void);
	BasePlayer(Sint32 number, const char name[MAX_NAME_LENGTH], Sint32 teamn, PlayerType type);
	void init();
	virtual ~BasePlayer(void);
	//void close(void);
	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	bool load(SDL_RWops *stream, Sint32 versionMinor);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	
	Uint8 *getData(bool compressed);
	bool setData(const Uint8 *data, int dataLength, bool compressed);
	int getDataLength(bool compressed);
	
	Uint8 *getData() { return getData(false); }
	bool setData(const Uint8 *data, int dataLength) { return setData(data, dataLength, false); }
	int getDataLength() { return getDataLength(false); }
	
	virtual Sint32 checkSum();
	
	void setip(Uint32 host, Uint16 port);
	void setip(IPaddress ip);
	bool sameip(IPaddress ip);
	bool bind(UDPsocket socket);
	void unbind();
	
	bool send(Uint8 *data, int size);
	bool send(Uint8 *data, int size, const Uint8 v);
	bool send(const Uint8 v);
	bool send(const Uint8 u, const Uint8 v);

public:
	bool destroyNet;
	bool disableRecursiveDestruction;
	
public:
	FILE *logFile;
};

class Player:public BasePlayer
{
public:
	Player();
	Player(SDL_RWops *stream, Team *teams[32], Sint32 versionMinor);
	Player(Sint32 number, const char name[MAX_NAME_LENGTH], Team *team, PlayerType type);
	virtual ~Player(void);

	void setTeam(Team *team);
	void setBasePlayer(const BasePlayer *initial, Team *teams[32]);
	
	void makeItAI();
	
	bool load(SDL_RWops *stream, Team *teams[32], Sint32 versionMinor);
	void save(SDL_RWops *stream);
	
public:
	Sint32 startPositionX, startPositionY;

	// team is the basic (structural) pointer. The others are directs access.
	Team *team;
	Game *game;
	Map *map;
	
	AI *ai;

public:
	Sint32 checkSum();
};

#endif
 
