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

}



const std::string& P2PPlayerInformation::getIPAddress() const
{
	return ipAddress;
}



void P2PPlayerInformation::setIPAddress(const std::string& nipAddress)
{
	ipAddress = nipAddress;
}



void P2PPlayerInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("P2PPlayerInformation");
	stream->writeText(ipAddress, "ipAddress");
	stream->writeLeaveSection();
}



void P2PPlayerInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("P2PPlayerInformation");
	ipAddress = stream->readText("ipAddress");
	stream->readLeaveSection();
}


	
bool P2PPlayerInformation::operator==(const P2PPlayerInformation& rhs) const
{
	if(ipAddress == rhs.ipAddress)
		return true;
	return false;
}



bool P2PPlayerInformation::operator!=(const P2PPlayerInformation& rhs) const
{
	if(ipAddress != rhs.ipAddress)
		return true;
	return false;
}



