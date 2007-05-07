/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __YOGGame_h
#define __YOGGame_h

#include "YOGPlayer.h"
#include <boost/shared_ptr.hpp>

///This handles a "game" from the server's point of view. This means that it handles
///routing between clients, holding the map and game data, etc..
class YOGGame
{
public:
	///Constructs a new YOG game
	YOGGame(Uint16 gameID);

	///Adds the player to the game
	void addPlayer(shared_ptr<YOGPlayer> player);

	///Removes the player from the game
	void removePlayer(shared_ptr<YOGPlayer> player);

	///Routes the given message to all the players in the game
	void routeMessage(shared_ptr<NetMessage> message);

private:
	Uint16 gameID;
	std::vector<shared_ptr<YOGPlayer> > players;
};


#endif
