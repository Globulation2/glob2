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
#include "NetMessage.h"
#include "Stream.h"
#include "TextStream.h"
#include "Toolkit.h"
#include "YOGServerMapDatabank.h"

using namespace GAGCore;

YOGServerMapDatabank::YOGServerMapDatabank()
{

}



void YOGServerMapDatabank::addMap(const YOGDownloadableMapInfo& map)
{
	maps.push_back(map);
}



void YOGServerMapDatabank::sendMapListToPlayer(boost::shared_ptr<YOGServerPlayer> player)
{
	//Send in 5 map chunks
	int c=0;
	for(int i=0; i<maps.size(); ++i)
	{
		if(c == 5)
		{
			boost::shared_ptr<NetDownloadableMapInfos> infos(new NetDownloadableMapInfos(std::vector<YOGDownloadableMapInfo>(maps.begin() + (i-5), maps.begin() + i)));
			player->sendMessage(infos);
			c=0;
		}
		else
		{
			c+=1;
		}
	}
	if(c!=0)
	{
		boost::shared_ptr<NetDownloadableMapInfos> infos(new NetDownloadableMapInfos(std::vector<YOGDownloadableMapInfo>((maps.end()-c), maps.end())));
		player->sendMessage(infos);
	}
}


void YOGServerMapDatabank::load()
{
	InputStream* stream = new TextInputStream(Toolkit::getFileManager()->openInputStreamBackend("mapdatabank.txt"));
	if(!stream->isEndOfStream())
	{
		Uint32 versionMinor = stream->readUint32("version");
		stream->readEnterSection("maps");
		Uint32 size = stream->readUint32("size");
		for(unsigned i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			YOGDownloadableMapInfo info;
			info.decodeData(stream, versionMinor);
			maps.push_back(info);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	delete stream;
}


void YOGServerMapDatabank::save()
{
	OutputStream* stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("mapdatabank.txt"));
	stream->writeUint32(VERSION_MINOR, "version");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "size");
	for(int i=0; i<maps.size(); ++i)
	{
		stream->writeEnterSection(i);
		maps[i].encodeData(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	delete stream;
}

