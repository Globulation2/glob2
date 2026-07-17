// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "Campaign.h"
#include "TextStream.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <iostream>
#include <memory>

// Defined in map/io/MapHeader.cpp. Forward-declared here to avoid pulling
// MapHeader.h, which transitively includes Team.h / WinningConditions.h /
// Map.h — none of which Campaign.cpp itself uses.
std::string glob2NameToFilename(const std::string& dir,
                                const std::string& name,
                                const std::string& extension="");

using namespace GAGCore;

CampaignMapEntry::CampaignMapEntry()
{
	isLocked=false;
	completed=false;
}



CampaignMapEntry::CampaignMapEntry(const std::string& name, const std::string& fileName)
{
	mapName=name;
	mapFileName=fileName;
	isLocked=false;
	completed=false;
}



const std::string& CampaignMapEntry::getMapName()
{
	return mapName;
}



void CampaignMapEntry::setMapName(const std::string& aMapName)
{
	mapName=aMapName;
}



const std::string& CampaignMapEntry::getMapFileName()
{
	return mapFileName;
}



void CampaignMapEntry::setMapFileName(const std::string& fileName)
{
	mapFileName=fileName;
}



void CampaignMapEntry::lockMap()
{
	isLocked=true;
}



void CampaignMapEntry::unlockMap()
{
	isLocked=false;
}



bool CampaignMapEntry::isUnlocked()
{
	return !isLocked;
}



bool CampaignMapEntry::isCompleted()
{
	return completed;
}



void CampaignMapEntry::setCompleted(bool ncompleted)
{
	completed = ncompleted;
}



const std::string& CampaignMapEntry::getDescription() const
{
	return description;
}



void CampaignMapEntry::setDescription(const std::string& ndescription)
{
	description=ndescription;
}



std::vector<std::string>& CampaignMapEntry::getUnlockedByMaps()
{
	return unlockedBy;
}



