/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#ifndef __SESSION_H
#define __SESSION_H

#include "GAG.h"
#include "Order.h"
#include "Team.h"
#include "Player.h"
#include "Map.h"

//! Save in stream at offset the actual file pos
#define SAVE_OFFSET(stream, offset) \
	{ \
		Uint32 pos=SDL_RWtell(stream); \
		SDL_RWseek(stream, offset, SEEK_SET); \
		SDL_WriteBE32(stream, pos); \
		SDL_RWseek(stream, pos, SEEK_SET); \
	}

//! This is named SessionGame but in fact it is Glob2's map headers.
//! Map Specific infos are not serialized and don't go through network
class SessionGame:public Order
{
public:
	SessionGame();
	SessionGame(const SessionGame &sessionGame);
	virtual ~SessionGame(void) { }
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);

	virtual Uint8 getOrderType();
	virtual char *getData();
	bool setData(const char *data, int dataLength);
	virtual int getDataLength();
	
	virtual Sint32 checkSum();
	
public:
	//! Major map version. Change only with structural modification
	Sint32 versionMajor;
	//! Minor map version. Change each time something has been changed in serialized version.
	Sint32 versionMinor;

	//! Offset of SessionInfo own's data from beginning of file
	Uint32 sessionInfoOffset;
	//! Offset of Game own's data from beginning of file
	Uint32 gameOffset;
	//! Offset of array of teams from beginning of file
	Uint32 teamsOffset;
	//! Offset of array of players from beginning of file
	Uint32 playersOffset;
	//! Offset of map (terrain) data from beginning of file
	Uint32 mapOffset;

	Sint32 numberOfPlayer;
	Sint32 numberOfTeam;

	//! TPF = Tick per frame.
	Sint32 gameTPF;
	//! Number of tick between order issue and order commit. This is the maximum lag during a game
	Sint32 gameLatency;
	
	Sint32 fileIsAMap;
protected:
	//! Serialized form of SessionGame
	enum {S_GAME_DATA_SIZE=28};
	char data[S_GAME_DATA_SIZE];
};

//! The session that indirectly derive from Order.
//! This session will go through the network at connection time
class SessionInfo:public SessionGame
{
public:
	SessionInfo();
	SessionInfo(const SessionGame &sessionGame);
	virtual ~SessionInfo(void) { }
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	Sint32 checkSum();
	
	bool setLocal(int p);
	
	int getTeamNumber(char playerName[BasePlayer::MAX_NAME_LENGTH], int team);
	int getAITeamNumber(SessionInfo *currentSessionInfo, int team);
	
	//! draw a list of players
	void draw(DrawableSurface *gfx);
	//! get information on player in a nice string
	void getPlayerInfo(int playerNumber, int *teamNumber, char *infoString, SessionInfo *savedSessionInfo, int stringLen);

public:
	BaseMap map;
	BasePlayer players[32];
	BaseTeam team[32];

protected:
	//! Serialized form of SessionInfo
	enum {S_INFO_DATA_SIZE=2464+S_GAME_DATA_SIZE};
	char data[S_INFO_DATA_SIZE];
};

#endif 
 
