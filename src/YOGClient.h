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

#ifndef __YOGClient_h
#define __YOGClient_h

#include "NetMessage.h"
#include "NetConnection.h"
#include "NetListener.h"
#include "YOGConsts.h"

///This represents the players YOG client, connecting to the YOG server.
class YOGClient
{
public:
	///Initializes and attempts to connect to server.
	YOGClient(const std::string& server);
	
	///Initializes the client as empty
	YOGClient();
	
	///Attempts a connection to server.
	void connect(const std::string& server);

	///Updates the client. This parses and interprets any incoming messages.
	void update();
private:
	NetConnection& nc;
	///This defines the current state of the connection. There are many states,
	///due to the asychronous design.
	enum ConnectionState
	{
		//This signified unconnected.
		NotConnected,
		///This is the starting state
		NeedToSendClientInformation,
		///This means that the client is waiting to recieve server information
		WaitingForServerInformation,
	};
	Uint32 connectionState;
};



#endif
