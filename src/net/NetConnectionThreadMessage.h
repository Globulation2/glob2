// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <string>
#include "SDL_net.h"
#include <vector>
#include <memory>

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
	NTMAcceptConnection,
	NTMExitThread,
	//type_append_marker
};


///This class represents a message sent between the main thread and the thread that manages IRC
class NetConnectionThreadMessage
{
public:
	///Destructor
	virtual ~NetConnectionThreadMessage() {}

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
	NTConnected(const std::string& ip);

	///Returns NTMConnected
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;
	
	///Returns the ip address of the connection
	const std::string& getIPAddress();
private:
	std::string ip;
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
	NTRecievedMessage(std::shared_ptr<NetMessage> message);

	///Returns NTMRecievedMessage
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves message
	std::shared_ptr<NetMessage> getMessage() const;
private:
	std::shared_ptr<NetMessage> message;
};




///NTSendMessage
class NTSendMessage : public NetConnectionThreadMessage
{
public:
	///Creates a NTSendMessage event
	NTSendMessage(std::shared_ptr<NetMessage> message);

	///Returns NTMSendMessage
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves message
	std::shared_ptr<NetMessage> getMessage() const;
private:
	std::shared_ptr<NetMessage> message;
};




///NTAcceptConnection
class NTAcceptConnection : public NetConnectionThreadMessage
{
public:
	///Creates a NTAcceptConnection event
	NTAcceptConnection(TCPsocket& socket);

	///Returns NTMAcceptConnection
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const NetConnectionThreadMessage& rhs) const;

	///Retrieves socket
	TCPsocket getSocket() const;
private:
	TCPsocket socket;
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
