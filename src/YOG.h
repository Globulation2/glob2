/*
    Copyright (C) 2001, 2002 Stephane Magnenat
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __YOG_H
#define __YOG_H

#include "Header.h"
#include <deque>

#define DEFAULT_CHAT_CHAN "#yog"

//! This class is a YOG client
class YOG
{
public:
	enum { IRC_CHANNEL_SIZE = 200, IRC_MESSAGE_SIZE=512, IRC_NICK_SIZE=9 };
	enum InfoMessage
	{
		IRC_MSG_JOIN=0
	};

	struct ChatMessage
	{
		char message[IRC_MESSAGE_SIZE];
		char source[IRC_NICK_SIZE];
		char diffusion[IRC_CHANNEL_SIZE];
	};

protected:
	//! the socket that we use to connect to IRC
	TCPsocket socket;
	//! the set for our socket; used to know if new data are available
	SDLNet_SocketSet socketSet;

	//! pending messages
	deque<ChatMessage> messages;

protected:
	//! Interprete a message from IRC; do parsing etc
	void interpreteIRCMessage(const char *message);

public:
	//! YOG Constructor
	YOG();
	//! YOG Destructor
	virtual ~YOG();

	// CONNECTION
	//! Connect to YOG server (IRC network), return true on sucess
	bool connect(const char *serverName, int serverPort, const char *nick);
	//! Try to disconnect from server in a cleany way
	bool disconnect(void);
	//! Force disconnect (kill socket)
	void forceDisconnect(void);

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
	const char *getMessageDiffusion(void);
	//! Free last message
	void freeChatMessage(void);
	//! Send a message (or a command)
	void sendCommand(const char *message);

	// INFO
	//! Return true if there is pending info message
	bool isInfoMessage(void);
	//! Get Info message type
	const InfoMessage *getInfoMessageType(void);
	//! Get Info message associated text
	const char *getInfoMessageText(void);
	//! Get the channel (or the user) from where the message is
	const char *getInfoMessageSource(void);
	//! Free last message
	void freeInfoMessage(void);

	//! Join a given channel
	void joinChannel(const char *channel=DEFAULT_CHAT_CHAN);
	//! Quit a given channel
	void quitChannel(const char *channel=DEFAULT_CHAT_CHAN);

	// GAME
	//! Create a new game and start the login room
	void createGame(void);
	//! Start the game
	void startGame(void);
	//! Set the game parameters
	void setGameParameters(const char *gameIdentifier, const char *gameInfo);

	//! Request all game for software gameIdentifier in YOG
	void requestYOGGames(const char *gameIdentifier);
	//! Return true if there is games pending
	bool isYOGGame(void);
	//! Get parameters from pending game
	const char *getYOGGame(void);
	//! Free last game parameters
	void freeYOGGame(void);

private:
	//! Send a string in IRC format
	bool sendString(char *data);
	//! Get a string in IRC format
	bool getString(char data[IRC_MESSAGE_SIZE]);
};

#endif
