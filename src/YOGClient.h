/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __YOGClient_h
#define __YOGClient_h

#include "NetMessage.h"
#include "NetConnection.h"
#include "NetListener.h"
#include "YOGConsts.h"
#include "YOGGameInfo.h"
#include "YOGPlayerInfo.h"
#include "YOGMessage.h"
#include "YOGEventListener.h"
#include <list>
#include "YOGGameServer.h"
#include "YOGChatChannel.h"
#include <map>


class MultiplayerGame;
class MapAssembler;

///This represents the players YOG client, connecting to the YOG server.
class YOGClient
{
public:
	///Initializes and attempts to connect to server.
	YOGClient(const std::string& server);
	
	///Initializes the client as empty
	YOGClient();
	
	///Initializes the client as empty
	void initialize();
	
	///Attempts a connection to server.
	void connect(const std::string& server);

	///Returns whether the client is still connected
	bool isConnected();

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
	
	///This will return the playerID of the current connection
	Uint16 getPlayerID() const;

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

	///This will find the name of the player with the given ID
	std::string findPlayerName(Uint16 playerID);

	///This will send for a manual update of the game list,
	void requestGameListUpdate();
	
	///This will send for a manual update of the player list,
	void requestPlayerListUpdate();

	///Returns true if the list of games has been changed since the last call to
	///this function. Defaults to false, untill the the game list is first initiated
	///from the server
	bool hasGameListChanged();

	///This will disconnect the client and server
	void disconnect();

	///Returns the username for the player	
	std::string getUsername() const;

	///Sends the message to create a new game with the given game name to the server
	void createGame(const std::string& name);

	///Assocciattes the provided MultiplayerGame with this connection
	void setMultiplayerGame(boost::shared_ptr<MultiplayerGame> game);

	///Returns the assocciatted MultiplayerGame
	boost::shared_ptr<MultiplayerGame> getMultiplayerGame();

	///Sets the map assembler for this connection
	void setMapAssembler(boost::shared_ptr<MapAssembler> assembler);
	
	///Returns the map assembler for this connection
	boost::shared_ptr<MapAssembler> getMapAssembler();

	///This sets the event listener, which will recieve all subsequent events.
	///Does not take ownership of the object
	void setEventListener(YOGEventListener* listener);

	///This attaches a game server to this client, for client-hosted games (such as LAN)
	void attachGameServer(boost::shared_ptr<YOGGameServer> server);

protected:
    friend class MultiplayerGame;
    friend class MapAssembler;
	friend class YOGChatChannel;
    
    ///Sends a message on behalf of the assocciatted MultiplayerGame or YOGChatChannel
    void sendNetMessage(boost::shared_ptr<NetMessage> message);


	///Adds a new YOGChatChannel to recieve chat events (done by YOGChatChannel itself)
	void addYOGChatChannel(YOGChatChannel* channel);

	///Removes the YOGChatChannel (done by YOGChatChannel itself)
	void removeYOGChatChannel(YOGChatChannel* channel);

private:
	std::string username;
	Uint16 playerID;

	bool wasConnected;

	NetConnection nc;

	ConnectionState connectionState;
	
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
	YOGLoginState loginState;
	
	std::list<YOGGameInfo> games;
	std::list<YOGPlayerInfo> players;
	std::map<Uint32, YOGChatChannel*> chatChannels;
	
	boost::shared_ptr<MultiplayerGame> joinedGame;
	boost::shared_ptr<MapAssembler> assembler;
	YOGEventListener* listener;

	boost::shared_ptr<YOGGameServer> server;

};



#endif
