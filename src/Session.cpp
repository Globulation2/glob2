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

#include "Session.h"
#include "Order.h"
#include "Marshaling.h"
#include "Version.h"
#include "LogFileManager.h"
#include "Utilities.h"
#include "Game.h"
#include "GlobalContainer.h"

#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>

SessionGame::SessionGame()
{
	versionMajor=VERSION_MAJOR;
	versionMinor=VERSION_MINOR;
	sessionInfoOffset=0;
	gameOffset=0;
	teamsOffset=0;
	playersOffset=0;
	mapOffset=0;
	mapScriptOffset=0;
	generationDescriptorOffset=0;

	numberOfPlayer=0;
	numberOfTeam=0;
//yes this is ugly but best way I could find to prevent loss of variables set
//when map is selected
	varPrestige=globalContainer->settings.tempVarPrestige;
	gameTPF=40;
	gameLatency=5;

	fileIsAMap=(Sint32)true;
	strncpy(mapName,"No name", MAP_NAME_MAX_SIZE);

	mapGenerationDescriptor=NULL;

	logFile=globalContainer->logFileManager->getFile("SessionGame.log");
}

SessionGame::~SessionGame(void)
{
	if (mapGenerationDescriptor)
		delete mapGenerationDescriptor;
	mapGenerationDescriptor=NULL;
}

SessionGame::SessionGame(const SessionGame &sessionGame)
{
	*this=sessionGame;
}

SessionGame& SessionGame::operator=(const SessionGame& sessionGame)
{
	//memcpy(this, &sessionGame, sizeof(*this));
	
	versionMajor=sessionGame.versionMajor;
	versionMinor=sessionGame.versionMinor;
	sessionInfoOffset=sessionGame.sessionInfoOffset;
	gameOffset=sessionGame.gameOffset;
	teamsOffset=sessionGame.teamsOffset;
	playersOffset=sessionGame.playersOffset;
	mapOffset=sessionGame.mapOffset;
	mapScriptOffset=sessionGame.mapScriptOffset;
	generationDescriptorOffset=sessionGame.generationDescriptorOffset;
	numberOfPlayer=sessionGame.numberOfPlayer;
	numberOfTeam=sessionGame.numberOfTeam;
	varPrestige=sessionGame.varPrestige;
	gameTPF=sessionGame.gameTPF;
	gameLatency=sessionGame.gameLatency;
	fileIsAMap=sessionGame.fileIsAMap;

	memcpy(mapName, sessionGame.mapName, sizeof(mapName));

	mapGenerationDescriptor=NULL;
	if (sessionGame.mapGenerationDescriptor)
		mapGenerationDescriptor=new MapGenerationDescriptor(*sessionGame.mapGenerationDescriptor);
	
	return *this;
}

void SessionGame::save(GAGCore::OutputStream *stream)
{
	versionMajor = VERSION_MAJOR;
	versionMinor = VERSION_MINOR;
	stream->writeEnterSection("SessionGame");
	stream->write("SEGb", 4, "signatureStart");
	stream->writeSint32(versionMajor, "versionMajor");
	stream->writeSint32(versionMinor, "versionMinor");
	// save 0, will be rewritten after
	stream->writeUint32(0, "sessionInfoOffset");
	stream->writeUint32(0, "gameOffset");
	stream->writeUint32(0, "teamsOffset");
	stream->writeUint32(0, "playersOffset");
	stream->writeUint32(0, "mapOffset");
	stream->writeUint32(0, "mapScriptOffset");
	stream->writeUint32(0, "generationDescriptorOffset");
	stream->writeSint32(numberOfPlayer, "numberOfPlayer");
	stream->writeSint32(numberOfTeam, "numberOfTeam");
	stream->writeSint32(gameTPF, "gameTPF");
	stream->writeSint32(gameLatency, "gameLatency");
	stream->writeSint32(fileIsAMap, "fileIsAMap");
	if (mapGenerationDescriptor)
	{
		stream->writeSint32((Sint32)true, "mapGenerationDescriptor");
		SAVE_OFFSET(stream, 36, "generationDescriptorOffset");
		mapGenerationDescriptor->save(stream);
	}
	else
		stream->writeSint32((Sint32)false, "mapGenerationDescriptor");
	stream->write("SEGe", 4, "signatureEnd");
	stream->writeLeaveSection();
}

