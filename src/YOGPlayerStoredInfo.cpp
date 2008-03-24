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


YOGPlayerStoredInfo::YOGPlayerStoredInfo()
{
	muted = false;
}



void YOGPlayerStoredInfo::setMuted()
{
	muted = true;
}



void YOGPlayerStoredInfo::setUnmuted()
{
	muted = false;
}



bool YOGPlayerStoredInfo::isMuted()
{
	return muted;
}



void YOGPlayerStoredInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGPlayerStoredInfo");
	stream->writeUint8(muted, "muted");
	stream->writeLeaveSection();
}



void YOGPlayerStoredInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGPlayerStoredInfo");
	muted = stream->readUint8("muted");
	stream->readLeaveSection();
}



bool YOGPlayerStoredInfo::operator==(const YOGPlayerStoredInfo& rhs) const
{
	if(muted == rhs.muted)
		return true;
	return false;
}



bool YOGPlayerStoredInfo::operator!=(const YOGPlayerStoredInfo& rhs) const
{
	if(muted != rhs.muted)
		return true;
	return false;
}


