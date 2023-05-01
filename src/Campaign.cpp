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


#include "Campaign.h"
#include "TextStream.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <iostream>
#include "Game.h"
#include "GlobalContainer.h"

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



void CampaignMapEntry::setCompleted(bool completed)
{
	this->completed = completed;
}



const std::string& CampaignMapEntry::getDescription() const
{
	return description;
}



void CampaignMapEntry::setDescription(const std::string& description)
{
	this->description=description;
}



std::vector<std::string>& CampaignMapEntry::getUnlockedByMaps()
{
	return unlockedBy;
}



bool CampaignMapEntry::load(InputStream* stream, Uint32 versionMinor)
{
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
	if (backend->isEndOfStream())
	{
		//std::cerr << "Campaign::load(\"" << fileName << "\") : error, can't open file." << std::endl;
		delete backend;
		return false;
	}
	else
	{
		TextInputStream* stream = new TextInputStream(backend);
		Uint32 versionMinor = stream->readUint32("versionMinor");
		name = stream->readText("campaignName");
		playerName = stream->readText("playerName");
		stream->readEnterSection("maps");
		Uint32 size=stream->readUint32("mapNum");
		maps.resize(size);
		for(Uint32 n=0; n<size; ++n)
		{
			stream->readEnterSection(n);
			maps[n].load(stream, versionMinor);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		if(versionMinor >= 83)
		{
			description = stream->readText("description");	
		}
		delete stream;
		delete backend;
		return true;
	}
}



void Campaign::save(bool isGameSave)
{
	std::string filename;
	if(!isGameSave)
		filename = glob2NameToFilename("campaigns", name.c_str(), "txt");
	else
		filename = glob2NameToFilename("games", name.c_str(), "txt");
	TextOutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
	stream->writeUint32(VERSION_MINOR, "versionMinor");
	stream->writeText(name, "campaignName");
	stream->writeText(playerName, "playerName");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "mapNum");
	for(unsigned n=0; n<maps.size(); ++n)
	{
		stream->writeEnterSection(n);
		maps[n].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeText(description, "description");
	delete stream;
}



size_t Campaign::getMapCount() const
{
	return maps.size();
}



CampaignMapEntry& Campaign::getMap(unsigned n)
{
	return maps[n];
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


void Campaign::setDescription(const std::string& description)
{
	this->description = description;
}


const std::string& Campaign::getDescription() const
{
	return description;
}

