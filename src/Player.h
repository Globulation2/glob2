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

#ifndef __PLAYER_H
#define __PLAYER_H

#include <assert.h>
#include <vector>

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_net.h>
#else
#include <Types.h>
#endif

#include "AI.h"

class Game;
class Map;
class Team;
namespace GAGCore
{
	class InputStream;
}

class BasePlayer
{
public:
 	enum PlayerType
	{
		P_NONE=0, // NOTE : we don't need any more because null player are not created
		P_LOST_DROPPING=1, // Player will be droped in any cases, but we still have to exchange orders
		P_LOST_FINAL=2, // Player is no longer considered, may be later changed to P_AI. All orders are NULLs.
		/*P_AI=3,
		P_IP=4,
		P_LOCAL=5*/
		P_IP=3,
		P_LOCAL=4,
		P_AI=5,
		// Note : P_AI + n is AI type n
	};
	
	static AI::ImplementitionID implementitionIdFromPlayerType(PlayerType type)
	{
		assert(type>=P_AI);
		return (AI::ImplementitionID)((int)type-(int)P_AI);
	}
	static PlayerType playerTypeFromImplementitionID(AI::ImplementitionID iid)
	{
		return (PlayerType)((int)iid+(int)P_AI);
	}

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
	Uint32 quitUStep;
	Uint32 lastUStepToExecute;
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
	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	Uint8 getOrderType();
	
	Uint8 *getData(bool compressed);
	bool setData(const Uint8 *data, int dataLength, bool compressed);
	int getDataLength(bool compressed);
	
	Uint32 checkSum();
	
	void makeItAI(AI::ImplementitionID aiType);
	
	void setip(Uint32 host, Uint16 port);
	void setip(IPaddress ip);
	bool sameip(IPaddress ip);
	bool localhostip();
	bool bind(UDPsocket socket);
	void unbind();
	
	bool send(Uint8 *data, int size);
	bool send(Uint8 *data, int size, const Uint8 v);
	bool send(const Uint8 v);
	bool send(const Uint8 u, const Uint8 v);
	bool send(const Uint8 u, const Uint8 v, const Uint32 checksum);

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
	Player(GAGCore::InputStream *stream, Team *teams[32], Sint32 versionMinor);
	Player(Sint32 number, const char name[MAX_NAME_LENGTH], Team *team, PlayerType type);
	virtual ~Player(void);

	void setTeam(Team *team);
	void setBasePlayer(const BasePlayer *initial, Team *teams[32]);
	
	bool load(GAGCore::InputStream *stream, Team *teams[32], Sint32 versionMinor);
	void save(GAGCore::OutputStream  *stream);
	
public:
	Sint32 startPositionX, startPositionY;

	// team is the basic (structural) pointer. The others are directs access.
	Team *team;
	Game *game;
	Map *map;
	
	AI *ai;

public:
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
};

#endif
