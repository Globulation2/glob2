// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGGameResults.h"

#include <iostream>
#include "Stream.h"

YOGGameResults::YOGGameResults()
{

}


void YOGGameResults::setGameResultState(const std::string& player, YOGGameResult result)
{
	results[player] = result;
}


YOGGameResult YOGGameResults::getGameResultState(const std::string& player)
{
	if(results.find(player)!=results.end())
	{
		return results[player];
	}
	return YOGGameResultUnknown;
}



void YOGGameResults::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGGameResults");
	stream->writeUint32(results.size(), "size");
	Uint32 n = 0;
	for(std::map<std::string, YOGGameResult>::const_iterator i=results.begin(); i!=results.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeText(i->first, "name");
		stream->writeUint8(i->second, "result");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
}



void YOGGameResults::decodeData(GAGCore::InputStream* stream, Uint32 netDataVersion)
{
	stream->readEnterSection("YOGGameResults");
	Uint32 size = stream->readUint32("size");
	for(unsigned int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		YOGGameResult result = static_cast<YOGGameResult>(stream->readUint8("result"));
		results[name] = result;
		stream->readLeaveSection();
	}

	stream->readLeaveSection();
}



bool YOGGameResults::operator==(const YOGGameResults& rhs) const
{
	if(results == rhs.results)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

	
	
bool YOGGameResults::operator!=(const YOGGameResults& rhs) const
{
	if(results != rhs.results)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}
