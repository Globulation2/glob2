// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>
#include "YOGPlayerID.h"
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
	YOGPlayerSessionInfo(const std::string& playerName, YOGPlayerID id);

	///Sets the name of the player
	void setPlayerName(const std::string& playerName);
	
	///Returns the name of the player
	std::string getPlayerName() const;

	///Sets the unique player ID
	void setPlayerID(YOGPlayerID id);
	
	///Returns the unique player ID
	YOGPlayerID getPlayerID() const;
	
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
	YOGPlayerID playerID;
	std::string playerName;
	YOGPlayerStoredInfo stored;
};
