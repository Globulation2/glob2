// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGServerRouterManager_h
#define YOGServerRouterManager_h

#include <memory>
#include <vector>
#include "NetListener.h"

class NetConnection;
class YOGServer;

///This class manages the list of YOGServerRouters
class YOGServerRouterManager
{
public:
	///Creates a YOGServerRouter
	YOGServerRouterManager(YOGServer& server);

	///Adds a connection to a YOG
	void addRouter(std::shared_ptr<NetConnection> connection);
	
	///Updates this manager
	void update();
	
	///This chooses a new yog router
	std::shared_ptr<NetConnection> chooseYOGRouter();
private:
	std::vector<std::shared_ptr<NetConnection> > routers;
	NetListener listener;
	std::shared_ptr<NetConnection> new_connection;
	YOGServer& server;
	int n;
};


#endif
