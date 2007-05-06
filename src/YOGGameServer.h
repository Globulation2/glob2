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

#ifndef __YOGGameServer_h
#define __YOGGameServer_h

#include "NetMessage.h"
#include "NetConnection.h"
#include "NetListener.h"
#include "YOGConsts.h"
#include "YOGPlayer.h"

#include <list>

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
	
	///This is the main update function. This must be called frequently (many times per
	///second) in order to give fast responce times and low latency for the users.
	void update();

	///Returns the login policy that is being used by the server
	YOGLoginPolicy getLoginPolicy() const;
	
	///Returns the game policy that is being used by the server
	YOGGamePolicy getGamePolicy() const;

	///Returns whether the users password is correct.
	YOGLoginState verifyLoginInformation(const std::string& username, const std::string& password);

	///Returns the list of games the server currently has
	const std::list<YOGGameInfo>& getGameList() const;

private:
	NetListener listener;
	std::list<YOGPlayer> players;
	std::list<YOGGameInfo> games;
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
};

#endif
