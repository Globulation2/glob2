/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "SDL_net.h"

class NetListener;

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

	///Pops the top-most message in the queue of messages.
	///When there are no messages, it will poll SDL for more packets.
	Message getMessage();
	
	///Queues a message for sending to the connection.
	void queueMessage(const Message& message);
protected:
	friend class NetListener;

	///This function attempts a connection using the provided TCP server socket.
	///One can use isConnected to test for success.
	void attemptConnection(TCPsocket& serverSocket);

private:
	IPaddress address;
	TCPsocket socket;
	bool connected;
};
