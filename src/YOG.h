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

//! This class is a YOG client
class YOG
{
public:
	enum { IRC_MESSAGE_SIZE=512 };

private:
	//! the socket that we use to connect to IRC
	TCPsocket socket;
	//! the set for our socket; used to know if new data are available
	SDLNet_SocketSet socketSet;

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
	char *getChatMessage(void);
	//! Get the channel from where the message is
	char *getChatMessageChannel(void);
	//! Free last message
	void freeChatMessage(void);
	//! Send a message to channel
	void sendChatMessage(const char *message, const char *channel="#yog");

	//! Join a given channel
	void joinChannel(const char *channel="#yog");
	//! Quit a given channel
	void quitChannel(const char *channel="#yog");

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
	char *getYOGGame(void);
	//! Free last game parameters
	void freeYOGGame(void);

private:
	//! Send a string in IRC format
	bool sendString(char *data);
	//! Get a string in IRC format
	bool getString(char data[IRC_MESSAGE_SIZE]);
};

#endif
