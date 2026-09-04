// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef NetConnectionThread_h
#define NetConnectionThread_h

#include "NetConnectionThreadMessage.h"
#include <memory>
#include <mutex>
#include <queue>

///IRC thread manages IRC
class NetConnectionThread
{
public:
	NetConnectionThread(std::queue<std::shared_ptr<NetConnectionThreadMessage> >& outgoing, std::recursive_mutex& outgoingMutex);
	
	~NetConnectionThread();
	
	///Runs the net thread
	void operator()();

	///Sends this net thread a message
	void sendMessage(std::shared_ptr<NetConnectionThreadMessage> message);

	///This returns whether the thread has exited
	bool hasThreadExited();

	///Returns true if this object is connected
	bool isConnected();
private:

	///Closes the connection
	void closeConnection();

	///Sends this net message back to the main thread
	void sendToMainThread(std::shared_ptr<NetConnectionThreadMessage> message);
	IPaddress address;
	TCPsocket socket;
	SDLNet_SocketSet set;
	bool connected;
	
	std::queue<std::shared_ptr<NetConnectionThreadMessage> > incoming;
	std::queue<std::shared_ptr<NetConnectionThreadMessage> >& outgoing;
	std::recursive_mutex incomingMutex;
	std::recursive_mutex& outgoingMutex;
	bool hasExited;
	//static Uint32 lastTime;
	//static Uint32 amount;
};


#endif
