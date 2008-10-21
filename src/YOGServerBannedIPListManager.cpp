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


#include "BinaryStream.h"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "FileManager.h"
#include <map>
#include "SDL_net.h"
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
		Uint32 dataVersionMinor = stream->readUint32("version");
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

