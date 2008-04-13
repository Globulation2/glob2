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


void YOGGameResults::setNumberOfPlayers(int number)
{
	results.resize(number, YOGGameResultUnknown);
	names.resize(number);
}


void YOGGameResults::setGameResultState(int player, YOGGameResult result)
{
	results[player] = result;	
}


YOGGameResult YOGGameResults::getGameResultState(int player)
{
	return results[player];
}


void YOGGameResults::setPlayerName(int player, const std::string& name)
{
	names[player] = name;
}

	
std::string YOGGameResults::getPlayerName(int player)
{
	return names[player];
}


void YOGGameResults::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGGameResults");
	stream->writeUint32(results.size(), "size");
	for(int i=0; i<results.size(); ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint8(static_cast<Uint8>(results[i]), "result");
		stream->writeText(names[i], "name");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}



void YOGGameResults::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGGameResults");
	Uint32 size = stream->readUint32("size");
	results.resize(size);
	names.resize(size);
	for(int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		results[i] = static_cast<YOGGameResult>(stream->readUint8("result"));
		names[i] = stream->readText("name");
		stream->readLeaveSection();
	}

	stream->readLeaveSection();
}



bool YOGGameResults::operator==(const YOGGameResults& rhs) const
{
	if(results == rhs.results && names == rhs.names)
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
	if(results != rhs.results || names != rhs.names)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}
