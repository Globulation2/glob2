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


MapHeader::MapHeader()
{
	versionMajor = VERSION_MAJOR;
	versionMinor = VERSION_MINOR;
	numberOfTeams = 0;
	mapName = "";
}



bool MapHeader::load(GAGCore::InputStream *stream)
{
	stream->readEnterSection("MapHeader");
	mapName = stream->readText("mapName");
	versionMajor = stream->readSint32("versionMajor");
	versionMinor = stream->readSint32("versionMinor");
	numberOfTeams = stream->readSint32("numberOfTeams");
	stream->readLeaveSection();
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
	


void MapHeader::setMapName(const std::string& newMapName)
{
	mapName = newMapName;
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



void MapHeader::loadFromSessionGame(SessionGame* session)
{
	versionMinor = session->versionMinor;
	versionMajor = session->versionMajor;
	numberOfTeams = session->numberOfTeam;
	mapName = session->getMapName();
}



