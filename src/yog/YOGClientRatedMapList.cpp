// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "FileManager.h"
#include "Stream.h"
#include "TextStream.h"
#include "Toolkit.h"
#include "Version.h"
#include "YOGClientRatedMapList.h"

using namespace GAGCore;

YOGClientRatedMapList::YOGClientRatedMapList(const std::string& username)
	: username(username)
{
	load();
}



void YOGClientRatedMapList::addRatedMap(const std::string& mapname)
{
	maps.insert(mapname);
	save();
}



bool YOGClientRatedMapList::isMapRated(const std::string& mapname)
{
	return maps.find(mapname) != maps.end();
}



void YOGClientRatedMapList::save()
{
	OutputStream* stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("rated-"+username+".txt"));
	stream->writeUint32(VERSION_MINOR, "version");
	Uint32 n = 0;
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "size");
	for(std::set<std::string>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeText(*i, "mapname");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	delete stream;
}



void YOGClientRatedMapList::load()
{
	StreamBackend* backend = Toolkit::getFileManager()->openInputStreamBackend("rated-"+username+".txt");
	if(!backend->isEndOfStream())
	{
		InputStream* stream = new TextInputStream(backend);
		stream->readUint32("version");
		stream->readEnterSection("maps");
		Uint32 size = stream->readUint32("size");
		for(unsigned i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			std::string name = stream->readText("mapname");
			maps.insert(name);
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


