/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "MapHeader.h"
#include "Game.h"
#include <algorithm>
#include "FileManager.h"

MapHeader::MapHeader()
{
	reset();
}



void MapHeader::reset()
{
	versionMajor = VERSION_MAJOR;
	versionMinor = VERSION_MINOR;
	numberOfTeams = 0;
	mapName = "";
	mapOffset = 0;
	isSavedGame=false;
	resetGameSHA1();
}



bool MapHeader::load(GAGCore::InputStream *stream)
{
	///First, check if its an old format map
	Uint32 pos = stream->getPosition();
	char* signature[4];
	stream->read(signature, 4, "signature");
	if(memcmp(signature, "SEGb",4) == 0)
	{
		return false;
	}
	stream->seekFromStart(pos);

	stream->readEnterSection("MapHeader");
	mapName = stream->readText("mapName");
	versionMajor = stream->readSint32("versionMajor");
	versionMinor = stream->readSint32("versionMinor");

	numberOfTeams = stream->readSint32("numberOfTeams");
	mapOffset = stream->readUint32("mapOffset");
	isSavedGame = stream->readUint8("isSavedGame");
	if(versionMinor==67)
		stream->readUint32("checksum");
	
	if(versionMinor>=68)
	{
		stream->read(SHA1, 20, "SHA1");
	}
	
	stream->readEnterSection("teams");
	for(int i=0; i<numberOfTeams; ++i)
	{
		stream->readEnterSection(i);
		teams[i].load(stream, versionMinor);
		stream->readLeaveSection(i);
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}


	
void MapHeader::save(GAGCore::OutputStream *stream)
{
	///Update version major and minor
	versionMajor = VERSION_MAJOR;
	versionMinor = VERSION_MINOR;
	stream->writeEnterSection("MapHeader");
	stream->writeText(mapName, "mapName");
	stream->writeSint32(versionMajor, "versionMajor");
	stream->writeSint32(versionMinor, "versionMinor");
	stream->writeSint32(numberOfTeams, "numberOfTeams");
	stream->writeUint32(mapOffset, "mapOffset");
	stream->writeUint8(isSavedGame, "isSavedGame");
	stream->write(SHA1, 20, "SHA1");
	stream->writeEnterSection("teams");
	for(int i=0; i<numberOfTeams; ++i)
	{
		stream->writeEnterSection(i);
		teams[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



Sint32 MapHeader::getVersionMajor() const
{
	return versionMajor;
}



Sint32 MapHeader::getVersionMinor() const
{
	return versionMinor;
}



Sint32 MapHeader::getNumberOfTeams() const
{
	return numberOfTeams;
}



void MapHeader::setNumberOfTeams(Sint32 teamNum)
{
	numberOfTeams = teamNum;
}



const std::string& MapHeader::getMapName() const
{
	return mapName;
}



std::string MapHeader::getFileName(bool isCampaignMap) const
{
	if(isCampaignMap)
		return glob2NameToFilename("campaigns", mapName, "map");
	else if (!isSavedGame)
		return glob2NameToFilename("maps", mapName, "map");
	else
		return glob2NameToFilename("games", mapName, "game");
}



void MapHeader::setMapName(const std::string& newMapName)
{
	mapName = newMapName;
}



Uint32 MapHeader::getMapOffset() const
{
	return mapOffset;
}



void MapHeader::setMapOffset(Uint32 newMapOffset)
{
	mapOffset = newMapOffset;
}



BaseTeam& MapHeader::getBaseTeam(const int n)
{
	assert(n>=0 && n<32);
	return teams[n];
}



const BaseTeam& MapHeader::getBaseTeam(const int n) const
{
	assert(n>=0 && n<32);
	return teams[n];
}



bool MapHeader::getIsSavedGame() const
{
	return isSavedGame;
}



void MapHeader::setIsSavedGame(bool newIsSavedGame)
{
	isSavedGame = newIsSavedGame;
}



void MapHeader::setGameSHA1(Uint8 SHA1sum[20])
{
	for(int i=0; i<20; ++i)
		SHA1[i] = SHA1sum[i];
}



Uint8* MapHeader::getGameSHA1()
{
	return SHA1;
}



void MapHeader::resetGameSHA1()
{
	for(int i=0; i<20; ++i)
		SHA1[i] = 0;
}



Uint32 MapHeader::checkSum() const
{
	Sint32 cs = 0;
	cs^=versionMajor;
	cs^=versionMinor;
	cs^=numberOfTeams;
	cs=(cs<<31)|(cs>>1);
	return cs;
}



bool MapHeader::operator!=(const MapHeader& rhs) const
{
	if( rhs.numberOfTeams != numberOfTeams ||
		rhs.mapOffset != mapOffset ||
		rhs.isSavedGame != isSavedGame ||
		rhs.mapName != mapName ||
		!std::equal(SHA1, SHA1+20, rhs.SHA1))
		return true;
	return false;
}



std::string glob2FilenameToName(const std::string& filename)
{
	std::string mapName;
	if(filename.find(".game")!=std::string::npos)
		mapName=filename.substr(filename.find("/")+1, filename.size()-6-filename.find("/"));
	else
		mapName=filename.substr(filename.find("/")+1, filename.size()-5-filename.find("/"));
	size_t pos = mapName.find("_");
	while(pos != std::string::npos)
	{
		mapName.replace(pos, 1, " ");
		pos = mapName.find("_");
	}
	return mapName;
}

template<typename It, typename T>
class contains: std::unary_function<T, bool>
{
public:
	contains(const It from, const It to) : from(from), to(to) {}
	bool operator()(T d) { return (std::find(from, to, d) != to); }
private:
	const It from;
	const It to;
};

std::string glob2NameToFilename(const std::string& dir, const std::string& name, const std::string& extension)
{
	const char* pattern = " \t";
	const char* endPattern = strchr(pattern, '\0');
	std::string fileName = name;
	std::replace_if(fileName.begin(), fileName.end(), contains<const char*, char>(pattern, endPattern), '_');
	std::string fullFileName = dir;
	fullFileName += DIR_SEPARATOR + fileName;
	if (extension != "" && extension != "\0")
	{
		fullFileName += '.';
		fullFileName += extension;
	}
	return fullFileName;
}

