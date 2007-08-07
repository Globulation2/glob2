/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __LANGameInformation_h
#define __LANGameInformation_h

#include "YOGGameInfo.h"
#include "SDL_net.h"

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
