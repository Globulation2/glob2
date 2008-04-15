/*
  Copyright 2008 (C) Bradley Arsenault

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
	for(int i=0; i<size; ++i)
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
