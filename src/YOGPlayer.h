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

#ifndef __YOGPlayer_h
#define __YOGPlayer_h

#include "YOGGameServer.h"
#include <boost/shared_ptr.hpp>

using namespace boost;

///This represents a connected user on the YOG server.
class YOGPlayer
{
public:
	///Establishes a YOGPlayer on the given connection.
	YOGPlayer(shared_ptr<NetConnection> connection);

	///Updates the YOGPlayer. This deals with all incoming messages.
	void update();

	///Returns true if this YOGPlayer is still connected
	bool isConnected();

private:
	enum
	{
		///Means this is waiting for the client to send version information to the server.
		WaitingForClientInformation,
		///Server information, such as the IRC server and server policies, needs to be sent
		NeedToSendServerInformation.
	};

	Uint8 connectionState;

	shared_ptr<NetConnection> connection;
	Uint16 versionMinor;
};





#endif
