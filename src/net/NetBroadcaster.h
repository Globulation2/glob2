// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "LANGameInformation.h"
#include <SDL3_net/SDL_net.h>

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
	NET_DatagramSocket *socket = nullptr;
	NET_Address *localaddress = nullptr;
	Uint64 lastTime = 0;
	Uint32 timer;
};