bool SessionGame::load(GAGCore::InputStream *stream)
{
	stream->readEnterSection("SessionGame");
	char signature[4];
	stream->read(signature, 4, "signatureStart");

	versionMajor = stream->readSint32("versionMajor");
	if (versionMajor != VERSION_MAJOR)
	{
		stream->readLeaveSection();
		return false;
	}

	versionMinor = stream->readSint32("versionMinor");
	if (versionMinor < MINIMUM_VERSION_MINOR)
	{
		stream->readLeaveSection();
		return false;
	}

	if (memcmp(signature,"SEGb",4)!=0)
	{
		stream->readLeaveSection();
		return false;
	}
	
	sessionInfoOffset = stream->readUint32("sessionInfoOffset");
	gameOffset = stream->readUint32("gameOffset");
	teamsOffset = stream->readUint32("teamsOffset");
	playersOffset = stream->readUint32("playersOffset");
	mapOffset = stream->readUint32("mapOffset");
	mapScriptOffset = stream->readUint32("mapScriptOffset");
	generationDescriptorOffset = stream->readUint32("generationDescriptorOffset");
	
	numberOfPlayer = stream->readSint32("numberOfPlayer");
	numberOfTeam = stream->readSint32("numberOfTeam");
	gameTPF = stream->readSint32("gameTPF");
	gameLatency = stream->readSint32("gameLatency");
	fileIsAMap = stream->readSint32("fileIsAMap");
	
	if (mapGenerationDescriptor)
		delete mapGenerationDescriptor;
	mapGenerationDescriptor=NULL;
	bool isDescriptor;

	isDescriptor=(bool)stream->readSint32("mapGenerationDescriptor");

	if (isDescriptor)
	{
		stream->seekFromStart(generationDescriptorOffset);
		mapGenerationDescriptor = new MapGenerationDescriptor();
		mapGenerationDescriptor->load(stream, versionMinor);
	}
	else
		mapGenerationDescriptor = NULL;
	
	stream->read(signature, 4, "signatureEnd");
	if (memcmp(signature,"SEGe",4)!=0)
	{
		stream->readLeaveSection();
		return false;
	}

	stream->readLeaveSection();
	return true;
}

Uint8 SessionGame::getOrderType()
{
	return DATA_SESSION_GAME;
}
/*
void SessionInfo::draw(DrawableSurface *gfx)
{
	char playerName[32];
	for (int i=0; i<numberOfPlayer; i++)
	{
		int teamNumber=players[i].teamNumber;
		strncpy(playerName, players[i].name, 32);
		int dx=320*(i/8);
		int dy=20*(i%8);
		BaseTeam &te=team[teamNumber];
		gfx->drawFilledRect(22+dx, 42+dy, 16, 16, te.colorR, te.colorG, te.colorB);
		gfx->drawString(40+dx, 40+dy, globalContainer->standardFont, playerName);
	}
}
*/
void SessionInfo::getPlayerInfo(int playerNumber, int *teamNumber, std::string &infoString, SessionInfo *savedSessionInfo)
{
	assert(playerNumber>=0);
	assert(playerNumber<numberOfPlayer);
	*teamNumber=players[playerNumber].teamNumber;
	if (players[playerNumber].type==BasePlayer::P_IP)
		infoString = FormatableString("%0 : %1").arg(players[playerNumber].name).arg(Utilities::stringIP(players[playerNumber].ip));
	else if (players[playerNumber].type>=BasePlayer::P_AI)
		infoString = FormatableString("%0 : (%1)").arg(players[playerNumber].name).arg(Toolkit::getStringTable()->getString("[AI]", players[playerNumber].type-BasePlayer::P_AI));
	else
		assert(false);
}

