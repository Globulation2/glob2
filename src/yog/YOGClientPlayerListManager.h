// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <memory>
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
	void recieveMessage(std::shared_ptr<NetMessage> message);
	
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
};

