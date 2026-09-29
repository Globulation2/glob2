// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGPlayerStoredInfo.h"

#include "Stream.h"

YOGPlayerStoredInfo::YOGPlayerStoredInfo()
{
	unmute_time = notADateTime();
	banned=false;
	moderator=false;
	///The default rating is 1000
	rating = 1000;
}



void YOGPlayerStoredInfo::setMuted(LocalTime nunmute_time)
{
	unmute_time = nunmute_time;
}



void YOGPlayerStoredInfo::setUnmuted()
{
	unmute_time = notADateTime();
}



bool YOGPlayerStoredInfo::isMuted() const
{
	LocalTime current_time = LocalClock::now();
	if(unmute_time == notADateTime() || unmute_time < current_time)
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
	stream->writeText(toString(unmute_time), "unmute_time");
	stream->writeUint8(banned, "banned");
	stream->writeUint8(moderator, "moderator");
	stream->writeUint32(rating, "rating");
	stream->writeLeaveSection();
}



void YOGPlayerStoredInfo::decodeData(GAGCore::InputStream* stream, Uint32 dataVersionMinor)
{
	stream->readEnterSection("YOGPlayerStoredInfo");
	std::string b = stream->readText("unmute_time");
	parseLocalTime(b, unmute_time);
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


