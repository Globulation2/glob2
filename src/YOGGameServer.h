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

#ifndef __YOGGameServer_h
#define __YOGGameServer_h

#include "NetMessage.h"
#include "NetConnection.h"
#include "NetListener.h"
#include "YOGConsts.h"
#include "YOGPlayer.h"
#include "YOGPasswordRegistry.h"
#include "NetBroadcaster.h"
#include "YOGChatChannelManager.h"

#include <list>
#include <map>
#include <boost/shared_ptr.hpp>

using namespace boost;

///This class encapsulates the YOG server. The YOG server is the games online server.
///There is one YOG server hosted by one of the project members. As well, each client
///is capable of hosting a YOG themselves, this technology is used for LAN games,
///and can also be used when a client decides to host a game themselves, which can
///reduce load on the project hosted server. For this, YOG has server redirection.
///
///The YOG server has a few different behaviours. It can require passwords (for the
///project hosted YOG server), or allow anonymous connections (for LAN and client hosted
///games). It can maintain a list of games and the users that are in them (for the project
///server), or it can have one game, and all connected users are part of it (client hosted
///games). In this manner, the same YOG server is re-used for multiple purposes.
class YOGGameServer
{
public:
	///Initiates the YOG Game Server and immeddiattely begins listening on the YOG port.
	YOGGameServer(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy);

	///If the attempt to bind to the local port failed, this will be false
	bool isListening();

	///This is the main update function. This must be called frequently (many times per
	///second) in order to give fast responce times and low latency for the users.
	void update();

	///Runs the server as its own entity. Returns the return code of the execution
	int run();

	///Returns the login policy that is being used by the server
	YOGLoginPolicy getLoginPolicy() const;
	
	///Returns the game policy that is being used by the server
	YOGGamePolicy getGamePolicy() const;

	///Returns whether the users password is correct.
	YOGLoginState verifyLoginInformation(const std::string& username, const std::string& password, Uint16 version);
	
	///This reigsters a new user
	YOGLoginState registerInformation(const std::string& username, const std::string& password, Uint16 version);

	///Returns the list of games the server currently has
	const std::list<YOGGameInfo>& getGameList() const;
	
	///Returns the list of players the server currently has
	const std::list<YOGPlayerInfo>& getPlayerList() const;

	///Tells the server that a player has logged in with the given information,
	void playerHasLoggedIn(const std::string& username, Uint16 id);

	///Tells the server that the player has logged out and disconnected
	void playerHasLoggedOut(Uint16 playerID);

	///Returns the chat channel manager
	YOGChatChannelManager& getChatChannelManager();

	///Asks the server whether a new game can be created with the given information.
	///Return YOGCreateRefusalUnknown if it can, or the refusal reason elsewise
	YOGGameCreateRefusalReason canCreateNewGame(const std::string& game);

	///Tells the server to create a new game with the given game information,
	///and returns the new id. The id will always be greater than 0
	Uint16 createNewGame(const std::string& name);

	///Asks the server whether a player can join the provided game with the information.
	///Return YOGJoinRefusalUnknown if it can, or the failure reason elsewise
	YOGGameJoinRefusalReason canJoinGame(Uint16 gameID);
	
	///Returns the game assocciatted with the given ID
	shared_ptr<YOGGame> getGame(Uint16 gameID);

	///Returns the player assocciatted with the given ID
	shared_ptr<YOGPlayer> getPlayer(Uint16 playerID);
	
	///This starts LAN broadcasting of the first game, if it exists
	void enableLANBroadcasting();
	
	///This stops LAN broadcasting
	void disableLANBroadcasting();

	///Returns the YOGGameInfo for modification
	YOGGameInfo& getGameInfo(Uint16 gameID);
private:
	Uint16 chooseNewPlayerID();

	///Removes the GameInfo with the given ID
	void removeGameInfo(Uint16 gameID);

	NetListener nl;
	std::map<Uint16, shared_ptr<YOGPlayer> > players;
	std::map<Uint16, shared_ptr<YOGGame> > games;
	std::list<YOGGameInfo> gameList;
	std::list<YOGPlayerInfo> playerList;
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
	
	YOGPasswordRegistry registry;
	
	boost::shared_ptr<NetBroadcaster> broadcaster;
	bool isBroadcasting;
	YOGChatChannelManager chatChannelManager;
};

#endif
