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

#ifndef YOGServerGameRouter_h
#define YOGServerGameRouter_h

#include <vector>
#include "boost/shared_ptr.hpp"

class YOGServerRouterPlayer;
class NetMessage;

///This class acts is the router for games, it routes messages between all connected players
class YOGServerGameRouter
{
public:
	///Constructs a YOGServerGameRouter
	YOGServerGameRouter(); 

	///Adds a player to this router group
	void addPlayer(boost::shared_ptr<YOGServerRouterPlayer> player);
	
	///Updates this game
	void update();
	
	///Returns true if this game is empty
	bool isEmpty();
	
	///Removes a net message to all players
	void routeMessage(boost::shared_ptr<NetMessage> message, YOGServerRouterPlayer* sender);
private:
	std::vector<boost::shared_ptr<YOGServerRouterPlayer> > players;
};


#endif
