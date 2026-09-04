// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef YOGServerChatChannelManager_h
#define YOGServerChatChannelManager_h

#include <map>
#include "SDL_net.h"
#include <memory>

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
	std::shared_ptr<YOGServerChatChannel> getChannel(Uint32 channel);

private:
	Uint32 currentChannelID;
	std::map<Uint32, std::shared_ptr<YOGServerChatChannel> > channels;
};


#endif
