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

#include "NetConnection.h"

NetConnection::NetConnection(const std::string& address, Uint16 port)
{
	connected = false;
	openConnection(address, port);
}



NetConnection::NetConnection()
{

}


	
void NetConnection::openConnection(const std::string& address, Uint16 port)
{
	if(!connected)
	{
		//Resolve the address
		if(SDLNet_ResolveHost(&address, ipaddress.c_str(), port) == -1)
		{
			printf("NetConnection::openConnection: %s\n", SDLNet_GetError());
			assert(false);
		}
		
		//Open the connection
		socket=SDLNet_TCP_Open(&address);
		if(!socket)
		{
			printf("NetConnection::openConnection: %s\n", SDLNet_GetError());
			assert(false);
		}
		else
		{
			connected=true;
		}
	}
}



void NetConnection::closeConnection()
{
	if(connected)
		SDLNet_TCP_Close(socket);
	connected=false;
}



bool NetConnection::isConnected()
{
	return connected;
}



Message NetConnection::getMessage()
{

}


	
void NetConnection::queueMessage(const Message& message)
{

}


	
void NetConnection::attemptConnection(TCPsocket& serverSocket)
{
	if(!connected)
	{
		socket=SDLNet_TCP_Accept(serverSocket);
		if(!socket)
		{
		}
		else
		{
			connected=true;
		}
	}
}


