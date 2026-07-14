// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "BinaryStream.h"
#include "FileManager.h"
#include <map>
#include "Stream.h"
#include <string>
#include "Toolkit.h"
#include "Version.h"
#include "YOGServerBannedIPListManager.h"
#include "YOGConsts.h"

using namespace GAGCore;

YOGServerBannedIPListManager::YOGServerBannedIPListManager()
{
	loadBannedIPList();
	saveCountdown=0;
        modified=false;
}



void YOGServerBannedIPListManager::update()
{
	if(saveCountdown == 0)
	{
		if(modified)
		{
			saveBannedIPList();
			modified=false;
		}
		saveCountdown = 300;
	}
	else
	{
		saveCountdown -= 1;
	}
	
	boost::posix_time::ptime current_time = boost::posix_time::second_clock::local_time();
	for(std::map<std::string, boost::posix_time::ptime>::iterator i=bannedIPs.begin(); i!=bannedIPs.end();)
	{
		if(i->second < current_time)
		{
			modified=true;
			bannedIPs.erase(i++);
		}
		else
		{
			++i;
		}
	}
}



void YOGServerBannedIPListManager::addBannedIP(const std::string& bannedIP, boost::posix_time::ptime unban_time)
{
	modified=true;
	bannedIPs[bannedIP] = unban_time;
}



bool YOGServerBannedIPListManager::isIPBanned(const std::string& bannedIP)
{
	if(bannedIPs.find(bannedIP) != bannedIPs.end())
	{
		return true;
	}
	return false;
}



void YOGServerBannedIPListManager::saveBannedIPList()
{
	OutputStream* stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(YOG_SERVER_FOLDER+"bannedips"));
	stream->writeUint32(VERSION_MINOR, "version");
	stream->writeUint32(bannedIPs.size(), "size");
	for(std::map<std::string, boost::posix_time::ptime>::iterator i = bannedIPs.begin(); i!=bannedIPs.end(); ++i)
	{
		stream->writeText(i->first, "ip");
		std::stringstream time;
		time<<i->second;
		stream->writeText(time.str(), "time");
	}
	delete stream;
}



void YOGServerBannedIPListManager::loadBannedIPList()
{
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(YOG_SERVER_FOLDER+"bannedips"));
	if(!stream->isEndOfStream())
	{
		stream->readUint32("version");
		Uint32 size = stream->readUint32("size");
		for(unsigned i=0; i<size; ++i)
		{
			std::string ip = stream->readText("ip");
			std::string b = stream->readText("time");
			
			std::stringstream time;
			boost::posix_time::ptime unban_time;
			time<<b;
			time>>unban_time;
			bannedIPs[ip]=unban_time;
		}
	}
	delete stream;
}

