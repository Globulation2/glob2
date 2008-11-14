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

#include "NetConnection.h"
#include <algorithm>
#include <iostream>
#include "StreamBackend.h"
#include "BinaryStream.h"
#include "NetMessage.h"

using namespace GAGCore;


	
//Uint32 NetConnection::lastTime = 0;
//Uint32 NetConnection::amount = 0;

NetConnection::NetConnection(const std::string& naddress, Uint16 port)
	: connect(incoming, incomingMutex)
{
	boost::thread thread(boost::ref(connect));
	connecting=false;
	openConnection(naddress, port);
}



NetConnection::NetConnection()
	: connect(incoming, incomingMutex)
{
	boost::thread thread(boost::ref(connect));
}



NetConnection::~NetConnection()
{
	boost::shared_ptr<NTExitThread> exitthread(new NTExitThread);
	connect.sendMessage(exitthread);
	while(!connect.hasThreadExited());
}


	
void NetConnection::openConnection(const std::string& connectaddress, Uint16 port)
{
	address = connectaddress;
	connecting=true;
	boost::shared_ptr<NTConnect> toconnect(new NTConnect(connectaddress, port));
	connect.sendMessage(toconnect);
}



void NetConnection::closeConnection()
{
	boost::shared_ptr<NTCloseConnection> close(new NTCloseConnection);
	connect.sendMessage(close);
}



bool NetConnection::isConnected()
{
	return connect.isConnected();
}



bool NetConnection::isConnecting()
{
	return connecting;
}



void NetConnection::update()
{
	boost::recursive_mutex::scoped_lock lock(incomingMutex);
	while(!incoming.empty())
	{
		boost::shared_ptr<NetConnectionThreadMessage> message = incoming.front();
		incoming.pop();
		Uint8 type = message->getMessageType();
		switch(type)
		{
			case NTMCouldNotConnect:
			{
				boost::shared_ptr<NTCouldNotConnect> info = static_pointer_cast<NTCouldNotConnect>(message);
				//std::cout<<"NetConnection::getMessage(): "<<info->format()<<std::endl;
				connecting=false;
			}
			break;
			case NTMConnected:
			{
				boost::shared_ptr<NTConnected> info = static_pointer_cast<NTConnected>(message);
				address = info->getIPAddress();
				//std::cout<<"NetConnection::getMessage(): "<<info->format()<<std::endl;
				connecting=false;
			}
			break;
			case NTMLostConnection:
			{
				boost::shared_ptr<NTLostConnection> info = static_pointer_cast<NTLostConnection>(message);
				//std::cout<<"NetConnection::getMessage(): "<<info->format()<<std::endl;
			}
			break;
			case NTMRecievedMessage:
			{
				boost::shared_ptr<NTRecievedMessage> info = static_pointer_cast<NTRecievedMessage>(message);
				recieved.push(info->getMessage());
				//std::cout<<"NetConnection::getMessage(): "<<info->format()<<std::endl;
				//std::cout<<"Recieved: "<<info->getMessage()->format()<<std::endl;
			}
			break;
		}
	}
}



shared_ptr<NetMessage> NetConnection::getMessage()
{
	update();

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
	//std::cout<<"Sending: "<<message->format()<<std::endl;
	boost::shared_ptr<NTSendMessage> close(new NTSendMessage(message));
	connect.sendMessage(close);
}



const std::string& NetConnection::getIPAddress() const
{
	return address;
}



bool NetConnection::attemptConnection(TCPsocket& serverSocket)
{
	TCPsocket socket=NULL;
	socket=SDLNet_TCP_Accept(serverSocket);
	if(socket)
	{
		IPaddress ip = *SDLNet_TCP_GetPeerAddress(socket);
		address = boost::lexical_cast<std::string>((ip.host >> 0 ) & 0xff) + "." +
		                 boost::lexical_cast<std::string>((ip.host >> 8 ) & 0xff) + "." +
		                 boost::lexical_cast<std::string>((ip.host >> 16) & 0xff) + "." +
		                 boost::lexical_cast<std::string>((ip.host >> 24) & 0xff);
		boost::shared_ptr<NTAcceptConnection> accept(new NTAcceptConnection(socket));
		connect.sendMessage(accept);
		while(connect.isConnected() == false)
			SDL_Delay(5);
		return true;
	}
	return false;
}
