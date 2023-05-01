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

#include "NetReteamingInformation.h"

#include <iostream>
#include "Stream.h"

NetReTeamingInformation::NetReTeamingInformation()
{

}



void NetReTeamingInformation::setPlayerToTeam(const std::string& playerName, int team)
{
	teams[playerName] = team;
}



bool NetReTeamingInformation::doesPlayerHaveTeam(const std::string& playerName) const
{
	return (teams.find(playerName) != teams.end());
}



int NetReTeamingInformation::getPlayersTeam(const std::string& playerName) const
{
	if(doesPlayerHaveTeam(playerName))
		return teams.find(playerName)->second;
	return -1;
}



void NetReTeamingInformation::encodeData(GAGCore::OutputStream* stream) const
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



void NetReTeamingInformation::decodeData(GAGCore::InputStream* stream)
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



bool NetReTeamingInformation::operator==(const NetReTeamingInformation& rhs) const
{
	if(teams == rhs.teams)
		return true;
	return false;
}



bool NetReTeamingInformation::operator!=(const NetReTeamingInformation& rhs) const
{
	if(teams != rhs.teams)
		return true;
	return false;
}



