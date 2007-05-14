/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "MapHeader.h"
#include "Game.h"

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
}



bool MapHeader::load(GAGCore::InputStream *stream)
{
	stream->readEnterSection("MapHeader");
	mapName = stream->readText("mapName");
	versionMajor = stream->readSint32("versionMajor");
	versionMinor = stream->readSint32("versionMinor");
	numberOfTeams = stream->readSint32("numberOfTeams");
	mapOffset = stream->readUint32("mapOffset");
	isSavedGame = stream->readUint32("isSavedGame");
	stream->readEnterSection("teams");
	for(int i=0; i<32; ++i)
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
	stream->writeEnterSection("teams");
	for(int i=0; i<32; ++i)
	{
		stream->writeEnterSection(i);
		teams[i].save(stream);
		stream->writeLeaveSection(i);
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



std::string MapHeader::getFileName() const
{
	if (!isSavedGame)
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


Uint32 MapHeader::checkSum() const
{
	Sint32 cs = 0;
	cs^=versionMajor;
	cs^=versionMinor;
	cs^=numberOfTeams;
	cs=(cs<<31)|(cs>>1);
	return cs;
}



