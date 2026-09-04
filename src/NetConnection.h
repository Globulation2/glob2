// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __NetConnection_h
#define __NetConnection_h

#include "SDL_net.h"
#include "NetConnectionThread.h"
#include <queue>
#include <thread>
#include <memory>

using std::shared_ptr;

class NetListener;
class NetMessage;

///NetConnection represents a low level wrapper arround SDL.
///It queues Message(s) it recieves from the connection.
class NetConnection
{
public:
	///Attempts to form a connection with the given address and the given port
	NetConnection(const std::string& address, Uint16 port);

	///Initiates the NetConnection as blank
	NetConnection();

	///Closes the NetConnection down.
	~NetConnection();
	
	///Opens a new connection.
	void openConnection(const std::string& address, Uint16 port);

	///Closes the current connection.
	void closeConnection();

	///Returns true if this object is connected
	bool isConnected();
	
	///Returns whether this object is in the proccess of connecting
	bool isConnecting();

	///Updates messages from the thread
	void update();
	
	///Pops the top-most message in the queue of recieved messages.
	///When there are no messages, it will poll SDL for more packets.
	///The caller assumes ownership of the NetMessage.
	shared_ptr<NetMessage> getMessage();
	
	///Sends a message across the connection.
	void sendMessage(shared_ptr<NetMessage> message);
	
	///Returns the IP address
	const std::string& getIPAddress() const;
protected:
	friend class NetListener;

	///This function attempts a connection using the provided TCP server socket.
	///One can use isConnected to test for success.
	bool attemptConnection(TCPsocket& serverSocket);
	
private:
	NetConnectionThread connect;
	std::thread connectThread;

	std::queue<std::shared_ptr<NetConnectionThreadMessage> > incoming;
	std::recursive_mutex incomingMutex;
	std::queue<shared_ptr<NetMessage> > recieved;
	
	std::string address;
	bool connecting;
};


#endif
