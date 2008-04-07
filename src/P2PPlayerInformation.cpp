/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "P2PPlayerInformation.h"


P2PPlayerInformation::P2PPlayerInformation()
{
	port = 0;
}



const std::string& P2PPlayerInformation::getIPAddress() const
{
	return ipAddress;
}



void P2PPlayerInformation::setIPAddress(const std::string& nipAddress)
{
	ipAddress = nipAddress;
}



int P2PPlayerInformation::getPort() const
{
	return port;
}



void P2PPlayerInformation::setPort(int nport)
{
	port = nport;
}



int P2PPlayerInformation::getPlayerID() const
{
	return playerID;
}



void P2PPlayerInformation::setPlayerID(int nplayerID)
{
	playerID = nplayerID;
}



void P2PPlayerInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("P2PPlayerInformation");
	stream->writeText(ipAddress, "ipAddress");
	stream->writeUint16(port, "port");
	stream->writeUint16(playerID, "playerID");
	stream->writeLeaveSection();
}



void P2PPlayerInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("P2PPlayerInformation");
	ipAddress = stream->readText("ipAddress");
	port = stream->readUint16("port");
	playerID = stream->readUint16("playerID");
	stream->readLeaveSection();
}


	
bool P2PPlayerInformation::operator==(const P2PPlayerInformation& rhs) const
{
	if(ipAddress == rhs.ipAddress && port == rhs.port && playerID == rhs.playerID)
		return true;
	return false;
}



bool P2PPlayerInformation::operator!=(const P2PPlayerInformation& rhs) const
{
	if(ipAddress != rhs.ipAddress || port != rhs.port || playerID != rhs.playerID)
		return true;
	return false;
}



