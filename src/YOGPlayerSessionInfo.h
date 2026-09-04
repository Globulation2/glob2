// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGPlayerSessionInfo_h
#define __YOGPlayerSessionInfo_h

#include <string>
#include "SDL_net.h"
#include "YOGPlayerStoredInfo.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}


///This class contains information related to a player connected to YOG
class YOGPlayerSessionInfo
{
public:
	///Construct an empty YOGPlayerSessionInfo
	YOGPlayerSessionInfo();

	///Construct a YOGPlayerSessionInfo
	YOGPlayerSessionInfo(const std::string& playerName, Uint16 id);

	///Sets the name of the player
	void setPlayerName(const std::string& playerName);
	
	///Returns the name of the player
	std::string getPlayerName() const;

	///Sets the unique player ID
	void setPlayerID(Uint16 id);
	
	///Returns the unique player ID
	Uint16 getPlayerID() const;
	
	///Returns the players stored info
	const YOGPlayerStoredInfo& getPlayerStoredInfo() const;
	
	///Sets the player stored info
	void setPlayerStoredInfo(const YOGPlayerStoredInfo& info);

	///Encodes this YOGPlayerSessionInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGPlayerSessionInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGPlayerSessionInfo
	bool operator==(const YOGPlayerSessionInfo& rhs) const;
	bool operator!=(const YOGPlayerSessionInfo& rhs) const;
private:
	Uint16 playerID;
	std::string playerName;
	YOGPlayerStoredInfo stored;
};

#endif
