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


