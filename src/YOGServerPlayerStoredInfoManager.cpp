// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerPlayerStoredInfoManager.h"

#include "Stream.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "Version.h"
#include "YOGServer.h"

using namespace GAGCore;

YOGServerPlayerStoredInfoManager::YOGServerPlayerStoredInfoManager(YOGServer* server)
	: server(server)
{
	loadPlayerInfos();
	saveCountdown=300;
	modified=false;
}



void YOGServerPlayerStoredInfoManager::update()
{
	if(saveCountdown == 0)
	{
		if(modified)
		{
			savePlayerInfos();
			modified=false;
		}
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



const YOGPlayerStoredInfo& YOGServerPlayerStoredInfoManager::getPlayerStoredInfo(const std::string& username)
{
	return playerInfos[username];
}



void YOGServerPlayerStoredInfoManager::setPlayerStoredInfo(const std::string& username, const YOGPlayerStoredInfo& info)
{
	modified=true;
	playerInfos[username] = info;
	server->setPlayerStoredInfo(username, info);
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
	OutputStream* stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(YOG_SERVER_FOLDER+"playerinfo"));
	stream->writeUint32(VERSION_MINOR, "version");
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
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(YOG_SERVER_FOLDER+"playerinfo"));
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


