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
#include <vector>

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

	///Sets the number of players for this game result
	void setNumberOfPlayers(int number);

	///Sets the game result state for a particular player
	void setGameResultState(int player, YOGGameResult result);

	///Gets the game result state for a particular player
	YOGGameResult getGameResultState(int player);
	
	///Sets the player name for a particular player number
	void setPlayerName(int player, const std::string& name);
	
	///Returns the player name for a particular player number
	std::string getPlayerName(int player);
	
	///Encodes this YOGGameResults into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameResults from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGGameResults
	bool operator==(const YOGGameResults& rhs) const;
	bool operator!=(const YOGGameResults& rhs) const;
private:
	std::vector<YOGGameResult> results;
	std::vector<std::string> names;
};

#endif
