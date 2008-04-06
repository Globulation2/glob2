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

#ifndef P2PManager_h
#define P2PManager_h

#include "boost/shared_ptr.hpp"
#include "P2PInformation.h"
#include <vector>

class YOGServerPlayer;
class NetMessage;

///This class represents the P2P manager, which acts as a third party to manage a group of P2P connections.
///This class operates server-side
class P2PManager
{
public:
	///Constructs a P2PManager
	P2PManager();

	///Adds a player to this P2P group
	void addPlayer(boost::shared_ptr<YOGServerPlayer> player);
	
	///Removes a player from this P2P group
	void removePlayer(boost::shared_ptr<YOGServerPlayer> player);
	
	///Updates this client
	void update();
	
	///This recieves a message from a player. The player must be in this p2p group
	void recieveMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player);
private:
	///Sends a message to all the players except player
	void sendNetMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player = boost::shared_ptr<YOGServerPlayer>());

	std::vector<boost::shared_ptr<YOGServerPlayer> > players;
	P2PInformation group;
};


#endif
