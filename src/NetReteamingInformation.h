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

#ifndef NetReteamingInformation_h
#define NetReteamingInformation_h

#include <string>
#include <map>

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}


///Reteaming is when you load a YOG save-game in YOG, and if the same players join, it automatically sets their team color
///This class stores reteaming information
class NetReteamingInformation
{
public:
	///NetReteamingInformation stores information to reload team colors in a Net game
	NetReteamingInformation();

	///Sets the player with the given name to be automatically set to the given team
	void setPlayerToTeam(const std::string& playerName, int team);
	
	///Returns true if this player name has an automatic team number assocciatted with it
	bool doesPlayerHaveTeam(const std::string& playerName) const;
	
	///Returns the team for the given player, -1 is this player doesn't have an automatic team
	int getPlayersTeam(const std::string& playerName) const;
	
	///Encodes this YOGGameInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGGameInfo
	bool operator==(const NetReteamingInformation& rhs) const;
	bool operator!=(const NetReteamingInformation& rhs) const;
private:
	std::map<std::string, int> teams;
};

#endif
