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
#include <algorithm>
#include <iostream>


NetConnection::NetConnection(const std::string& address, Uint16 port)
{
	connected = false;
	set=SDLNet_AllocSocketSet(1);
	openConnection(address, port);
}



NetConnection::NetConnection()
{
	set=SDLNet_AllocSocketSet(1);
	connected = false;
}



NetConnection::~NetConnection()
{
	closeConnection();
	SDLNet_FreeSocketSet(set);
}


	
void NetConnection::openConnection(const std::string& connectaddress, Uint16 port)
{
	if(!connected)
	{
		//Resolve the address
		if(SDLNet_ResolveHost(&address, connectaddress.c_str(), port) == -1)
		{
			std::cout<<"NetConnection::openConnection: "<<SDLNet_GetError()<<std::endl;
			assert(false);
		}
		
		//Open the connection
		socket=SDLNet_TCP_Open(&address);
		if(!socket)
		{
			std::cout<<"NetConnection::openConnection: "<<SDLNet_GetError()<<std::endl;
			assert(false);
		}
		else
		{
			SDLNet_TCP_AddSocket(set, socket);
			connected=true;
		}
	}
}



void NetConnection::closeConnection()
{
	if(connected)
	{
		SDLNet_TCP_DelSocket(set, socket);
		SDLNet_TCP_Close(socket);
		connected=false;
	}
}



bool NetConnection::isConnected()
{
	return connected;
}



shared_ptr<NetMessage> NetConnection::getMessage()
{
	//Poll the SDL_net socket for more messages
	while (true)
	{
		int numReady = SDLNet_CheckSockets(set, 0);	
		//This checks if there are any active sockets.
		//SDLNet_CheckSockets is used because it is non-blocking
		if(numReady==-1) {
			std::cout<<"NetConnection::getMessage: " << SDLNet_GetError() << std::endl;
			//most of the time this is a system error, so use perror
			perror("SDLNet_CheckSockets");
		}
		else if(numReady) {
			//Read and interpret the length of the message
			Uint8* lengthData = new Uint8[2];
			Uint16 amount = SDLNet_TCP_Recv(socket, lengthData, 2);
			if(amount <= 0)
			{
				std::cout<<"NetConnection::getMessage: " << SDLNet_GetError() << std::endl;
				closeConnection();
			}
			else
			{
				Uint16 length = SDLNet_Read16(lengthData);
				//Read in the data.
				Uint8* data = new Uint8[length];
	
				amount = SDLNet_TCP_Recv(socket, data, length);
				if(amount <= 0)
				{
					std::cout<<"NetConnection::getMessage: " << SDLNet_GetError() << std::endl;
					closeConnection();
				}
				else
				{
					//Now interpret the message from the data, and add it to the queue
					shared_ptr<NetMessage> message = NetMessage::getNetMessage(data, length);
					recieved.push(message);
					std::cout<<"Recieved: "<<message->format()<<std::endl;
				}
			}
		}
		else
		{
			break;
		}
	}


	//Check if there are messages in the queue.
	//If so, return one, else, return NULL
	if(recieved.size())
	{
		shared_ptr<NetMessage> message = recieved.front();
		recieved.pop();
		return message;
	}
	else
	{
		return shared_ptr<NetMessage>();
	}
}


	
void NetConnection::sendMessage(shared_ptr<NetMessage> message)
{
	if(connected)
	{
		std::cout<<"Sending: "<<message->format()<<std::endl;
		Uint32 length = message->getDataLength();
		Uint8* data = message->encodeData();		
		Uint8* newData = new Uint8[length+2];
		SDLNet_Write16(length, newData);
		std::copy(data, data+length, newData+2);
		Uint32 result=SDLNet_TCP_Send(socket, newData, length+2);
		if(result<length)
		{
			std::cout<<"NetConnection::sendMessage: "<<SDLNet_GetError()<<std::endl;
			closeConnection();
		}
		delete data;
		delete newData;
	}
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
			address = *SDLNet_TCP_GetPeerAddress(socket);
			SDLNet_TCP_AddSocket(set, socket);
		}
	}
}


