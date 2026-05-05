// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerGameLog.h"
#include <sstream>
#include "Stream.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "Version.h"

using namespace GAGCore;

YOGServerGameLog::YOGServerGameLog()
{
	boost::posix_time::ptime local = boost::posix_time::second_clock::local_time();
	hour =  local - boost::posix_time::seconds(local.time_of_day().seconds()%3600);
	flushTime = local + boost::posix_time::minutes(5);
	modified=false;
	load();
}



void YOGServerGameLog::addGameResults(YOGGameResults results)
{
	games.push_back(results);
	modified=true;
}



void YOGServerGameLog::update()
{
	boost::posix_time::ptime local = boost::posix_time::second_clock::local_time();
	boost::posix_time::ptime new_hour =  local - boost::posix_time::seconds(local.time_of_day().total_seconds()%3600);
	if(new_hour != hour)
	{
		if(modified)
		{
			save();
			modified=false;
		}
		hour = new_hour;
		load();
	}
	
	if(local > flushTime)
	{
		if(modified)
		{
			save();
			modified=false;
		}
		flushTime = local + boost::posix_time::minutes(5);
	}
}



void YOGServerGameLog::save()
{
	std::stringstream s;
	s<<YOG_SERVER_FOLDER+"gamelog/gamelog";
	s<<hour;
	s<<".log";
	OutputStream* stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(s.str()));
	
	stream->writeUint32(VERSION_MINOR, "version");
	stream->writeUint32(games.size(), "size");
	for(std::vector<YOGGameResults>::iterator i = games.begin(); i!=games.end(); ++i)
	{
		i->encodeData(stream);
	}
	delete stream;
}



void YOGServerGameLog::load()
{
	games.clear();
	std::stringstream s;
	s<<YOG_SERVER_FOLDER+"gamelog/gamelog";
	s<<hour;
	s<<".log";
	StreamBackend* backend = Toolkit::getFileManager()->openInputStreamBackend(s.str());
	if(!backend->isEndOfStream())
	{
		InputStream* stream = new BinaryInputStream(backend);
		Uint32 version = stream->readUint32("version");
		Uint32 size = stream->readUint32("size");
		games.resize(size);
		for(std::vector<YOGGameResults>::iterator i = games.begin(); i!=games.end(); ++i)
		{
			i->decodeData(stream, version);
		}
		delete stream;
	}
	else
	{
		delete backend;
	}
}

