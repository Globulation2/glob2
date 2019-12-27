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


#ifndef YOGGameResults_h
#define YOGGameResults_h

#include "YOGConsts.h"
#include <map>

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This stores the win/lose/disconnected results for a single game
class YOGGameResults
{
public:
	///Constructs a default yog game results
	YOGGameResults();

	///Sets the game result state for a particular player
	void setGameResultState(const std::string& player, YOGGameResult result);

	///Gets the game result state for a particular player
	YOGGameResult getGameResultState(const std::string& player);

	///Encodes this YOGGameResults into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameResults from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 netDataVersion);

	///Test for equality between two YOGGameResults
	bool operator==(const YOGGameResults& rhs) const;
	bool operator!=(const YOGGameResults& rhs) const;
private:
	std::map<std::string, YOGGameResult> results;
};

#endif
