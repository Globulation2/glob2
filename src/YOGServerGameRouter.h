// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGServerGameRouter_h
#define YOGServerGameRouter_h

#include <vector>
#include <memory>

class YOGServerRouterPlayer;
class NetMessage;

///This class acts is the router for games, it routes messages between all connected players
class YOGServerGameRouter
{
public:
	///Constructs a YOGServerGameRouter
	YOGServerGameRouter(); 

	///Adds a player to this router group
	void addPlayer(std::shared_ptr<YOGServerRouterPlayer> player);
	
	///Updates this game
	void update();
	
	///Returns true if this game is empty
	bool isEmpty();
	
	///Removes a net message to all players
	void routeMessage(std::shared_ptr<NetMessage> message, YOGServerRouterPlayer* sender);
private:
	std::vector<std::shared_ptr<YOGServerRouterPlayer> > players;
};


#endif
