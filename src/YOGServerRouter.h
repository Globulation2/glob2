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

#ifndef YOGServerRouter_h
#define YOGServerRouter_h

#include "boost/shared_ptr.hpp"
#include "SDL_net.h"
#include <vector>
#include <map>
#include "NetListener.h"

class NetConnection;
class YOGServerGameRouter;
class YOGServerRouterPlayer;

///This class acts as a server router. Bassically, it routes the messages for a game between players.
///The main YOG server delegates down to this system, which may be on another server, and quite possibly
///on multiple servers
class YOGServerRouter
{
public:
	///This constructs a router
	YOGServerRouter();

	///This updates the router
	void update();
	
	///Runs the router as its own entity. Returns the return code of the execution
	int run();

	///Returns the game id
	boost::shared_ptr<YOGServerGameRouter> getGame(Uint16 gameID);

private:
	NetListener nl;
	boost::shared_ptr<NetConnection> new_connection;
	boost::shared_ptr<NetConnection> yog_connection;
	std::map<Uint16, boost::shared_ptr<YOGServerGameRouter> > games;
	std::vector<boost::shared_ptr<YOGServerRouterPlayer> > players;
};

#endif