bool CampaignMapEntry::load(InputStream* stream, Uint32 versionMinor)
{
	// Default the version-gated fields so old-format campaigns (pre-75 lack
	// description, pre-76 lack completed) produce fully-defined state instead
	// of retaining whatever the object held before load() ran.
	description = "";
	completed = false;
	stream->readEnterSection("CampaignMap");
	mapName = stream->readText("mapName");
	mapFileName = stream->readText("mapFileName");
	isLocked = stream->readUint8("isLocked");
	stream->readEnterSection("unlockedBy");
	Uint32 size=stream->readUint32("size");
	unlockedBy.resize(size);
	for(unsigned n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		unlockedBy[n]=stream->readText("unlockedBy");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if(versionMinor>=75)
	{
		description = stream->readText("description");
	}
	if(versionMinor>=76)
	{
		completed = stream->readUint8("completed");
	}
	stream->readLeaveSection();
	return true;
}



void CampaignMapEntry::save(OutputStream* stream)
{
	stream->writeEnterSection("CampaignMap");
	stream->writeText(mapName, "mapName");
	stream->writeText(mapFileName, "mapFileName");
	stream->writeUint8(isLocked, "isLocked");
	stream->writeEnterSection("unlockedBy");
	stream->writeUint32(unlockedBy.size(), "size");
	for(unsigned n=0; n<unlockedBy.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeText(unlockedBy[n], "unlockedBy");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeText(description, "description");
	stream->writeUint8(completed, "completed");
	stream->writeLeaveSection();
}



Campaign::Campaign()
{
	name="No Name";
	playerName="";
}



bool Campaign::load(const std::string& fileName)
{
	StreamBackend* backend = Toolkit::getFileManager()->openInputStreamBackend(fileName);
	// openInputStreamBackend never returns nullptr; missing files surface as
	// a backend wrapping a NULL FILE*, which fails isValid().
	if (!backend->isValid())
	{
		std::cerr << "Campaign::load(\"" << fileName << "\") : error, can't open file." << std::endl;
		delete backend;
		return false;
	}

	TextInputStream* stream = new TextInputStream(backend);
	Uint32 versionMinor = stream->readUint32("versionMinor");
	// Parser failure on empty/corrupt files leaves versionMinor at the
	// uninitialized-istream-extract default (0). Reject anything outside the
	// supported range so corrupt files don't silently produce blank campaigns.
	if (versionMinor < MINIMUM_VERSION_MINOR || versionMinor > VERSION_MINOR)
	{
		std::cerr << "Campaign::load(\"" << fileName << "\") : unsupported or corrupt versionMinor "
		          << versionMinor << std::endl;
		delete stream;
		delete backend;
		return false;
	}

	// Default the version-gated field so pre-83 campaigns (which lack the
	// campaign-level description) produce fully-defined state rather than
	// retaining a stale value from a previously loaded campaign.
	description = "";
	name = stream->readText("campaignName");
	playerName = stream->readText("playerName");
	stream->readEnterSection("maps");
	Uint32 size = stream->readUint32("mapNum");
	maps.resize(size);
	for (Uint32 n = 0; n < size; ++n)
	{
		stream->readEnterSection(n);
		maps[n].load(stream, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if (versionMinor >= 83)
		description = stream->readText("description");

	delete stream;
	delete backend;
	return true;
}



bool Campaign::save(bool isGameSave)
{
	std::string filename;
	if(!isGameSave)
		filename = glob2NameToFilename("campaigns", name.c_str(), "txt");
	else
		filename = glob2NameToFilename("games", name.c_str(), "txt");

	// openOutputStreamBackend never returns nullptr; on fopen failure it
	// returns a backend wrapping NULL, which fails isValid() and would crash
	// (assert(fp) in debug, raw fwrite(NULL) UB in release) on the first
	// write. Mirrors the pattern in Campaign::load and MapEdit::save.
	std::unique_ptr<StreamBackend> backend(
		Toolkit::getFileManager()->openOutputStreamBackend(filename));
	if (!backend->isValid())
	{
		std::cerr << "Campaign::save(\"" << filename << "\") : error, can't open file." << std::endl;
		return false;
	}

	// TextOutputStream takes ownership of the backend and frees it in its
	// destructor, so release() at the point of handoff. unique_ptr on the
	// stream itself protects against leak-on-throw from any future write.
	auto stream = std::make_unique<TextOutputStream>(backend.release());
	stream->writeUint32(VERSION_MINOR, "versionMinor");
	stream->writeText(name, "campaignName");
	stream->writeText(playerName, "playerName");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "mapNum");
	for(unsigned n=0; n<maps.size(); ++n)
	{
		stream->writeEnterSection(n);
		maps[n].save(stream.get());
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeText(description, "description");
	return true;
}



size_t Campaign::getMapCount() const
{
	return maps.size();
}



CampaignMapEntry& Campaign::getMap(unsigned n)
{
	return maps[n];
}



CampaignMapEntry* Campaign::findUnlockedMap(const std::string& mapName)
{
	for (size_t n = 0; n < maps.size(); ++n)
	{
		if (maps[n].getMapName() == mapName && maps[n].isUnlocked())
			return &maps[n];
	}
	return nullptr;
}



void Campaign::appendMap(CampaignMapEntry& map)
{
	maps.push_back(map);
}



void Campaign::removeMap(unsigned n)
{
	maps.erase(maps.begin()+n);
}



void Campaign::setCompleted(const std::string& map)
{
	for(unsigned n=0; n<maps.size(); ++n)
	{
		if(maps[n].getMapName() == map)
			maps[n].setCompleted(true);
		for(unsigned i=0; i<maps[n].getUnlockedByMaps().size(); ++i)
		{
			if(maps[n].getUnlockedByMaps()[i] == map)
			{
				maps[n].unlockMap();
				break;
			}
		}
	}
}



void Campaign::setName(const std::string& campaignName)
{
	name=campaignName;
}



const std::string& Campaign::getName() const
{
	return name;
}

void Campaign::setPlayerName(const std::string& playerName)
{
	this->playerName=playerName;
}

const std::string& Campaign::getPlayerName() const
{
	return playerName;
}


void Campaign::setDescription(const std::string& ndescription)
{
	description = ndescription;
}


const std::string& Campaign::getDescription() const
{
	return description;
}

