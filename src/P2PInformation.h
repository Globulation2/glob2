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


#ifndef P2PInformation_h
#define P2PInformation_h

#include <string>
#include <vector>
#include "Stream.h"
#include "P2PPlayerInformation.h"


///This class carries the information for P2P routing
class P2PInformation
{
public:
	///This constructs the p2p information
	P2PInformation();
	
	///Adds a P2P player
	void addP2PPlayer(P2PPlayerInformation& information);
	
	///Retrieves a P2PPlayer
	P2PPlayerInformation& getPlayerInformation(size_t n);
	const P2PPlayerInformation& getPlayerInformation(size_t n) const;
	
	///Removes a P2P player
	void removeP2PPlayer(size_t n);
	
	///Returns the number of P2P players there are
	size_t getNumberOfP2PPlayer() const;

	///Encodes this P2PInformation into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this P2PInformation from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two P2PInformation
	bool operator==(const P2PInformation& rhs) const;
	bool operator!=(const P2PInformation& rhs) const;
private:
	std::vector<P2PPlayerInformation> players;
};

#endif
