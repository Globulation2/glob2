// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL_net.h>
#include <string>
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
	UDPsocket socket = nullptr;
	Uint64 lastTime = 0;
};
