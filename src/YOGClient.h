/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __YOGClient_h
#define __YOGClient_h

#include "NetMessage.h"
#include "NetConnection.h"
#include "NetListener.h"
#include "YOGConsts.h"
#include "YOGGameInfo.h"
#include "YOGPlayerInfo.h"
#include "YOGMessage.h"

///This represents the players YOG client, connecting to the YOG server.
class YOGClient
{
public:
	///Initializes and attempts to connect to server.
	YOGClient(const std::string& server);
	
	///Initializes the client as empty
	YOGClient();
	
	///Attempts a connection to server.
	void connect(const std::string& server);

	///Updates the client. This parses and interprets any incoming messages.
	void update();
	
	///This defines the current state of the connection. There are many states,
	///due to the asychronous design.
	enum ConnectionState
	{
		//This signified unconnected.
		NotConnected,
		///This is the starting state
		NeedToSendClientInformation,
		///This means that the client is waiting to recieve server information
		WaitingForServerInformation,
		///This means that the YOGClient is waiting for an external force to send
		///login information to the YOGClient. This would usually be the GUI that
		///is managing it. This means a call to attemptLogin or attemptRegistration
		WaitingForLoginInformation,
		///This means that the client is waiting for a reply from a login attempt.
		WaitingForLoginReply,
		///This means that the client is waiting for a reply from a registration attempt.
		WaitingForRegistrationReply,
		///This means that the client is waiting for a game list to be sent
		WaitingForGameList,
		///This means the client is on standby, waiting for the user to ask
		///for some input
		ClientOnStandby,
	};
	
	///This returns the current connection state. This state includes both internal and external
	ConnectionState getConnectionState() const;
	
	///This will return the current login policy used by the server, if its known.
	///When unknown, this will return YOGUnknownLoginPolicy
	YOGLoginPolicy getLoginPolicy() const;
	
	///This will return the current game policy used by the server, if its known.
	///When unknown, this will return YOGUnknownGamePolicy
	YOGGamePolicy getGamePolicy() const;

	///This will attempt a login with the provided login information. The password is not
	///mandatory. If the login policy is YOGAnonymousLogin, then the password will simply
	///be ignored. Login attempts should be done when the client is in the
	///WaitingForLoginInformation state.
	void attemptLogin(const std::string& username, const std::string& password = "");
	
	///This all attempt to register a new user with the provided login information.
	///The password is mandatory. After a successful register, the client is
	///considered logged-in and does not need to attempt a login
	void attemptRegistration(const std::string& username, const std::string& password);
	
	///This will return the login state. When this is unknown (it hasn't recieved a reply
	///yet), this returns YOGLoginUnknown. In the case when there has been multiple
	///attempts at a login (or registration), this returns the state of the most recent attempt that has
	///gotten a reply.
	YOGLoginState getLoginState() const;
	
	///This will return the list of games on hosted on the server.
	const std::list<YOGGameInfo>& getGameList() const;

	///This will return the list of players on the server
	const std::list<YOGPlayerInfo>& getPlayerList() const;

	///This will send for a manual update of the game list,
	void requestGameListUpdate();
	
	///This will send for a manual update of the player list,
	void requestPlayerListUpdate();

	///Returns true if the list of games has been changed since the last call to
	///this function. Defaults to false, untill the the game list is first initiated
	///from the server
	bool hasGameListChanged();

	///Returns true if the list of players has been changed since the last call to
	///this function. Defaults to false, untill the the player list is first initiated
	///from the server
	bool hasPlayerListChanged();

	///This will disconnect the client and server
	void disconnect();

	///This sends a message to the server to remove the game that the player is connected to.
	///This will only be accepted if it is from the host of the game.
	void removeGame();

	///This will send a message indicating the game the host is hosting has begun.
	void gameHasStarted();

	///This will send a message indicating the game the host is hosting has finished
	void gameHasFinished();

	///Sends a message through YOG
	void sendMessage(boost::shared_ptr<YOGMessage> message);

	///Returns a new YOG message if there are any, and returns NULL otherwise
	boost::shared_ptr<YOGMessage> getMessage();


private:
	NetConnection& nc;

	Uint32 connectionState;
	
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
	YOGLoginState loginState;
	
	std::list<YOGGameInfo> games;
	std::list<YOGPlayerInfo>& players;
	bool gameListChanged;
	bool playerListChanged;
};



#endif
