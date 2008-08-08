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


#ifndef YOGServerRouterPlayer_h
#define YOGServerRouterPlayer_h

#include "boost/shared_ptr.hpp"
#include "boost/weak_ptr.hpp"
#include "YOGServerRouterAdministrator.h"

class NetConnection;
class NetMessage;
class YOGServerGameRouter;
class YOGServerRouter;

///This represents a single connectee to the YOGServerRouterPlayer
class YOGServerRouterPlayer
{
public:
	///Constructs a YOGServerRouterPlayer to use the given net connection
	YOGServerRouterPlayer(boost::shared_ptr<NetConnection> connection, YOGServerRouter* router);

	///Provides a weak pointer to this class
	void setPointer(boost::weak_ptr<YOGServerRouterPlayer> pointer);

	///Sends a message to the player
	void sendNetMessage(boost::shared_ptr<NetMessage> message);

	///Updates this player
	void update();
	
	///Returns true if this player is still connected
	bool isConnected();
	
	///Returns true if this player is an admin
	bool isAdministrator();

private:
	boost::shared_ptr<NetConnection> connection;
	boost::shared_ptr<YOGServerGameRouter> game;
	YOGServerRouter* router;
	boost::weak_ptr<YOGServerRouterPlayer> pointer;
	bool isAdmin;
};

#endif
