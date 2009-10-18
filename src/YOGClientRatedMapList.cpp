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


