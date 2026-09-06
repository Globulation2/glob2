// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <memory>
#include <list>
#include "YOGGameInfo.h"
#include "ListenerList.h"

class NetMessage;
class YOGClient;
class YOGClientGameListListener;

///This class manages the list of available games on the client end
class YOGClientGameListManager
{
public:
	///Constructs the yog game list manager with a link to the YOGClient
	YOGClientGameListManager(YOGClient* client);

	///Recieves an incoming message
	void recieveMessage(std::shared_ptr<NetMessage> message);
	
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
	ListenerList<YOGClientGameListListener> listeners;
};

