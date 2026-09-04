// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGClientChatChannel_h
#define __YOGClientChatChannel_h

#include <vector>
#include <list>
#include "boost/date_time/posix_time/posix_time.hpp"
#include <tuple>
#include "SDL_net.h"

class YOGClient;
class YOGMessage;
class YOGClientChatListener;

///This represents on the client end a single channel of chat, including its history.
///Channels are used in the lobby, in private conversations and in the pre-game setup
class YOGClientChatChannel
{
public:
	///Creates a new YOGClientChatChannel, with its channel id and then YOGClient to listen from
	///Adds itself to the YOGClient to listen for chat events
	YOGClientChatChannel(Uint32 channelID, std::shared_ptr<YOGClient> client);

	///Destroys the YOGClientChatChannel
	~YOGClientChatChannel();

	///Retrieves the size of the history
	Uint32 getHistorySize() const;

	///Retrieves YOG message x, where 0 is the first message recieved, and higher gets more recent	
	const std::shared_ptr<YOGMessage> getMessage(Uint32 n) const;

	///Retrieves the local time that YOG message x was recieved, where higher x gets more recent
	boost::posix_time::ptime getMessageTime(Uint32 n) const;

	///Sends a message through this channel
	void sendMessage(std::shared_ptr<YOGMessage> message);

	///Returns the channel ID of this channel
	Uint32 getChannelID() const;

	///Sets the channel ID of this channel
	void setChannelID(Uint32 channel);

	///Adds the listener for this channel. Does not take ownership.
	void addListener(YOGClientChatListener* listener);
	
	///Removes the listener from this channel
	void removeListener(YOGClientChatListener* listener);

protected:
	friend class YOGClient;

	///Recieves a message from the network (called by YOGClient)
	void recieveMessage(std::shared_ptr<YOGMessage> message);

	///This sends the message to all listeners
	void sendToListeners(std::shared_ptr<YOGMessage> message);

private:
	std::shared_ptr<YOGClient> client;
	Uint32 channelID;
	std::vector<std::tuple<std::shared_ptr<YOGMessage>, boost::posix_time::ptime> > messageHistory;
	std::list<YOGClientChatListener*> listeners;
};


#endif
