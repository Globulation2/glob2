// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetListener.h"
#include <iostream>

NetListener::NetListener(Uint16 port)
{
	listening=false;
	startListening(port);
}


	
NetListener::NetListener()
{
	listening=false;
}



NetListener::~NetListener()
{
	stopListening();
}



void NetListener::startListening(Uint16 nport)
{
	if(!listening)
	{
		IPaddress address;
		if(SDLNet_ResolveHost(&address, NULL, nport) == -1)
		{
			if(verbose)
				std::cout<<"NetListener::startListening:"<<SDLNet_GetError()<<std::endl;
			listening=false;
		}
		
		socket=SDLNet_TCP_Open(&address);
		if(!socket)
		{
			if(verbose)
				std::cout<<"NetListener::startListening:"<<SDLNet_GetError()<<std::endl;
			listening=false;
		}
		else
		{
			listening=true;
			port = nport;
		}
	}
	

}



void NetListener::stopListening()
{
	if(listening)
		SDLNet_TCP_Close(socket);
	listening=false;
}


	
bool NetListener::isListening()
{
	return listening;
}


	
bool NetListener::attemptConnection(NetConnection& connection)
{
	if(listening)
	{
		bool accepted = connection.attemptConnection(socket);
		return accepted;
	}
	else
	{
		return false;
	}
	return false;
}


