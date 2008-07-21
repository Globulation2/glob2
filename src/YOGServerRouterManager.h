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

#ifndef YOGServerRouterManager_h
#define YOGServerRouterManager_h

#include "boost/shared_ptr.hpp"
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
	void addRouter(boost::shared_ptr<NetConnection> connection);
	
	///Updates this manager
	void update();
	
	///This chooses a new yog router
	boost::shared_ptr<NetConnection> chooseYOGRouter();
private:
	std::vector<boost::shared_ptr<NetConnection> > routers;
	NetListener listener;
	boost::shared_ptr<NetConnection> new_connection;
	YOGServer& server;
	int n;
};


#endif
