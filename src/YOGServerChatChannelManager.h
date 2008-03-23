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

#ifndef YOGServerChatChannelManager_h
#define YOGServerChatChannelManager_h

#include <map>
#include "SDL_net.h"
#include "boost/shared_ptr.hpp"

class YOGServerChatChannel;

///This does serverside management of YOG chat channels
class YOGServerChatChannelManager
{
public:
	///Creates the YOGServerChatChannelManager
	YOGServerChatChannelManager();

	///Destroys the YOGServerChatChannelManager
	~YOGServerChatChannelManager();

	///This updates the chat channel manager. Removes all chat channels that have no players, except for the lobby
	void update();

	///Creates a new chat channel, returning its number
	Uint32 createNewChatChannel();

	///Returns the lobbys channel
	Uint32 getLobbyChannel();

	///Returns the YOGServerChatChannel for the particular channel
	boost::shared_ptr<YOGServerChatChannel> getChannel(Uint32 channel);

private:
	Uint32 currentChannelID;
	std::map<Uint32, boost::shared_ptr<YOGServerChatChannel> > channels;
};


#endif