Uint8 *SessionGame::getData(bool compressed)
{
	if (compressed)
	{
		fprintf(logFile, "getData::[%d, %d, %d, %d, %d, %d, %d, %p]\n",
			versionMajor, versionMinor, numberOfPlayer, numberOfTeam, gameTPF, gameLatency, fileIsAMap, mapGenerationDescriptor);
		if (mapGenerationDescriptor)
			fprintf(logFile, "mgscs=%x\n", mapGenerationDescriptor->checkSum());
		addSint8(data, (Sint8)versionMajor, 0);
		addSint8(data, (Sint8)versionMinor, 1);
		addSint8(data, (Sint8)numberOfPlayer, 2);
		addSint8(data, (Sint8)numberOfTeam, 3);
		addSint8(data, (Sint8)gameTPF, 4);
		addSint8(data, (Sint8)gameLatency, 5);
		addSint8(data, (Sint8)fileIsAMap, 6);

		int l=Utilities::strmlen(mapName, sizeof(mapName));
		memcpy(data+7, mapName, l);

		if (mapGenerationDescriptor)
		{
			addSint8(data, 1, 7+l);
			// TODO: make a compression for mapGenerationDescriptor
			assert(mapGenerationDescriptor->getDataLength()==MapGenerationDescriptor::DATA_SIZE);
			memcpy(data+8+l, mapGenerationDescriptor->getData(), MapGenerationDescriptor::DATA_SIZE);
		}
		else
			addSint8(data, 0, 7+l);

	}
	else
	{
		addSint32(data, versionMajor, 0);
		addSint32(data, versionMinor, 4);
		addSint32(data, numberOfPlayer, 8);
		addSint32(data, numberOfTeam, 12);
		addSint32(data, gameTPF, 16);
		addSint32(data, gameLatency, 20);
		addSint32(data, fileIsAMap, 24);
		if (mapGenerationDescriptor)
		{
			addSint32(data, 1, 28);
			assert(mapGenerationDescriptor->getDataLength()==MapGenerationDescriptor::DATA_SIZE);
			memcpy(data+32, mapGenerationDescriptor->getData(), MapGenerationDescriptor::DATA_SIZE);
		}
		else
			addSint32(data, 0, 28);
		memcpy(data+32, mapName, sizeof(mapName));
	}
	return data;
}

bool SessionGame::setData(const Uint8 *data, int dataLength, bool compressed)
{
	if (compressed)
	{
		if (dataLength!=SessionGame::getDataLength(true))
			return false;

		versionMajor=getSint8(data, 0);
		versionMinor=getSint8(data, 1);
		numberOfPlayer=getSint8(data, 2);
		numberOfTeam=getSint8(data, 3);
		gameTPF=getSint8(data, 4);
		gameLatency=getSint8(data, 5);
		fileIsAMap=getSint8(data, 6);

		int l=Utilities::strmlen((char *)(data+7), sizeof(mapName));
		memcpy(mapName, data+7, l);
		assert(mapName[sizeof(mapName)-1]==0);

		if (mapGenerationDescriptor)
			delete mapGenerationDescriptor;
		mapGenerationDescriptor=NULL;
		bool isDescriptor=getSint8(data, 7+l);
		if (isDescriptor)
		{
			mapGenerationDescriptor=new MapGenerationDescriptor();
			mapGenerationDescriptor->setData(data+8+l, MapGenerationDescriptor::DATA_SIZE);
		}
		else
			mapGenerationDescriptor=NULL;
		fprintf(logFile, "setData::[%d, %d, %d, %d, %d, %d, %d, %p]\n",
			versionMajor, versionMinor, numberOfPlayer, numberOfTeam, gameTPF, gameLatency, fileIsAMap, mapGenerationDescriptor);
		if (mapGenerationDescriptor)
			fprintf(logFile, "mgscs=%x\n", mapGenerationDescriptor->checkSum());
	}
	else
	{
		if (dataLength!=SessionGame::getDataLength())
			return false;

		versionMajor=getSint32(data, 0);
		versionMinor=getSint32(data, 4);
		numberOfPlayer=getSint32(data, 8);
		numberOfTeam=getSint32(data, 12);
		gameTPF=getSint32(data, 16);
		gameLatency=getSint32(data, 20);
		fileIsAMap=getSint32(data, 24);
		if (mapGenerationDescriptor)
			delete mapGenerationDescriptor;
		mapGenerationDescriptor=NULL;
		bool isDescriptor=getSint32(data, 28);
		if (isDescriptor)
		{
			mapGenerationDescriptor=new MapGenerationDescriptor();
			mapGenerationDescriptor->setData(data+32, MapGenerationDescriptor::DATA_SIZE);
		}
		else
			mapGenerationDescriptor=NULL;

		memcpy(mapName, data+32+MapGenerationDescriptor::DATA_SIZE, sizeof(mapName));
		assert( Utilities::strnlen(mapName, sizeof(mapName)) < (int)sizeof(mapName) );
	}
		
	return true;
}


int SessionGame::getDataLength(bool compressed)
{
	if (compressed)
	{
		int l=8+Utilities::strmlen(mapName, sizeof(mapName));
		if (mapGenerationDescriptor)
			l+=mapGenerationDescriptor->getDataLength();
		return l;
	}
	else
	{
		if (mapGenerationDescriptor)
			return S_GAME_DATA_SIZE;
		else
			return S_GAME_ONLY_DATA_SIZE;
	}
}

