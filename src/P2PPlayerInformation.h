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


#ifndef P2PPlayerInformation_h
#define P2PPlayerInformation_h

#include <string>
#include "Stream.h"

///This class represents a single player on the P2P routing system
class P2PPlayerInformation
{
public:
	P2PPlayerInformation();

	///Returns the IP address if the p2p player
	const std::string& getIPAddress() const;
	
	///Sets the IP address of the p2p player
	void setIPAddress(const std::string& ipAddress);

	///Returns the port of the p2p player
	int getPort() const;
	
	///Sets the port of the p2p player
	void setPort(int port);

	///Returns the yog playerID of the p2p player
	int getPlayerID() const;
	
	///Sets the port of the p2p player
	void setPlayerID(int port);

	///Encodes this PPlayerInformation into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this PPlayerInformation from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two PPlayerInformation
	bool operator==(const P2PPlayerInformation& rhs) const;
	bool operator!=(const P2PPlayerInformation& rhs) const;
private:
	std::string ipAddress;
	int port;
	int playerID;
};


#endif
