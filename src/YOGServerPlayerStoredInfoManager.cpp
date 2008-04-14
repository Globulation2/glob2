/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "YOGServerPlayerStoredInfoManager.h"

#include "Stream.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "Version.h"

using namespace GAGCore;

YOGServerPlayerStoredInfoManager::YOGServerPlayerStoredInfoManager()
{
	loadPlayerInfos();
	saveCountdown=300;
	modified=false;
}



void YOGServerPlayerStoredInfoManager::update()
{
	if(saveCountdown == 0)
	{
		savePlayerInfos();
		saveCountdown = 300;
	}
	else
	{
		saveCountdown -= 1;
	}
}



void YOGServerPlayerStoredInfoManager::insureStoredInfoExists(const std::string& username)
{
	if(playerInfos.find(username) == playerInfos.end())
	{
		playerInfos.insert(std::make_pair(username, YOGPlayerStoredInfo()));
		modified=true;
	}
}



bool YOGServerPlayerStoredInfoManager::doesStoredInfoExist(const std::string& username)
{
	if(playerInfos.find(username) == playerInfos.end())
	{
		return false;
	}
	return true;
}



YOGPlayerStoredInfo& YOGServerPlayerStoredInfoManager::getPlayerStoredInfo(const std::string& username)
{
	modified=true;
	return playerInfos[username];
}



std::list<std::string> YOGServerPlayerStoredInfoManager::getBannedPlayers()
{
	std::list<std::string> players;
	for(std::map<std::string, YOGPlayerStoredInfo>::iterator i = playerInfos.begin(); i!=playerInfos.end(); ++i)
	{
		if(i->second.isBanned())
			players.push_back(i->first);
	}
	return players;
}



void YOGServerPlayerStoredInfoManager::savePlayerInfos()
{
	OutputStream* stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("playerinfo"));
	stream->writeUint32(NET_DATA_VERSION, "version");
	stream->writeUint32(playerInfos.size(), "size");
	for(std::map<std::string, YOGPlayerStoredInfo>::iterator i = playerInfos.begin(); i!=playerInfos.end(); ++i)
	{
		stream->writeText(i->first, "username");
		i->second.encodeData(stream);
	}
	delete stream;
}



void YOGServerPlayerStoredInfoManager::loadPlayerInfos()
{
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend("playerinfo"));
	if(!stream->isEndOfStream())
	{
		Uint32 dataVersionMinor = stream->readUint32("version");
		Uint32 size = stream->readUint32("size");
		for(unsigned i=0; i<size; ++i)
		{
			std::string name = stream->readText("username");
			YOGPlayerStoredInfo info;
			info.decodeData(stream, dataVersionMinor);
			playerInfos.insert(std::make_pair(name, info));
		}
	}
	delete stream;
}


