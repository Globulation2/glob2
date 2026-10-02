// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3_net/SDL_net.h>
#include <string>
#include <vector>
class NetBroadcaster
{
  public:
	NetBroadcaster(const std::string &identifier, const std::string &endpoint);
	~NetBroadcaster();
	void update();
	void disableBroadcasting();
	void enableBroadcasting();

  private:
	std::string beacon;
	NET_DatagramSocket *socket = nullptr;
	NET_Address *localaddress = nullptr;
	std::vector<NET_DatagramSocket *> broadcastSockets;
	Uint64 lastTime = 0;
};
