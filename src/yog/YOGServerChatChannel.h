// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <list>
#include "SDL_net.h"
#include <memory>

class YOGMessage;
class YOGServerPlayer;

///This represents a chat channel server-side
class YOGServerChatChannel
{
public:
	///Creates a new chat channel
	YOGServerChatChannel(Uint32 channel);

	///Adds a player to this chat channel
	void addPlayer(std::shared_ptr<YOGServerPlayer> player);

	///Removes a player from this chat channel
	void removePlayer(std::shared_ptr<YOGServerPlayer> player);

	///Routes a YOG message to all players in this channel, except for sender
	void routeMessage(std::shared_ptr<YOGMessage> message, std::shared_ptr<YOGServerPlayer> sender);

	///Returns the number of players in this chat channel
	size_t getNumberOfPlayers() const;
private:
	Uint32 channel;
	std::list<std::shared_ptr<YOGServerPlayer> > players;
};

