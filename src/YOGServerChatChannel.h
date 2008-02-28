/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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


#ifndef YOGServerChatChannel_h
#define YOGServerChatChannel_h

#include <list>
#include "SDL_net.h"
#include <boost/shared_ptr.hpp>

class YOGMessage;
class YOGPlayer;

///This represents a chat channel server-side
class YOGServerChatChannel
{
public:
	///Creates a new chat channel
	YOGServerChatChannel(Uint32 channel);

	///Adds a player to this chat channel
	void addPlayer(boost::shared_ptr<YOGPlayer> player);

	///Removes a player from this chat channel
	void removePlayer(boost::shared_ptr<YOGPlayer> player);

	///Routes a YOG message to all players in this channel, except for sender
	void routeMessage(boost::shared_ptr<YOGMessage> message, boost::shared_ptr<YOGPlayer> sender);

	///Returns the number of players in this chat channel
	size_t getNumberOfPlayers() const;
private:
	Uint32 channel;
	std::list<boost::shared_ptr<YOGPlayer> > players;
};

#endif
