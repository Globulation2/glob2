// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>
#include <map>

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

//! Sentinel returned by NetReteamingInformation::getPlayersTeam() when the
//! given player has no auto-assigned team (i.e. did not appear in the
//! source save-game). See NetReteamingInformation.cpp:34.
static constexpr int RETEAM_NO_TEAM = -1;


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

