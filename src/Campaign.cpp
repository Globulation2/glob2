/*
  Copyright (C) 2006 Bradley Arsenault

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
	for(int n=0; n<size; ++n)
	{
		stream->readEnterSection(n);
		unlockedBy[n]=stream->readText("unlockedBy");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
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
	for(int n=0; n<unlockedBy.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeText(unlockedBy[n], "unlockedBy");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
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
		std::cerr << "Campaign::load(\"" << name << "\") : error, can't open file." << std::endl;
		delete backend;
		return false;
	}
	else
	{
		TextInputStream* stream = new TextInputStream(backend);
		name = stream->readText("campaignName");
		playerName = stream->readText("playerName");
		stream->readEnterSection("maps");
		Uint32 size=stream->readUint32("mapNum");
		maps.resize(size);
		for(Uint32 n=0; n<size; ++n)
		{
			stream->readEnterSection(n);
			maps[n].load(stream, VERSION_MINOR);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		delete stream;
		return true;
	}
}



void Campaign::save(bool isGameSave)
{
	std::string filename;
	if(!isGameSave)
		filename = glob2NameToFilename("campaigns", name.c_str(), "txt");
	else
		filename = glob2NameToFilename("games", (name+"-"+playerName).c_str(), "txt");
	TextOutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
	stream->writeText(name, "campaignName");
	stream->writeText(playerName, "playerName");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "mapNum");
	for(int n=0; n<maps.size(); ++n)
	{
		stream->writeEnterSection(n);
		maps[n].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
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



void Campaign::unlockAllFrom(const std::string& map)
{
	for(int n=0; n<maps.size(); ++n)
	{
		for(int i=0; i<maps[n].getUnlockedByMaps().size(); ++i)
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

