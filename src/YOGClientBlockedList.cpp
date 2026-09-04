// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "FileManager.h"
#include "Stream.h"
#include "TextStream.h"
#include "Toolkit.h"
#include "Version.h"
#include "YOGClientBlockedList.h"

using namespace GAGCore;

YOGClientBlockedList::YOGClientBlockedList(const std::string& username)
	: username(username)
{
	load();
}



void YOGClientBlockedList::load()
{
	StreamBackend* backend = Toolkit::getFileManager()->openInputStreamBackend("blocked-"+username+".txt");
	if(!backend->isEndOfStream())
	{
		InputStream* stream = new TextInputStream(backend);
		stream->readUint32("version");
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
	OutputStream* stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("blocked-"+username+".txt"));
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

