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

#ifndef __MAPHEADER_H
#define __MAPHEADER_H

#include "Stream.h"
#include "Version.h"
#include "BaseTeam.h"

///This is the map header. It is static with the map, and does not change from game to game if
///the user is playing on the same map. It holds small details about a map that aren't placed
///elsewhere.
class MapHeader
{
public:
	/// Gives default values to all entries
	MapHeader();
		
	/// Resets the MapHeader to a "blank" state with default values
	void reset();	

	/// Loads map header information from the stream
	bool load(GAGCore::InputStream *stream);
	
	/// Saves map header information to the stream.
	void save(GAGCore::OutputStream *stream) const;

	/// Returns the version major
	Sint32 getVersionMajor() const;
	
	/// Returns the version minor
	Sint32 getVersionMinor() const;
	
	/// Returns the number of teams
	Sint32 getNumberOfTeams() const;

	/// Sets the number of teams in the map
	void setNumberOfTeams(Sint32 teamNum);

	/// Returns the user-friendly name of the map	
	const std::string& getMapName() const;
	
	/// Returns the file name of the map. isCampaignMap is a special override when the game is loading campaign maps
	std::string getFileName(bool isCampaignMap=false) const;
	
	/// Sets the user-friendly name of the map
	void setMapName(const std::string& newMapName);

	/// Returns the offset of the Map info in the game save.
	/// This is used to quickly gain access to Map data, for
	/// the generation of map previews
	Uint32 getMapOffset() const;

	/// Sets the map offset. Should only be done during saving
	/// the game or map.
	/// Note that technically the MapHeader is the first thing
	/// written to the file. This means that it has to be 
	/// *overwritten* after the offset has been found.
	void setMapOffset(Uint32 mapOffset);

	/// Returns the base team for team n. n must be between 0 and 31
	BaseTeam& getBaseTeam(const int n);
	const  BaseTeam& getBaseTeam(const int n) const;

	/// Returns true if this header represents a saved game, otherwise it
	/// is a map
	bool getIsSavedGame() const;
	
	/// Sets whether or no this header represents a saved game
	void setIsSavedGame(bool isSavedGame);

	/// Sets the complete game checksum
	void setGameSHA1(Uint8 SHA1sum[20]);
	
	/// Returns the complete game checksum
	Uint8* getGameSHA1();
	
	/// Returns the complete game checksum
	void resetGameSHA1();

	/// Returns a checksum of the map header information
	Uint32 checkSum() const;
	
	///Comparison does *not* count file version numbers
	bool operator!=(const MapHeader& rhs) const;
	bool operator==(const MapHeader& rhs) const;
private:
	/// Major map version. Changes only with structural modification
	Sint32 versionMajor;
	/// Minor map version. Changes each time something has been changed in serializations
	Sint32 versionMinor;
	/// The number of teams on the map
	Sint32 numberOfTeams;
	/// The offset of Map in the save. This is set during saving,
	/// and is used to generate Map previews without loading
	/// the complete file.
	Uint32 mapOffset;
	
	/// The teams in the map. BaseTeam is used to allow access to information like team numbers and
	/// team colors without loading the entire game.
	BaseTeam teams[32];
	
	/// If this is true, this map header represents a saved game, rather than a new map
	bool isSavedGame;
	
	/// Set to the complete files SHA1
	Uint8 SHA1[20];

	std::string mapName;
};


//! extract the user-visible name from a glob2 map filename, return empty string if filename is an invalid glob2 map
std::string glob2FilenameToName(const std::string& filename);
//! create the filename from the directory, end user-visible name and extension. directory and extension must be given without the / and the .
std::string glob2NameToFilename(const std::string& dir, const std::string& name, const std::string& extension="");

#endif
