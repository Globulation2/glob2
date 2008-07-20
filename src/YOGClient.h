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

#include "NetConnection.h"
#include "YOGConsts.h"
#include <list>
#include <map>

class MultiplayerGame;
class YOGClientFileAssembler;
class P2PConnection;
class YOGClientGameListManager;
class YOGClientPlayerListManager;
class YOGServer;
class YOGClientChatChannel;
class YOGClientEventListener;
class YOGClientEvent;
class YOGClientBlockedList;
class YOGClientCommandManager;
class YOGClientMapUploader;
class YOGClientDownloadableMapList;
class YOGClientMapDownloader;
class YOGClientRatedMapList;

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

	///Returns whether the client is still connected
	bool isConnecting();

	///Updates the client. This parses and interprets any incoming messages.
	void update();
	
	///This returns the current ip address
	const std::string& getIPAddress() const;
	
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

	///Sets a file assembler for the given id
	void setYOGClientFileAssembler(Uint16 fileID, boost::shared_ptr<YOGClientFileAssembler> assembler);
	
	///Returns the map assembler for this connection
	boost::shared_ptr<YOGClientFileAssembler> getYOGClientFileAssembler(Uint16 fileID);

	///This attaches a game server to this client, for client-hosted games (such as LAN)
	void attachGameServer(boost::shared_ptr<YOGServer> server);

	///This retrieves the attached game server
	boost::shared_ptr<YOGServer> getGameServer();

	///This attaches a P2PConnection to this client
	void setP2PConnection(boost::shared_ptr<P2PConnection> connection);
	
	///This retrieves the attached P2P connection
	boost::shared_ptr<P2PConnection> getP2PConnection();

	///This attaches a YOGClientGameListManager to this client
	void setGameListManager(boost::shared_ptr<YOGClientGameListManager> gameListManager);

	///This retrieves the YOGClientGameListManager of this client
	boost::shared_ptr<YOGClientGameListManager> getGameListManager();

	///This attaches a YOGClientPlayerListManager to this client
	void setPlayerListManager(boost::shared_ptr<YOGClientPlayerListManager> playerListManager);

	///This retrieves the YOGClientGameListManager of this client
	boost::shared_ptr<YOGClientPlayerListManager> getPlayerListManager();

	///This adds an event listener
	void addEventListener(YOGClientEventListener* listener);

	///This removes an event listenr
	void removeEventListener(YOGClientEventListener* listener);

	///This attaches a NetConnection to this client for the game-router connection
	void setGameConnection(boost::shared_ptr<NetConnection> gameConnection);

	///This retrieves the NetConnection of this clients game-router connection
	boost::shared_ptr<NetConnection> getGameConnection();

	///This retrieves the YOGClientBlockedList of this client
	boost::shared_ptr<YOGClientBlockedList> getBlockedList();

	///This retrieves the YOGClientCommandManager of this client
	boost::shared_ptr<YOGClientCommandManager> getCommandManager();

	///This retrieves the YOGClientMapUploader of this client
	YOGClientMapUploader* getMapUploader();

	///This sets the YOGClientMapUploader of this client
	void setMapUploader(YOGClientMapUploader* uploader);

	///This returns the YOGClientDownloadableMapList
	boost::shared_ptr<YOGClientDownloadableMapList> getDownloadableMapList();

	///This returns the YOGClientRatedMapList
	boost::shared_ptr<YOGClientRatedMapList> getRatedMapList();

	///This sets the YOGClientMapDownloader of this client
	void setMapDownloader(YOGClientMapDownloader* downloader);
	
	///This returns the YOGClientMapDownloader of this client
	YOGClientMapDownloader* getMapDownloader();

protected:
    friend class MultiplayerGame;
    friend class YOGClientFileAssembler;
    friend class P2PConnection;
	friend class YOGClientChatChannel;
	friend class MultiplayerGamePlayerManager;
	friend class NetEngine;
	friend class YOGClientGameListManager;
	friend class YOGClientMapUploader;
	friend class YOGClientMapDownloadScreen;
	friend class YOGClientDownloadableMapList;
	friend class YOGClientMapDownloader;
    
    ///Sends a message on behalf of the assocciatted MultiplayerGame or YOGClientChatChannel
    void sendNetMessage(boost::shared_ptr<NetMessage> message);

	///Adds a new YOGClientChatChannel to recieve chat events (done by YOGClientChatChannel itself)
	void addYOGClientChatChannel(YOGClientChatChannel* channel);

	///Removes the YOGClientChatChannel (done by YOGClientChatChannel itself)
	void removeYOGClientChatChannel(YOGClientChatChannel* channel);

	///This sends an event to all the listeners
	void sendToListeners(boost::shared_ptr<YOGClientEvent> event);

private:
	std::string username;
	Uint16 playerID;

	bool wasConnected;
	bool wasConnecting;

	NetConnection nc;

	ConnectionState connectionState;
	
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
	YOGLoginState loginState;
	
	std::map<Uint32, YOGClientChatChannel*> chatChannels;
	
	boost::shared_ptr<MultiplayerGame> joinedGame;
	std::map<Uint16, boost::shared_ptr<YOGClientFileAssembler> > assembler;
	boost::shared_ptr<P2PConnection> p2pconnection;
	boost::shared_ptr<YOGClientGameListManager> gameListManager;
	boost::shared_ptr<YOGClientPlayerListManager> playerListManager;
	boost::shared_ptr<NetConnection> gameConnection;
	boost::shared_ptr<YOGClientBlockedList> blocked;
	boost::shared_ptr<YOGClientCommandManager> commands;
	boost::shared_ptr<YOGClientDownloadableMapList> downloadableMapList;
	boost::shared_ptr<YOGClientRatedMapList> ratedMapList;
	YOGClientMapUploader* uploader;
	YOGClientMapDownloader* downloader;
	boost::shared_ptr<YOGServer> server;
	std::list<YOGClientEventListener*> listeners;


};



#endif
