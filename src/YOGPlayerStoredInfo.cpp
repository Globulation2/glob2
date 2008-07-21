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

#include "YOGPlayerStoredInfo.h"

#include "Stream.h"
#include <sstream>

YOGPlayerStoredInfo::YOGPlayerStoredInfo()
{
	banned=false;
	moderator=false;
	///The default rating is 1000
	rating = 1000;
}



void YOGPlayerStoredInfo::setMuted(boost::posix_time::ptime nunmute_time)
{
	unmute_time = nunmute_time;
}



void YOGPlayerStoredInfo::setUnmuted()
{
	unmute_time = boost::posix_time::ptime();
}



bool YOGPlayerStoredInfo::isMuted() const
{
	boost::posix_time::ptime current_time = boost::posix_time::second_clock::local_time();
	if(unmute_time == boost::posix_time::ptime() || unmute_time < current_time)
	{
		return false;
	}
	return true;
}



void YOGPlayerStoredInfo::setBanned()
{
	banned = true;
}



void YOGPlayerStoredInfo::setUnbanned()
{
	banned = false;
}



bool YOGPlayerStoredInfo::isBanned() const
{
	return banned;
}



void YOGPlayerStoredInfo::setModerator(bool isModerator)
{
	moderator=isModerator;
}



bool YOGPlayerStoredInfo::isModerator() const
{
	return moderator;
}



void YOGPlayerStoredInfo::setPlayerRating(int nrating)
{
	rating = nrating;
}



int YOGPlayerStoredInfo::getPlayerRating() const
{
	return rating;
}



void YOGPlayerStoredInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGPlayerStoredInfo");
	std::stringstream time;
	time<<unmute_time;
	stream->writeText(time.str(), "unmute_time");
	stream->writeUint8(banned, "banned");
	stream->writeUint8(moderator, "moderator");
	stream->writeUint32(rating, "rating");
	stream->writeLeaveSection();
}



void YOGPlayerStoredInfo::decodeData(GAGCore::InputStream* stream, Uint32 dataVersionMinor)
{
	stream->readEnterSection("YOGPlayerStoredInfo");
	std::string b = stream->readText("unmute_time");
	std::stringstream time;
	time<<b;
	time>>unmute_time;
	banned=stream->readUint8("banned");
	moderator=stream->readUint8("moderator");
	rating=stream->readUint32("rating");
	stream->readLeaveSection();
}



bool YOGPlayerStoredInfo::operator==(const YOGPlayerStoredInfo& rhs) const
{
	if(unmute_time == rhs.unmute_time && banned == rhs.banned && rating == rhs.rating)
		return true;
	return false;
}



bool YOGPlayerStoredInfo::operator!=(const YOGPlayerStoredInfo& rhs) const
{
	if(unmute_time != rhs.unmute_time && banned == rhs.banned && rating == rhs.rating)
		return true;
	return false;
}


