/*
  Standalone IRC client
  Copyright (C) 2001-2004 Stephane Magnenat
  for any question or comment contact me at nct@ysagoon.com

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

#ifndef __IRC_H
#define __IRC_H

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_net.h>
#else
#include <Types.h>
#endif

#include <deque>
#include <vector>

#ifdef WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#endif


//! This class is an IRC client
class IRC
{
public:
	enum IRCConst
	{
		IRC_CHANNEL_SIZE = 200,
		IRC_MESSAGE_SIZE=512,
		IRC_NICK_SIZE=9
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
		char source[IRC_NICK_SIZE+1];
		char diffusion[IRC_CHANNEL_SIZE+1];
		char message[IRC_MESSAGE_SIZE+1];
		
		ChatMessage::ChatMessage() { source[0]=0; diffusion[0]=0; message[0]=0; }
	};

	struct InfoMessage
	{
		InfoMessageType type;
		char source[IRC_NICK_SIZE+1];
		char diffusion[IRC_CHANNEL_SIZE+1];
		char message[IRC_MESSAGE_SIZE+1];
		
		InfoMessage::InfoMessage(InfoMessageType t) { type=t; source[0]=0; diffusion[0]=0; message[0]=0; }
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
	
	//! The chat where default chat will go
	char chatChan[IRC_CHANNEL_SIZE+1];
	//! copy of the nick
	char nick[IRC_NICK_SIZE+1];

protected:
	//! Interprete a message from IRC; do parsing etc
	void interpreteIRCMessage(const char *message);
	//! Force disconnect (kill socket)
	void forceDisconnect(void);

public:
	//! Constructor
	IRC();
	//! Destructor
	virtual ~IRC();

	// CONNECTION
	//! Connect to YOG server (IRC network), return true on sucess
	bool connect(const char *serverName, int serverPort, const char *nick);
	//! Try to disconnect from server in a cleany way
	bool disconnect(void);

	// RUN
	//! Do a YOG step
	void step(void);

	// CHAT
	//! Return true if there is pending message
	bool isChatMessage(void);
	//! Get message
	const char *getChatMessage(void);
	//! Get the user from where the message is
	const char *getChatMessageSource(void);
	//! Get where the message has been spawned
	const char *getChatMessageDiffusion(void);
	//! Free last message
	void freeChatMessage(void);
	//! Send a message (or a command), return true if command has been sent
	void sendCommand(const char *message);
	//! Set the chat channel
	void setChatChannel(const char *chan);

	// INFO
	//! Return true if there is pending info message
	bool isInfoMessage(void);
	//! Get Info info message type
	const InfoMessageType getInfoMessageType(void);
	//! Get the associated nick with info message
	const char *getInfoMessageSource(void);
	//! Get where the info message has been spawned
	const char *getInfoMessageDiffusion(void);
	//! Get the associated nick with info message
	const char *getInfoMessageText(void);
	//! Free last info message
	void freeInfoMessage(void);

	//! Join a given channel, if no argument is given, join chat channel
	void joinChannel(const char *channel=NULL);
	//! Quit a given channel, if no argument is given, leave chat channel
	void leaveChannel(const char *channel=NULL);

private:
	//! Send a string in IRC format
	bool sendString(char *data);
	//! Get a string in IRC format
	bool getString(char data[IRC_MESSAGE_SIZE]);
};

#endif
