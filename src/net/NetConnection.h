/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __NetConnection_h
#define __NetConnection_h

#include "SDL_net.h"
#include "NetConnectionThread.h"
#include <queue>
#include <boost/shared_ptr.hpp>

using namespace boost;

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
	
	std::queue<boost::shared_ptr<NetConnectionThreadMessage> > incoming;
	boost::recursive_mutex incomingMutex;
	std::queue<shared_ptr<NetMessage> > recieved;
	
	std::string address;
	bool connecting;
};


#endif
