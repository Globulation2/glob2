// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

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


