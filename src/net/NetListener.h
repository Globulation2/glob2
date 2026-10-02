// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NetConnection.h"
class NetListener
{
  public:
	NetListener() = default;
	explicit NetListener(Uint16 port) { startListening(port); }
	void startListening(Uint16 port)
	{
		NetListenConfig config;
		config.protocol = NetListenConfig::Protocol::Tcp;
		config.port = port;
		startListening(config);
	}
	explicit NetListener(const NetListenConfig &config) { startListening(config); }
	~NetListener() = default;
	void startListening(const NetListenConfig &config);
	void stopListening();
	bool isListening() const;
	bool attemptConnection(NetConnection &connection);

  private:
	std::unique_ptr<NetTransportListener> listener;
};
