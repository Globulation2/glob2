/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __YOGClientChatChannel_h
#define __YOGClientChatChannel_h

#include <vector>
#include <list>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/tuple/tuple.hpp"
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
	YOGClientChatChannel(Uint32 channelID, boost::shared_ptr<YOGClient> client);

	///Destroys the YOGClientChatChannel
	~YOGClientChatChannel();

	///Retrieves the size of the history
	Uint32 getHistorySize() const;

	///Retrieves YOG message x, where 0 is the first message received, and higher gets more recent
	const boost::shared_ptr<YOGMessage> getMessage(Uint32 n) const;

	///Retrieves the local time that YOG message x was received, where higher x gets more recent
	boost::posix_time::ptime getMessageTime(Uint32 n) const;

	///Sends a message through this channel
	void sendMessage(boost::shared_ptr<YOGMessage> message);

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

	///Receives a message from the network (called by YOGClient)
	void receiveMessage(boost::shared_ptr<YOGMessage> message);

	///This sends the message to all listeners
	void sendToListeners(boost::shared_ptr<YOGMessage> message);

private:
	boost::shared_ptr<YOGClient> client;
	Uint32 channelID;
	std::vector<boost::tuple<boost::shared_ptr<YOGMessage>, boost::posix_time::ptime> > messageHistory;
	std::list<YOGClientChatListener*> listeners;
};


#endif
