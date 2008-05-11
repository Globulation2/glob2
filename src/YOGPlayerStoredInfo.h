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

#ifndef YOGPlayerStoredInfo_h
#define YOGPlayerStoredInfo_h

#include "boost/date_time/posix_time/posix_time.hpp"
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class represents information stored on the server about players
class YOGPlayerStoredInfo
{
public:
	///Constructs a default YOGPlayerStoredInfo
	YOGPlayerStoredInfo();
	
	///Sets this player to be muted until the given time
	void setMuted(boost::posix_time::ptime unmute_time);
	
	///Sets this player to be unmuted
	void setUnmuted();
	
	///Returns true if this player is muted, false otherwise
	bool isMuted();

	///Sets this player to be banned
	void setBanned();

	///Sets this player to be unbanned
	void setUnbanned();
	
	///Returns true if this player is banned, false otherwise
	bool isBanned();
	
	///Sets whether this player is a moderator or not
	void setModerator(bool isModerator);
	
	///Returns whether this player is a moderator
	bool isModerator();
	
	///Sets this players rating
	void setPlayerRating(int rating);
	
	///Gets this players rating
	int getPlayerRating() const;

	///Encodes this YOGPlayerStoredInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGPlayerStoredInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 dataVersionMinor);
	
	///Test for equality between two YOGPlayerStoredInfo
	bool operator==(const YOGPlayerStoredInfo& rhs) const;
	bool operator!=(const YOGPlayerStoredInfo& rhs) const;
private:
	boost::posix_time::ptime unmute_time;
	bool banned;
	bool moderator;
	int rating;
};

#endif
