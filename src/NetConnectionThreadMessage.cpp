/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "NetConnectionThreadMessage.h"
#include <sstream>
#include <typeinfo>
#include "NetMessage.h"


NTConnect::NTConnect(std::string server, Uint16 port)
	: server(server), port(port)
{
}



Uint8 NTConnect::getMessageType() const
{
	return NTMConnect;
}



std::string NTConnect::format() const
{
	std::ostringstream s;
	s<<"NTMConnect("<<"server="<<server<<"; "<<"port="<<port<<"; "<<")";
	return s.str();
}



bool NTConnect::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTConnect))
	{
		const NTConnect& r = dynamic_cast<const NTConnect&>(rhs);
		if(r.server == server && r.port == port)
			return true;
	}
	return false;
}


std::string NTConnect::getServer() const
{
	return server;
}



Uint16 NTConnect::getPort() const
{
	return port;
}



NTCouldNotConnect::NTCouldNotConnect(std::string error)
	: error(error)
{
}



Uint8 NTCouldNotConnect::getMessageType() const
{
	return NTMCouldNotConnect;
}



std::string NTCouldNotConnect::format() const
{
	std::ostringstream s;
	s<<"NTCouldNotConnect("<<"error="<<error<<"; "<<")";
	return s.str();
}



bool NTCouldNotConnect::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTCouldNotConnect))
	{
		const NTCouldNotConnect& r = dynamic_cast<const NTCouldNotConnect&>(rhs);
		if(r.error == error)
			return true;
	}
	return false;
}


std::string NTCouldNotConnect::getError() const
{
	return error;
}



NTConnected::NTConnected(const std::string& ip)
	: ip(ip)
{
}



Uint8 NTConnected::getMessageType() const
{
	return NTMConnected;
}



std::string NTConnected::format() const
{
	std::ostringstream s;
	s<<"NTConnected(ip="<<ip<<";)";
	return s.str();
}



const std::string& NTConnected::getIPAddress()
{
	return ip;
}



bool NTConnected::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTConnected))
	{
		//const NTConnected& r = dynamic_cast<const NTConnected&>(rhs);
		return true;
	}
	return false;
}


NTCloseConnection::NTCloseConnection()
{
}



Uint8 NTCloseConnection::getMessageType() const
{
	return NTMCloseConnection;
}



std::string NTCloseConnection::format() const
{
	std::ostringstream s;
	s<<"NTCloseConnection()";
	return s.str();
}



bool NTCloseConnection::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTCloseConnection))
	{
		//const NTCloseConnection& r = dynamic_cast<const NTCloseConnection&>(rhs);
		return true;
	}
	return false;
}


NTLostConnection::NTLostConnection(std::string error)
	: error(error)
{
}



Uint8 NTLostConnection::getMessageType() const
{
	return NTMLostConnection;
}



std::string NTLostConnection::format() const
{
	std::ostringstream s;
	s<<"NTLostConnection("<<"error="<<error<<"; "<<")";
	return s.str();
}



bool NTLostConnection::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTLostConnection))
	{
		const NTLostConnection& r = dynamic_cast<const NTLostConnection&>(rhs);
		if(r.error == error)
			return true;
	}
	return false;
}


std::string NTLostConnection::getError() const
{
	return error;
}



NTRecievedMessage::NTRecievedMessage(boost::shared_ptr<NetMessage> message)
	: message(message)
{
}



Uint8 NTRecievedMessage::getMessageType() const
{
	return NTMRecievedMessage;
}



std::string NTRecievedMessage::format() const
{
	std::ostringstream s;
	s<<"NTRecievedMessage("<<"message->format()="<<message->format()<<"; "<<")";
	return s.str();
}



bool NTRecievedMessage::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTRecievedMessage))
	{
		const NTRecievedMessage& r = dynamic_cast<const NTRecievedMessage&>(rhs);
		if(r.message == message)
			return true;
	}
	return false;
}


boost::shared_ptr<NetMessage> NTRecievedMessage::getMessage() const
{
	return message;
}



NTSendMessage::NTSendMessage(boost::shared_ptr<NetMessage> message)
	: message(message)
{
}



Uint8 NTSendMessage::getMessageType() const
{
	return NTMSendMessage;
}



std::string NTSendMessage::format() const
{
	std::ostringstream s;
	s<<"NTSendMessage("<<"message->format()="<<message->format()<<"; "<<")";
	return s.str();
}



bool NTSendMessage::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTSendMessage))
	{
		const NTSendMessage& r = dynamic_cast<const NTSendMessage&>(rhs);
		if(r.message == message)
			return true;
	}
	return false;
}


boost::shared_ptr<NetMessage> NTSendMessage::getMessage() const
{
	return message;
}



NTAcceptConnection::NTAcceptConnection(TCPsocket& socket)
	: socket(socket)
{
}



Uint8 NTAcceptConnection::getMessageType() const
{
	return NTMAcceptConnection;
}



std::string NTAcceptConnection::format() const
{
	std::ostringstream s;
	s<<"NTAcceptConnection()";
	return s.str();
}



bool NTAcceptConnection::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTAcceptConnection))
	{
		const NTAcceptConnection& r = dynamic_cast<const NTAcceptConnection&>(rhs);
		if(r.socket == socket)
			return true;
	}
	return false;
}


TCPsocket NTAcceptConnection::getSocket() const
{
	return socket;
}



NTExitThread::NTExitThread()
{
}



Uint8 NTExitThread::getMessageType() const
{
	return NTMExitThread;
}



std::string NTExitThread::format() const
{
	std::ostringstream s;
	s<<"NTExitThread()";
	return s.str();
}



bool NTExitThread::operator==(const NetConnectionThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(NTExitThread))
	{
		//const NTExitThread& r = dynamic_cast<const NTExitThread&>(rhs);
		return true;
	}
	return false;
}


//code_append_marker
