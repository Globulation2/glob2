/*
  Standalone IRC client
  Copyright (C) 2001-2004 Stephane Magnenat
  for any question or comment contact me at <stephane at magnenat dot net>

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

#ifndef __IRC_H
#define __IRC_H

#include <SDL_net.h>

#include <deque>
#include <vector>
#include <set>
#include <string>
#include <map>

#ifdef WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#endif


//! This class is an IRC client
class IRC
{
	// STATUS nct 20060315 : mostly clean, some string cleanup still welcome though
	static const bool verbose = false;

public:
	enum IRCConst
	{
		IRC_CHANNEL_SIZE = 200,
		IRC_MESSAGE_SIZE = 512,
		//! we are using oftc, which says its nicklen is 30
		IRC_NICK_SIZE = 30
	};

	enum InfoMessageType
	{
		IRC_MSG_NONE=0,
		IRC_MSG_JOIN,
		IRC_MSG_PART,
		IRC_MSG_QUIT,
		IRC_MSG_MODE,
		IRC_MSG_NOTICE
	};

	struct ChatMessage
	{
		std::string source;
		std::string diffusion;
		std::string message;

		ChatMessage() { source[0] = 0; diffusion[0] = 0; message[0] = 0; }
	};

	struct InfoMessage
	{
		InfoMessageType type;
		std::string source;
		std::string diffusion;
		std::string message;

		InfoMessage(InfoMessageType t) { type=t; source[0] = 0; diffusion[0] = 0; message[0] = 0; }
	};

protected:
	//! the socket that we use to connect to IRC
	TCPsocket socket;
	//! the set for our socket; used to know if new data are available
	SDLNet_SocketSet socketSet;

	//! pending messages
	std::deque<ChatMessage> messages;

	//! pending info messages
	std::deque<InfoMessage> infoMessages;

	//! all users on each connected channels
	std::map<std::string, std::set<std::string> > usersOnChannels;
	//! true if usersOnChannels has been modified
	bool usersOnChannelsModified;
	//! iterator after the last channel user
	std::set<std::string>::const_iterator endChannelUser;
	//! iterator to the next channel user
	std::set<std::string>::const_iterator nextChannelUser;

	//! The chat where default chat will go
	std::string chatChan;
	//! copy of the nick
	std::string nick;

protected:
	//! Interprete a message from IRC; do parsing etc
	void interpreteIRCMessage(const std::string &message);
	//! Force disconnect (kill socket)
	void forceDisconnect(void);

public:
	//! Constructor
	IRC();
	//! Destructor
	virtual ~IRC();

	// CONNECTION
	//! Connect to YOG server (IRC network), return true on sucess
	bool connect(const std::string &serverName, int serverPort, const std::string &nick);
	//! Try to disconnect from server in a cleany way
	bool disconnect(void);

	// RUN
	//! Do a YOG step
	void step(void);

	// CHAT
	//! Return true if there is pending message
	bool isChatMessage(void);
	//! Get message
	const std::string &getChatMessage(void);
	//! Get the user from where the message is
	const std::string &getChatMessageSource(void);
	//! Get where the message has been spawned
	const std::string &getChatMessageDiffusion(void);
	//! Free last message
	void freeChatMessage(void);
	//! Send a message (or a command), return true if command has been sent
	void sendCommand(const std::string &message);
	//! Set the chat channel
	void setChatChannel(const std::string &chan);

	// INFO
	//! Return true if there is pending info message
	bool isInfoMessage(void);
	//! Get Info info message type
	const InfoMessageType getInfoMessageType(void);
	//! Get the associated nick with info message
	const std::string &getInfoMessageSource(void);
	//! Get where the info message has been spawned
	const std::string &getInfoMessageDiffusion(void);
	//! Get the associated nick with info message
	const std::string &getInfoMessageText(void);
	//! Free last info message
	void freeInfoMessage(void);

	// CHANNEL
	//! Join a given channel, if no argument is given, join chat channel
	void joinChannel(const std::string &channel);
	//! Quit a given channel, if no argument is given, leave chat channel
	void leaveChannel(const std::string &channel);
	//! Init current channel iteration for listing users. Return if the channel exists. Iteration has to be completed (iterated until isMoreChannelUser return false) before calling step.
	bool initChannelUserListing(const std::string &channel);
	//! Return if there is user to be iterated on the current channel iterator. initChannelUserListing has to be called once before calling this method.
	bool isMoreChannelUser(void);
	//! Return the next channel user on the current channel iterator. initChannelUserListing has to be called once before calling this method. Returned string has to be copied before next call to this function or next call to initChannelUserListing or next call to step.
	const std::string &getNextChannelUser(void);
	//! Returns true if user on channel list has been modified since last call. Reset the modification flag to false.
	bool isChannelUserBeenModified(void);

private:
	//! Send a string in IRC format
	bool sendString(const std::string &data);
	//! Get a string in IRC format
	bool getString(char data[IRC_MESSAGE_SIZE]);
};

#endif
