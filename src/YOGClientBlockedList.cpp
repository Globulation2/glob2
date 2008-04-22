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

#include "FileManager.h"
#include "Stream.h"
#include "TextStream.h"
#include "Toolkit.h"
#include "Version.h"
#include "YOGClientBlockedList.h"

using namespace GAGCore;

YOGClientBlockedList::YOGClientBlockedList()
{
	load();
}



void YOGClientBlockedList::load()
{
	StreamBackend* backend = Toolkit::getFileManager()->openInputStreamBackend("blocked.txt");
	if(!backend->isEndOfStream())
	{
		InputStream* stream = new TextInputStream(backend);
		Uint32 versionMinor = stream->readUint32("version");
		stream->readEnterSection("blockedPlayers");
		Uint32 size = stream->readUint32("size");
		for(unsigned i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			std::string name = stream->readText("username");
			blockedPlayers.insert(name);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		delete stream;
	}
	else
	{
		delete backend;
	}
}


	
void YOGClientBlockedList::save()
{
	OutputStream* stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("blocked.txt"));
	stream->writeUint32(VERSION_MINOR, "version");
	Uint32 n = 0;
	stream->writeEnterSection("blockedPlayers");
	stream->writeUint32(blockedPlayers.size(), "size");
	for(std::set<std::string>::iterator i = blockedPlayers.begin(); i!=blockedPlayers.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeText(*i, "username");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	delete stream;
}


	
void YOGClientBlockedList::addBlockedPlayer(const std::string& name)
{
	blockedPlayers.insert(name);
}


	
bool YOGClientBlockedList::isPlayerBlocked(const std::string& name)
{
	return blockedPlayers.find(name) != blockedPlayers.end();
}


	
void YOGClientBlockedList::removeBlockedPlayer(const std::string& name)
{
	std::set<std::string>::iterator i = blockedPlayers.find(name);
	if(i != blockedPlayers.end())
		blockedPlayers.erase(i);
}


	
const std::set<std::string>& YOGClientBlockedList::getBlockedPlayers() const
{
	return blockedPlayers;
}

