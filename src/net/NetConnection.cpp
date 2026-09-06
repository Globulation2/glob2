// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetConnection.h"
#include <string>
#include <functional>
#include "NetMessage.h"

using namespace GAGCore;
using std::static_pointer_cast;
using std::shared_ptr;


	
NetConnection::NetConnection(const std::string& naddress, Uint16 port)
	: connect(incoming, incomingMutex)
{
	connectThread = std::thread(std::ref(connect));
	connecting=false;
	openConnection(naddress, port);
}



NetConnection::NetConnection()
	: connect(incoming, incomingMutex)
{
	connectThread = std::thread(std::ref(connect));
}



NetConnection::~NetConnection()
{
	std::shared_ptr<NTExitThread> exitthread(new NTExitThread);
	connect.sendMessage(exitthread);
	if (connectThread.joinable())
		connectThread.join();
}


	
void NetConnection::openConnection(const std::string& connectaddress, Uint16 port)
{
	address = connectaddress;
	connecting=true;
	std::shared_ptr<NTConnect> toconnect(new NTConnect(connectaddress, port));
	connect.sendMessage(toconnect);
}



void NetConnection::closeConnection()
{
	std::shared_ptr<NTCloseConnection> close(new NTCloseConnection);
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
	std::lock_guard<std::recursive_mutex> lock(incomingMutex);
	while(!incoming.empty())
	{
		std::shared_ptr<NetConnectionThreadMessage> message = incoming.front();
		incoming.pop();
		Uint8 type = message->getMessageType();
		switch(type)
		{
			case NTMCouldNotConnect:
			{
				std::shared_ptr<NTCouldNotConnect> info = static_pointer_cast<NTCouldNotConnect>(message);
				connecting=false;
			}
			break;
			case NTMConnected:
			{
				std::shared_ptr<NTConnected> info = static_pointer_cast<NTConnected>(message);
				address = info->getIPAddress();
				connecting=false;
			}
			break;
			case NTMLostConnection:
			{
				std::shared_ptr<NTLostConnection> info = static_pointer_cast<NTLostConnection>(message);
			}
			break;
			case NTMRecievedMessage:
			{
				std::shared_ptr<NTRecievedMessage> info = static_pointer_cast<NTRecievedMessage>(message);
				recieved.push(info->getMessage());
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
	std::shared_ptr<NTSendMessage> close(new NTSendMessage(message));
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
		address = std::to_string((ip.host >> 0 ) & 0xff) + "." +
		                 std::to_string((ip.host >> 8 ) & 0xff) + "." +
		                 std::to_string((ip.host >> 16) & 0xff) + "." +
		                 std::to_string((ip.host >> 24) & 0xff);
		std::shared_ptr<NTAcceptConnection> accept(new NTAcceptConnection(socket));
		connect.sendMessage(accept);
		while(connect.isConnected() == false)
			SDL_Delay(5);
		return true;
	}
	return false;
}
