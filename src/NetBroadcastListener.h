// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __NetBroadcastListener_h
#define __NetBroadcastListener_h

#include "SDL_net.h"
#include "LANGameInformation.h"
#include <vector>

///This listens for sub-net broadcasts (finding a LAN game)
class NetBroadcastListener
{
public:
	///Constructs a NetBroadcastListener, and begins listening
	NetBroadcastListener();

	~NetBroadcastListener();

	///Updates the broadcast listener
	void update();

	///Gets a list of all the LAN games
	const std::vector<LANGameInformation>& getLANGames();

	///Gets the IP address for the given lan game
	std::string getIPAddress(size_t num);
	
	///Enables listening
	void enableListening();
	
	///Disables listening
	void disableListening();
private:
	UDPsocket socket;
	std::vector<LANGameInformation> games;
	std::vector<int> timeouts;
	std::vector<IPaddress> addresses;
	Uint64 lastTime;
};

#endif
