/*
 * Globulation 2 session support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __SESSION_H
#define __SESSION_H

#include "GAG.h"
#include "Order.h"
#include "Team.h"
#include "Player.h"
#include "Map.h"

class SessionGame:public Order
{
public:
	SessionGame();
	virtual ~SessionGame(void) { }
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);

	virtual Uint8 getOrderType();
	virtual char *getData();
	bool setData(const char *data, int dataLength);
	virtual int getDataLength();
	
	virtual Sint32 checkSum();
	
public:
	Sint32 numberOfPlayer;
	Sint32 numberOfTeam;

	Sint32 gameTPF;//Tick per frame.
	Sint32 gameLatency;
private:
	char data[16];
};

class SessionInfo:public SessionGame
{
public:
	SessionInfo();
	virtual ~SessionInfo(void) { }
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	Sint32 checkSum();
	
	bool setLocal(int p);
	
public:
	BaseMap map;
	BasePlayer players[32];
	BaseTeam team[32];
	
private:
	char data[1968];
};

#endif 
 
