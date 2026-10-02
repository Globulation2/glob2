// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3_net/SDL_net.h>
#include <string>
#include <vector>
struct LANDiscoveredHost
{
	std::string identifier, endpoint;
	Uint64 lastSeen = 0;
};
class NetBroadcastListener
{
  public:
	NetBroadcastListener();
	~NetBroadcastListener();
	void update();
	const std::vector<LANDiscoveredHost> &getLANHosts() const
	{
		return hosts;
	}
	std::string getIPAddress(size_t index) const
	{
		return hosts.at(index).endpoint;
	}
	bool isListening() const
	{
		return socket != nullptr;
	}
	void enableListening();
	void disableListening();

  private:
	NET_DatagramSocket *socket = nullptr;
	std::vector<LANDiscoveredHost> hosts;
};
