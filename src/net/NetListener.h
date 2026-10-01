// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NetConnection.h"
class NetListener
{
  public:
	NetListener() = default;
	explicit NetListener(const NetListenConfig &config)
	{
		startListening(config);
	}
	~NetListener() = default;
	void startListening(const NetListenConfig &config);
	void stopListening();
	bool isListening() const;
	bool attemptConnection(NetConnection &connection);

  private:
	std::unique_ptr<NetTransportListener> listener;
};
