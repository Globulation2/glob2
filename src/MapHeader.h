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

#ifndef __MAPHEADER_H
#define __MAPHEADER_H

#include "Session.h"
#include "Stream.h"
#include "Version.h"

///This is the map header. It is static with the map, and does not change from game to game if
///the user is playing on the same map. It holds small details about a map that aren't placed
///elsewhere.
class MapHeader
{
public:
	///Gives default values to all entries
	MapHeader();
		
	///Loads map header information from the stream
	bool load(GAGCore::InputStream *stream);
	
	///Saves map header information to the stream
	void save(GAGCore::OutputStream *stream);

	///Returns the version major
	Sint32 getVersionMajor() const;
	
	///Returns the version minor
	Sint32 getVersionMinor() const;
	
	///Returns the number of teams
	Sint32 getNumberOfTeams() const;

	///Sets the number of teams in the map
	void setNumberOfTeams(Sint32 teamNum);

	///Returns the user-friendly name of the map	
	const std::string& getMapName() const;
	
	//! Set the user-friendly name of the map
	void setMapName(const std::string& newMapName);

	///Returns a checksum of the map header information
	Uint32 checkSum() const;

	///This loads information from a provided SessionGame instance.
	///This is used purely for backwards compatibility
	void loadFromSessionGame(SessionGame* session);
private:
	//! Major map version. Change only with structural modification
	Sint32 versionMajor;
	//! Minor map version. Change each time something has been changed in serialized version.
	Sint32 versionMinor;

	///The number of teams on the map
	Sint32 numberOfTeams;

	std::string mapName;
};

#endif
