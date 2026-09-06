// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

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
	Uint64 lastTime;
	Uint32 timer;
};