Uint32 SessionGame::checkSum()
{
	Uint32 cs=0;

	cs^=versionMajor;
	cs^=versionMinor;
	cs^=numberOfPlayer;
	cs^=numberOfTeam;
	cs=(cs<<31)|(cs>>1);
	cs^=gameTPF;
	cs^=gameLatency;
	cs=(cs<<31)|(cs>>1);
	
	if (mapGenerationDescriptor)
	{
		cs=(cs<<31)|(cs>>1);
		cs^=mapGenerationDescriptor->checkSum();
	}
	//printf("versionMajor=%d, versionMinor=%d, numberOfPlayer=%d, numberOfTeam=%d, gameTPF=%d, gameLatency=%d.\n", 
	//	versionMajor, versionMinor, numberOfPlayer, numberOfTeam, gameTPF, gameLatency);
	//printf("mapGenerationDescriptor=%x", mapGenerationDescriptor);
	
	fprintf(logFile, "SessionGame::sc=%x.\n", cs);
	
	return cs;
}

void SessionGame::setMapName(const char *s)
{
	assert(s);
	assert(s[0]);
	strncpy(mapName, s, sizeof(mapName));
	mapName[MAP_NAME_MAX_SIZE-1]=0;
}

std::string SessionGame::getMapName() const
{
	//printf("(get)mapName=(%s).\n", mapName);
	return std::string(mapName);
}

const char *SessionGame::getMapNameC(void) const
{
	return mapName;
}

std::string SessionGame::getFileName(void) const
{
	if (fileIsAMap)
		return glob2NameToFilename("maps", mapName, "map");
	else
		return glob2NameToFilename("games", mapName, "game");
}

SessionInfo::SessionInfo()
:SessionGame()
{
}

SessionInfo::SessionInfo(const SessionGame &sessionGame)
:SessionGame(sessionGame)
{
}

