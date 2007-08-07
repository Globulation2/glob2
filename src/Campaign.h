/*
  Copyright (C) 2006 Bradley Arsenault

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

#ifndef CAMPAIGN_H
#define CAMPAIGN_H

#include <string>
#include <vector>
#include "Stream.h"

///An entry for a single map in the campaign
class CampaignMapEntry
{
public:
	CampaignMapEntry();
	CampaignMapEntry(const std::string& name, const std::string& fileName);
	bool load(GAGCore::InputStream* stream, Uint32 versionMinor);
	void save(GAGCore::OutputStream* stream);
	///Returns the name of the map as seen by the user
	const std::string& getMapName();
	///Sets the name of the map as seen by the user
	void setMapName(const std::string& mapName);
	///Returns the filename of the map
	const std::string& getMapFileName();
	///Sets the filename
	void setMapFileName(const std::string& fileName);
	///Locks the map in the campaign
	void lockMap();
	///Unlocks the map in the campaign
	void unlockMap();
	///Returns true if the map is unlocked
	bool isUnlocked();

	///Gets the vector holding the list of unlocked by maps
	std::vector<std::string>& getUnlockedByMaps();

private:
	std::string mapName;
	std::string mapFileName;
	bool isLocked;
	std::vector<std::string> unlockedBy;
};


///This campaign class handles both new campaigns and campaign game saves
class Campaign
{
public:
	///Standard campaign constructor
	Campaign();

	///Loads the campaign with the provided name
	bool load(const std::string& fileName);
	///Save the campaign
	void save(bool isGameSave=false);


	///Gets the number of maps in this campaign
	size_t getMapCount() const;
	///Returns the name of the map n
	CampaignMapEntry& getMap(unsigned n);
	///Appends a map to the list of maps
	void appendMap(CampaignMapEntry& map);
	///Removes map n
	void removeMap(unsigned n);

	///Unlocks all the maps that are unlocked by this "played" map
	void unlockAllFrom(const std::string& map);

	///Sets the name
	void setName(const std::string& campaignName);
	///Retrieves the name
	const std::string& getName() const;

	///Sets the name of the player. This only qualifies for campaign game saves
	void setPlayerName(const std::string& playerName);
	///Retrieves the player name
	const std::string& getPlayerName() const;

private:
	std::vector<CampaignMapEntry> maps;
	std::string name;
	std::string playerName;
};


#endif
