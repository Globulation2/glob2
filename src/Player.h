/*
 * Globulation 2 player support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __PLAYER_H
#define __PLAYER_H

#include "GAG.h"
#include "AI.h"
#include "Team.h"
#include <queue>
#include "SDL_net.h"

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

	enum {
		MAX_NAME_LENGTH = 16
	};
	
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
		
		// client to players state:
		
		PNS_PLAYER_SEND_ONE_REQUEST,
		PNS_SERVER_SEND_SESSION_INFO,
		PNS_PLAYER_SEND_CHECK_SUM,
		
		PNS_OK,
		
		PNS_SERVER_SEND_CROSS_CONNECTION_START,
		PNS_PLAYER_CONFIRMED_CROSS_CONNECTION_START,
		PNS_PLAYER_FINISHED_CROSS_CONNECTION,
		
		PNS_CROSS_CONNECTED,
		
		PNS_SERVER_SEND_START_GAME,
		PNS_PLAYER_CONFIRMED_START_GAME,
		PNS_PLAYER_PLAYS,
		
		// players to players states:
		
		PNS_BINDED,
		PNS_SENDING_FIRST_PACKET,
		PNS_HOST
	};
	
	PlayerNetworkState netState;
	int netTimeout; // time before next action to repeat.
	int netTimeoutSize;
	int netTOTL; // Number of timeout allowed. TimeOut To Live

private:
	char data[44];	
public:
	
	BasePlayer(void);
	BasePlayer(Sint32 number, const char name[16], Sint32 teamn, PlayerType type);
	void init();
	virtual ~BasePlayer(void);
	void close(void);
	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	void load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	virtual Sint32 checkSum();
	
	void setip(Uint32 host, Uint16 port);
	void setip(IPaddress ip);
	bool sameip(IPaddress ip);
	bool bind();
	void unbind();
	
	bool send(char *data, int size);
	bool send(const int v);
	bool send(const int u, const int v);
	
	bool destroyNet;
};

class Player:public BasePlayer
{
public:
	Player();
	Player(SDL_RWops *stream, Team *teams[32]);
	Player(Sint32 number, const char name[16], Team *team, PlayerType type);
	virtual ~Player(void);

	void setBasePlayer(const BasePlayer *initial, Team *teams[32]);
	void load(SDL_RWops *stream, Team *teams[32]);
	void save(SDL_RWops *stream);

public:
	Sint32 startPositionX, startPositionY;

	Team *team;
	AI *ai;

public:
	Sint32 checkSum();
};

#endif
 
