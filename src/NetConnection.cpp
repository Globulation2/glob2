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
#include "StreamBackend.h"
#include "BinaryStream.h"

using namespace GAGCore;

NetConnection::NetConnection(const std::string& address, Uint16 port)
{
	connected = false;
	set=SDLNet_AllocSocketSet(1);
	openConnection(address, port);
	count = 0;
	lastTime = 0;
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
		}
		
		//Open the connection
		socket=SDLNet_TCP_Open(&address);
		if(!socket)
		{
			std::cout<<"NetConnection::openConnection: "<<SDLNet_GetError()<<std::endl;
		}
		else
		{
			SDLNet_TCP_AddSocket(set, socket);
			connected=true;
			count = 0;
			lastTime = SDL_GetTicks();
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
			int amount = SDLNet_TCP_Recv(socket, lengthData, 2);
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
	
				for(int i=0; i<length; ++i)
				{
					amount = SDLNet_TCP_Recv(socket, data+i, 1);
					if(amount <= 0)
					{
						std::cout<<"NetConnection::getMessage: " << SDLNet_GetError() << std::endl;
						closeConnection();
					}
				}
				if(connected)
				{
					MemoryStreamBackend* msb = new MemoryStreamBackend(data, length);
					msb->seekFromStart(0);
					BinaryInputStream* bis = new BinaryInputStream(msb);

					//Now interpret the message from the data, and add it to the queue
					shared_ptr<NetMessage> message = NetMessage::getNetMessage(bis);
					recieved.push(message);
					std::cout<<"Recieved: "<<message->format()<<std::endl;
					
					delete bis;
				}
				delete[] data;
			}
			delete[] lengthData;
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

		MemoryStreamBackend* msb = new MemoryStreamBackend;
		BinaryOutputStream* bos = new BinaryOutputStream(msb);
		bos->writeUint8(message->getMessageType(), "messageType");
		message->encodeData(bos);
		
		msb->seekFromEnd(0);
		Uint32 length = msb->getPosition();
		msb->seekFromStart(0);
		
		Uint8* newData = new Uint8[length+2];
		SDLNet_Write16(length, newData);
		msb->read(newData+2, length);
		
		count += length + 2;
		if(count >= 500)
		{
			float speed = (float)(count) / (float)(SDL_GetTicks() - lastTime);
			std::cout<<"speed "<<speed*1000<<" bp/s"<<std::endl;
			count = 0;
			lastTime = SDL_GetTicks();
		}

		Uint32 result=SDLNet_TCP_Send(socket, newData, length+2);
		if(result<(length+2))
		{
			std::cout<<"NetConnection::sendMessage: "<<SDLNet_GetError()<<std::endl;
			closeConnection();
		}
		delete bos;
		delete[] newData;
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


