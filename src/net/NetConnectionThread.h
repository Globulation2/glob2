// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "NetConnectionThreadMessage.h"
#include "ThreadMessageQueues.h"

///Manages a single TCP connection on a worker thread
class NetConnectionThread : public ThreadMessageQueues<NetConnectionThreadMessage>
{
public:
	NetConnectionThread(std::queue<std::shared_ptr<NetConnectionThreadMessage> >& outgoing, std::recursive_mutex& outgoingMutex);

	~NetConnectionThread();

	///Runs the net thread
	void operator()();

	///Returns true if this object is connected
	bool isConnected();

private:
	///Closes the connection
	void closeConnection();

	IPaddress address;
	TCPsocket socket;
	SDLNet_SocketSet set;
	bool connected;
};
