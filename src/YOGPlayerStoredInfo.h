// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

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
	bool isMuted() const;

	///Sets this player to be banned
	void setBanned();

	///Sets this player to be unbanned
	void setUnbanned();
	
	///Returns true if this player is banned, false otherwise
	bool isBanned() const;
	
	///Sets whether this player is a moderator or not
	void setModerator(bool isModerator);
	
	///Returns whether this player is a moderator
	bool isModerator() const;
	
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
