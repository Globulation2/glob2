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
#include "YOGServerRouterAdministrator.h"

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

	///This constructs a router with a specific ip address of the server
	YOGServerRouter(const std::string& yogip);

	///This updates the router
	void update();

	///Runs the router as its own entity. Returns the return code of the execution
	int run();

	///Returns the game id
	boost::shared_ptr<YOGServerGameRouter> getGame(Uint16 gameID);

	///Returns true if the password given is correct for the administrator for this server
	bool isAdministratorPasswordCorrect(const std::string& password);

	///Returns the router administrator
	YOGServerRouterAdministrator& getAdministrator();

	///This puts the router into shutdown mode, disconnecting from YOG and turning off once all clients disconnect
	void enterShutdownMode();

	///This prints a status report of the router
	std::string getStatusReport();

private:
	NetListener nl;
	boost::shared_ptr<NetConnection> new_connection;
	boost::shared_ptr<NetConnection> yog_connection;
	std::map<Uint16, boost::shared_ptr<YOGServerGameRouter> > games;
	std::vector<boost::shared_ptr<YOGServerRouterPlayer> > players;
	YOGServerRouterAdministrator admin;
	bool shutdownMode;
};

#endif
