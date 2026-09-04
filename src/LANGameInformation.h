// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __LANGameInformation_h
#define __LANGameInformation_h

#include "YOGGameInfo.h"
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class represents the information for a LAN game
class LANGameInformation
{
public:
	///Constructs a LANGameInformation with the given game information
	LANGameInformation(const YOGGameInfo& information);

	///Constructs an empty LANGameInformation
	LANGameInformation();
	
	///Encodes this LANGameInformation into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this LANGameInformation from a bit stream
	void decodeData(GAGCore::InputStream* stream);

	///Returns the game information
	const YOGGameInfo& getGameInformation() const;
	
	///Returns the game information for modification
	YOGGameInfo& getGameInformation();
private:
	YOGGameInfo gameInfo;
};



#endif
