// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <memory>
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
	std::shared_ptr<YOGServerGameRouter> getGame(Uint16 gameID);
	
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
	std::shared_ptr<NetConnection> new_connection;
	std::shared_ptr<NetConnection> yog_connection;
	std::map<Uint16, std::shared_ptr<YOGServerGameRouter> > games;
	std::vector<std::shared_ptr<YOGServerRouterPlayer> > players;
	YOGServerRouterAdministrator admin;
	bool shutdownMode;
};

