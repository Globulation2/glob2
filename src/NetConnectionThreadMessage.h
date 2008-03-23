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

#ifndef NetConnectionThreadMessage_h
#define NetConnectionThreadMessage_h

#include <string>
#include "SDL_net.h"
#include <vector>
#include <boost/shared_ptr.hpp>

class NetMessage;

enum NetConnectionThreadMessageType
{
	NTMConnect,
	NTMCouldNotConnect,
	NTMConnected,
	NTMCloseConnection,
	NTMLostConnection,
	NTMRecievedMessage,
	NTMSendMessage,
	NTMAttemptConnection,
	NTMExitThread,
	//type_append_marker
};


///This class represents a message sent between the main thread and the thread that manages IRC
class NetConnectionThreadMessage
{
public:
	///Returns the event type
	virtual Uint8 getMessageType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two NetConnectionThreadMessage
	virtual bool operator==(const NetConnectionThreadMessage& rhs) const = 0;
};



///NTConnect
class NTConnect : public NetConnectionThreadMessage
{
public:
	///Creates a NTMConnect event
	NTConnect(std::string server, Uint16 port);

	///Returns NTMMConnect
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves server
	std::string getServer() const;

	///Retrieves port
	Uint16 getPort() const;
private:
	std::string server;
	Uint16 port;
};




///NTCouldNotConnect
class NTCouldNotConnect : public NetConnectionThreadMessage
{
public:
	///Creates a NTCouldNotConnect event
	NTCouldNotConnect(std::string error);

	///Returns NTMCouldNotConnect
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves error
	std::string getError() const;
private:
	std::string error;
};




///NTConnected
class NTConnected : public NetConnectionThreadMessage
{
public:
	///Creates a NTConnected event
	NTConnected();

	///Returns NTMConnected
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;
};




///NTCloseConnection
class NTCloseConnection : public NetConnectionThreadMessage
{
public:
	///Creates a NTCloseConnection event
	NTCloseConnection();

	///Returns NTMCloseConnection
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;
};




///NTLostConnection
class NTLostConnection : public NetConnectionThreadMessage
{
public:
	///Creates a NTLostConnection event
	NTLostConnection(std::string error);

	///Returns NTMLostConnection
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves error
	std::string getError() const;
private:
	std::string error;
};




///NTRecievedMessage
class NTRecievedMessage : public NetConnectionThreadMessage
{
public:
	///Creates a NTRecievedMessage event
	NTRecievedMessage(boost::shared_ptr<NetMessage> message);

	///Returns NTMRecievedMessage
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves message
	boost::shared_ptr<NetMessage> getMessage() const;
private:
	boost::shared_ptr<NetMessage> message;
};




///NTSendMessage
class NTSendMessage : public NetConnectionThreadMessage
{
public:
	///Creates a NTSendMessage event
	NTSendMessage(boost::shared_ptr<NetMessage> message);

	///Returns NTMSendMessage
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves message
	boost::shared_ptr<NetMessage> getMessage() const;
private:
	boost::shared_ptr<NetMessage> message;
};




///NTAttemptConnection
class NTAttemptConnection : public NetConnectionThreadMessage
{
public:
	///Creates a NTAttemptConnection event
	NTAttemptConnection(TCPsocket& socket);

	///Returns NTMAttemptConnection
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves socket
	TCPsocket& getSocket() const;
private:
	TCPsocket& socket;
};




///NTExitThread
class NTExitThread : public NetConnectionThreadMessage
{
public:
	///Creates a NTExitThread event
	NTExitThread();

	///Returns NTMExitThread
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;
};



//event_append_marker



#endif
