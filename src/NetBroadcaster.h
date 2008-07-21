/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __NetBroadcaster_h
#define __NetBroadcaster_h

#include "LANGameInformation.h"
#include "SDL_net.h"

///This class allows for subnet broadcasting (hosting a LAN game)
class NetBroadcaster
{
public:
	///Creates a new NetBroadcaster with the given information to broadcast
	NetBroadcaster(LANGameInformation& info);
	
	~NetBroadcaster();
	
	///Begins broadcasting the following game information
	void broadcast(LANGameInformation& info);
	
	///Updates the broadcaster
	void update();
	
	///Disables broadcasting
	void disableBroadcasting();
	
	///Enables broadcasting
	void enableBroadcasting();
private:
	LANGameInformation info;
	UDPsocket socket;
	UDPsocket localsocket;
	Uint32 lastTime;
	Uint32 timer;
};

#endif
