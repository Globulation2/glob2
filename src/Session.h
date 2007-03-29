/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __SESSION_H
#define __SESSION_H

#include "MapGenerationDescriptor.h"
#include "Player.h"
#include "Team.h"

//! Save in stream at offset the actual file pos
#define SAVE_OFFSET(stream, offset, name) \
	{ \
		if (stream->canSeek()) \
		{ \
			Uint32 pos = stream->getPosition(); \
			stream->seekFromStart(offset); \
			stream->writeUint32(pos, name); \
			stream->seekFromStart(pos); \
		} \
	}

//! This is named SessionGame but in fact it is Glob2's map headers.
//! Map Specific infos are not serialized and don't go through network
class SessionGame
{
public:
	SessionGame();
	SessionGame(const SessionGame &sessionGame);
	SessionGame& operator=(const SessionGame& sessionGame);
	virtual ~SessionGame(void);
	bool load(GAGCore::InputStream *stream);
	void save(GAGCore::OutputStream *stream);

	virtual Uint8 getOrderType();

	virtual Uint8 *getData(bool compressed);
	virtual bool setData(const Uint8 *data, int dataLength, bool compressed);
	virtual int getDataLength(bool compressed);

	virtual Uint8 *getData() { return SessionGame::getData(false); }
	virtual bool setData(const Uint8 *data, int dataLength) { return SessionGame::setData(data, dataLength, false); }
	virtual int getDataLength() { return SessionGame::getDataLength(false); }

	virtual Uint32 checkSum();

	// internal and external map/game/file name manipulation
	//! Safely copy s to mapName[] and remove the extention if needed.
	//! Set the user-friendly name of the map
	void setMapName(const char *s);
	//! Return the user-friendly name of the map
	std::string getMapName(void) const;
	//! Return the user-friendly name of the map, C version
	const char *getMapNameC(void) const;
	//! Return the filename of the map, including std directories.
	std::string getFileName(void) const;
	
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
	//! Offset of map script data from beginning of file
	Uint32 mapScriptOffset;
	//! Offset of generationDescriptor data from beginning of file
	Uint32 generationDescriptorOffset;

	Sint32 numberOfPlayer;
	Sint32 numberOfTeam;
	Sint32 varPrestige;

	//! TPF = Tick per frame.
	Sint32 gameTPF;
	//! Number of tick between order issue and order commit. This is the maximum lag during a game
	Sint32 gameLatency;
	//! 1 : file is a map, 0 : file is a game
	Sint32 fileIsAMap;

	MapGenerationDescriptor *mapGenerationDescriptor;

	enum { MAP_NAME_MAX_SIZE=64 };

protected:
	//! Name of map or game, serialized
	char mapName[MAP_NAME_MAX_SIZE];

	//! Serialized form of SessionGame
	enum {S_GAME_ONLY_DATA_SIZE=32+MAP_NAME_MAX_SIZE};
	enum {S_GAME_DATA_SIZE=S_GAME_ONLY_DATA_SIZE+MapGenerationDescriptor::DATA_SIZE};
	Uint8 data[S_GAME_DATA_SIZE];
protected:
	FILE *logFile;
};

//! The session that indirectly derive from Order.
//! This session will go through the network at connection time
class SessionInfo:public SessionGame
{
public:
	SessionInfo();
	SessionInfo(const SessionGame &sessionGame);
	virtual ~SessionInfo(void) { }
	bool load(GAGCore::InputStream *stream);
	void save(GAGCore::OutputStream *stream);

	Uint8 getOrderType();

	Uint8 *getData(bool compressed);
	bool setData(const Uint8 *data, int dataLength, bool compressed);
	int getDataLength(bool compressed);

	Uint8 *getData() { return SessionInfo::getData(false); }
	bool setData(const Uint8 *data, int dataLength) { return SessionInfo::setData(data, dataLength, false); }
	int getDataLength() { return SessionInfo::getDataLength(false); }

	Uint32 checkSum();

	bool setLocal(int p);

	int getTeamNumber(char playerName[32], int team);
	int getAITeamNumber(SessionInfo *currentSessionInfo, int team);

	//! get information on player in a nice string
	void getPlayerInfo(int playerNumber, int *teamNumber, std::string &infoString, SessionInfo *savedSessionInfo);

public:
//	BaseMap map;
	BasePlayer players[32];
	BaseTeam teams[32];

protected:
	//! Serialized form of SessionInfo
	enum {S_INFO_ONLY_DATA_SIZE=2560};
	enum {S_INFO_DATA_SIZE=S_INFO_ONLY_DATA_SIZE+S_GAME_DATA_SIZE};
	Uint8 data[S_INFO_DATA_SIZE];
};

#endif 
 
