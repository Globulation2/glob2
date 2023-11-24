/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef YOGClientGameListManager_h
#define YOGClientGameListManager_h

#include "boost/shared_ptr.hpp"
#include <list>
#include "YOGGameInfo.h"

class NetMessage;
class YOGClient;
class YOGClientGameListListener;

///This class manages the list of available games on the client end
class YOGClientGameListManager
{
public:
	///Constructs the yog game list manager with a link to the YOGClient
	YOGClientGameListManager(YOGClient* client);

	///Receives an incoming message
	void receiveMessage(boost::shared_ptr<NetMessage> message);
	
	///This will return the list of games on hosted on the server.
	const std::list<YOGGameInfo>& getGameList() const;
	
	///This will return the list of games on hosted on the server.
	std::list<YOGGameInfo>& getGameList();
	
	///Returns the game info with the given game id
	YOGGameInfo getGameInfo(Uint16 gameID);

	///This will add a listener for events saying the game list has been updated
	void addListener(YOGClientGameListListener* listener);
	
	///This will remove a listener
	void removeListener(YOGClientGameListListener* listener);

private:
	///This will send the event that the game list has been updated to all the listeners
	void sendToListeners();

	std::list<YOGGameInfo> games;
	std::list<YOGClientGameListListener*> listeners;
	YOGClient* client;
};

#endif
