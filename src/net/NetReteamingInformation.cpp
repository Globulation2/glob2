// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetReteamingInformation.h"

#include "Stream.h"

NetReteamingInformation::NetReteamingInformation()
{

}



void NetReteamingInformation::setPlayerToTeam(const std::string& playerName, int team)
{
	teams[playerName] = team;
}



bool NetReteamingInformation::doesPlayerHaveTeam(const std::string& playerName) const
{
	return (teams.find(playerName) != teams.end());
}



int NetReteamingInformation::getPlayersTeam(const std::string& playerName) const
{
	if(doesPlayerHaveTeam(playerName))
		return teams.find(playerName)->second;
	return RETEAM_NO_TEAM;
}



void NetReteamingInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetReteamingInformation");
	stream->writeEnterSection("teams");
	stream->writeUint32(teams.size(), "size");
	Uint32 n=0;
	for(std::map<std::string, int>::const_iterator i = teams.begin(); i!=teams.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeText(i->first, "playerName");
		stream->writeUint8(i->second, "team");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void NetReteamingInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetReteamingInformation");
	teams.clear();
	stream->readEnterSection("teams");
	Uint32 size=stream->readUint32("size");
	for(unsigned int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		std::string playerName = stream->readText("playerName");
		int team = stream->readUint8("team");
		teams[playerName]=team;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}



bool NetReteamingInformation::operator==(const NetReteamingInformation& rhs) const
{
	if(teams == rhs.teams)
		return true;
	return false;
}



bool NetReteamingInformation::operator!=(const NetReteamingInformation& rhs) const
{
	if(teams != rhs.teams)
		return true;
	return false;
}