void SessionInfo::save(GAGCore::OutputStream *stream)
{
	SessionGame::save(stream);

	stream->writeEnterSection("SessionInfo");
	// update to this version
	SAVE_OFFSET(stream, 12, "sessionInfoOffset");
	
	stream->write(mapName, MAP_NAME_MAX_SIZE, "mapName");

	stream->write("GLO2", 4, "signatureStart");
	
	stream->writeEnterSection("players");
	for (int i=0; i<numberOfPlayer; i++)
	{
		stream->writeEnterSection(i);
		players[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	
	stream->writeEnterSection("teams");
	for (int i=0; i<numberOfTeam; i++)
	{
		stream->writeEnterSection(i);
		teams[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->write("GLO2", 4, "signatureEnd");
	stream->writeLeaveSection();
}

bool SessionInfo::load(GAGCore::InputStream *stream)
{
	char signature[4];

	if (!SessionGame::load(stream))
		return false;

	stream->readEnterSection("SessionGame");
	if (stream->canSeek())
		stream->seekFromStart(sessionInfoOffset);

	stream->read(mapName, MAP_NAME_MAX_SIZE, "mapName");
	//std::cout << "SessionInfo::load : end-user map name is " << mapName << std::endl;

	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature,"GLO2",4) != 0)
	{
		stream->readLeaveSection();
		return false;
	}

	stream->readEnterSection("players");
	for (int i=0; i<numberOfPlayer; ++i)
	{
		stream->readEnterSection(i);
		if(!players[i].load(stream, versionMinor))
		{
			stream->readLeaveSection(3);
			return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("teams");
	for (int i=0; i<numberOfTeam; ++i)
	{
		stream->readEnterSection(i);
		if(!teams[i].load(stream, versionMinor))
		{
			stream->readLeaveSection(3);
			return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();
	if (memcmp(signature,"GLO2",4) != 0)
		return false;

	return true;
}

Uint8 SessionInfo::getOrderType()
{
	return DATA_SESSION_INFO;
}

Uint8 *SessionInfo::getData(bool compressed)
{
	if (compressed)
	{
		int l=0;

		memcpy(l+data, SessionGame::getData(true), SessionGame::getDataLength(true));
		l+=SessionGame::getDataLength(true);

		for (int i=0; i<numberOfPlayer; ++i)
		{
			assert(players[i].getDataLength(true)==44);
			memcpy(l+data, players[i].getData(true), players[i].getDataLength(true));
			l+=players[i].getDataLength(true);
		}

		for (int i=0; i<numberOfTeam; ++i)
		{
			assert(teams[i].getDataLength()==16);
			// TODO: make a compressed version for team data.
			memcpy(l+data, teams[i].getData(), teams[i].getDataLength());
			l+=teams[i].getDataLength();
		}

		assert(l==getDataLength(true));
		return data;
	}
	else
	{
		int l=0;

		for (int i=0; i<32; ++i)
		{
			assert(players[i].getDataLength(false)==64);
			memcpy(l+data, players[i].getData(false), players[i].getDataLength(false));
			l+=players[i].getDataLength(false);
		}

		for (int i=0; i<32; ++i)
		{
			assert(teams[i].getDataLength()==16);
			memcpy(l+data, teams[i].getData(), teams[i].getDataLength());
			l+=teams[i].getDataLength();
		}

		memcpy(l+data, SessionGame::getData(), SessionGame::getDataLength());
		l+=SessionGame::getDataLength();

		assert(l==getDataLength());
		return data;
	}
}

bool SessionInfo::setData(const Uint8 *data, int dataLength, bool compressed)
{
	if (compressed)
	{
		int l=0;

		bool good=SessionGame::setData(l+data, SessionGame::getDataLength(true), true);
		l+=SessionGame::getDataLength(true);
		if (!good)
			return false;
		
		for (int i =0; i<numberOfPlayer; ++i)
		{
			players[i].setData(l+data, players[i].getDataLength(true), true);
			l+=players[i].getDataLength(true);
		}

		for (int i=0; i<numberOfTeam; ++i)
		{
			teams[i].setData(l+data, teams[i].getDataLength());
			teams[i].race.load();
			l+=teams[i].getDataLength();
		}

		if(l!=getDataLength(true))
			return false;

		return true;
	}
	else
	{
		int l=0;

		for (int i=0; i<32; ++i)
		{
			players[i].setData(l+data, players[i].getDataLength(false), false);
			l+=players[i].getDataLength(false);
		}

		for (int i=0; i<32; ++i)
		{
			teams[i].setData(l+data, teams[i].getDataLength());
			teams[i].race.load();
			l+=teams[i].getDataLength();
		}

		bool good=SessionGame::setData(l+data, SessionGame::getDataLength());
		l+=SessionGame::getDataLength();
		if (!good)
			return false;
			
		if(l!=getDataLength())
			return false;

		return true;
	}
}

int SessionInfo::getDataLength(bool compressed)
{
	if (compressed)
	{
		if (numberOfPlayer>0)
			assert(players[0].getDataLength(true)==44);
		if (numberOfTeam>0)
			assert(teams[0].getDataLength()==16);
		return SessionGame::getDataLength(true)+numberOfPlayer*44+numberOfTeam*16;
	}
	else
		return S_INFO_ONLY_DATA_SIZE+SessionGame::getDataLength();
}

Uint32 SessionInfo::checkSum()
{
	Uint32 cs=0;
	
	int l=Utilities::strmlen(mapName, sizeof(mapName));
	for (int i=0; i<l; ++i)
		cs^=(((Sint32)mapName[i])<<((i&7)<<2));
	
	for (int i=0; i<numberOfPlayer; ++i)
		cs^=players[i].checkSum();
	
	for (int i=0; i<numberOfTeam; ++i)
		cs^=teams[i].checkSum();
	
	cs^=SessionGame::checkSum();
	//fprintf(logFile, "SessionInfo::sc=%x.\n", cs);
	
	return cs;
}

bool SessionInfo::setLocal(int p)
{
	if (p>=numberOfPlayer)
		return false;
	players[p].type=BasePlayer::P_LOCAL;
	return true;
}

int SessionInfo::getTeamNumber(char playerName[BasePlayer::MAX_NAME_LENGTH], int team)
{
	for (int i=0; i<numberOfPlayer; i++)
		if (strncmp(players[i].name, playerName, BasePlayer::MAX_NAME_LENGTH)==0)
			team=players[i].teamNumber;
	return team;
}

int SessionInfo::getAITeamNumber(SessionInfo *currentSessionInfo, int team)
{
	int oldAICount=0;
	for (int i=0; i<numberOfPlayer; i++)
		if (players[i].type>=BasePlayer::P_AI)
			oldAICount++;
	if (oldAICount==0)
		return team;
	
	int newAICount=0;
	assert(currentSessionInfo);
	for (int i=0; i<currentSessionInfo->numberOfPlayer; i++)
		if (currentSessionInfo->players[i].type>=BasePlayer::P_AI)
			newAICount++;
	
	int targetCount=(newAICount%oldAICount);
	for (int i=0; i<numberOfPlayer; i++)
		if (players[i].type>=BasePlayer::P_AI)
			if (targetCount++==0)
			{
				team=players[i].teamNumber;
				break;
			}
	return team;
}
