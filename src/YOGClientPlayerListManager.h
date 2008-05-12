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

#ifndef YOGClientPlayerListManager_h
#define YOGClientPlayerListManager_h

#include "boost/shared_ptr.hpp"
#include <list>
#include "YOGPlayerSessionInfo.h"

class NetMessage;
class YOGClient;
class YOGClientPlayerListListener;

///This class manages the list of available players on the client end
class YOGClientPlayerListManager
{
public:
	///Constructs the yog player list manager with a link to the YOGClient
	YOGClientPlayerListManager(YOGClient* client);

	///Recieves an incoming message
	void recieveMessage(boost::shared_ptr<NetMessage> message);
	
	///This will return the list of players on hosted on the server.
	const std::list<YOGPlayerSessionInfo>& getPlayerList() const;
	
	///This will return the list of players on hosted on the server.
	std::list<YOGPlayerSessionInfo>& getPlayerList();

	///This will add a listener for events saying the player list has been updated
	void addListener(YOGClientPlayerListListener* listener);
	
	///This will remove a listener
	void removeListener(YOGClientPlayerListListener* listener);

	///This will find the name of the player with the given ID
	std::string findPlayerName(Uint16 playerID);
	
	///Returns true if a player with the given name exists
	bool doesPlayerExist(const std::string& name);
	
	///Returns the session info of a given player
	YOGPlayerSessionInfo& getPlayerInfo(const std::string& name);
private:
	///This will send the event that the player list has been updated to all the listeners
	void sendToListeners();

	std::list<YOGPlayerSessionInfo> players;
	std::list<YOGClientPlayerListListener*> listeners;
	YOGClient* client;
};

#endif
