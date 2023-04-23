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

#ifndef __NetListener_h
#define __NetListener_h

#include "SDL_net.h"
#include "NetConnection.h"


///NetListener represents a low level wrapper arround SDL.
///It listens for incoming connections. One should frequently
///attemptConnection
class NetListener
{
public:
	///Creates and starts listening on the given port
	NetListener(Uint16 port);
	
	///Creates a null listener, start listening with startListening
	NetListener();
	
	///Stops listening if nesseccarry
	~NetListener();
	
	///Causes the listener to start listening on the provided port
	void startListening(Uint16 port);

	///Stops the listener from listening on the port
	void stopListening();
	
	///Returns true if the connection is activly listening, false otherwise
	bool isListening();
	
	///Attempts to accept an incoming connection, placing it
	///in the provided NetConnection. Returns true if successfull,
	///false otherwise.
	bool attemptConnection(NetConnection& connection);

private:
	static const bool verbose=false;
	TCPsocket socket;
	bool listening;
	Uint16 port;
};


#endif
