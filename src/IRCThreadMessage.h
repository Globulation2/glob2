// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef IRCThreadMessage_h
#define IRCThreadMessage_h

#include <string>
#include "SDL_net.h"
#include <vector>

enum IRCThreadMessageType
{
	ITMConnect,
	ITMDisconnect,
	ITMSendMessage,
	ITMDisconnected,
	ITMRecieveMessage,
	ITMJoinChannel,
	ITMExitThread,
	ITMUserListModified,
	//type_append_marker
};

///This class represents a message sent between the main thread and the thread that manages IRC
class IRCThreadMessage
{
public:
	virtual ~IRCThreadMessage() {}

	///Returns the event type
	virtual Uint8 getMessageType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two IRCThreadMessageType
	virtual bool operator==(const IRCThreadMessage& rhs) const = 0;
};




///ITConnect
class ITConnect : public IRCThreadMessage
{
public:
	///Creates a ITConnect event
	ITConnect(std::string server, std::string nick, int serverPort);

	///Returns ITMConnect
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;

	///Retrieves server
	std::string getServer() const;

	///Retrieves nick
	std::string getNick() const;

	///Retrieves serverPort
	int getServerPort() const;
private:
	std::string server;
	std::string nick;
	int serverPort;
};




///ITDisconnect
class ITDisconnect : public IRCThreadMessage
{
public:
	///Creates a ITDisconnect event
	ITDisconnect();

	///Returns ITMDisconnect
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;
};




///ITSendMessage
class ITSendMessage : public IRCThreadMessage
{
public:
	///Creates a ITSendMessage event
	ITSendMessage(std::string text);

	///Returns ITMSend
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;

	///Retrieves text
	std::string getText() const;
private:
	std::string text;
};




///ITDisconnected
class ITDisconnected : public IRCThreadMessage
{
public:
	///Creates a ITDisconnected event
	ITDisconnected();

	///Returns ITMDisconnected
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;
};




///ITRecieveMessage
class ITRecieveMessage : public IRCThreadMessage
{
public:
	///Creates a ITRecieveMessage event
	ITRecieveMessage(std::string message);

	///Returns ITMRecieve
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;

	///Retrieves message
	std::string getMessage() const;
private:
	std::string message;
};




///ITJoinChannel
class ITJoinChannel : public IRCThreadMessage
{
public:
	///Creates a ITJoinChannel event
	ITJoinChannel(std::string channel);

	///Returns ITMJoinChannel
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;

	///Retrieves channel
	std::string getChannel() const;
private:
	std::string channel;
};




///ITExitThread
class ITExitThread : public IRCThreadMessage
{
public:
	///Creates a ITExitThread event
	ITExitThread();

	///Returns ITMExitThread
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;
};




///ITUserListModified
class ITUserListModified : public IRCThreadMessage
{
public:
	///Creates a ITUserListModified event
	ITUserListModified();

	///Returns ITMUserListModified
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two IRCThreadMessage
	bool operator==(const IRCThreadMessage& rhs) const;

	///Adds a user to the list of users in this message
	void addUser(const std::string& user);

	///Returns the list of users
	std::vector<std::string>& getUsers();
private:
	std::vector<std::string> users;
};



//event_append_marker



#endif
