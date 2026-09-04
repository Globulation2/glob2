// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#ifndef CAMPAIGN_H
#define CAMPAIGN_H

#include <string>
#include <vector>

#include "Types.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

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
	///Returns true if the map is completed
	bool isCompleted();
	///Sets whether the map has been completed
	void setCompleted(bool completed);
	
	///Returns the description of this map in the campaign
	const std::string& getDescription() const;
	///Sets the description of this map in the campaign
	void setDescription(const std::string& description);

	///Gets the vector holding the list of unlocked by maps
	std::vector<std::string>& getUnlockedByMaps();

private:
	std::string mapName;
	std::string mapFileName;
	bool isLocked;
	bool completed;
	std::vector<std::string> unlockedBy;
	std::string description;
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

	///Sets a particular map as completed and unlocks all the maps that are unlocked by this "played" map
	void setCompleted(const std::string& map);

	///Sets the name
	void setName(const std::string& campaignName);
	///Retrieves the name
	const std::string& getName() const;

	///Sets the name of the player. This only qualifies for campaign game saves
	void setPlayerName(const std::string& playerName);
	///Retrieves the player name
	const std::string& getPlayerName() const;
	
	///Sets the description of the campaign
	void setDescription(const std::string& description);
	///Retrieves the description
	const std::string& getDescription() const;

private:
	std::vector<CampaignMapEntry> maps;
	std::string name;
	std::string playerName;
	std::string description;
};


#endif
