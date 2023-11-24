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

#include "YOGClientChatChannel.h"
#include "YOGClient.h"
#include "YOGMessage.h"
#include "YOGClientChatListener.h"
#include "NetMessage.h"

YOGClientChatChannel::YOGClientChatChannel(Uint32 channelID, boost::shared_ptr<YOGClient> client)
	: client(client), channelID(channelID)
{
	client->addYOGClientChatChannel(this);
}



YOGClientChatChannel::~YOGClientChatChannel()
{
	client->removeYOGClientChatChannel(this);
}



Uint32 YOGClientChatChannel::getHistorySize() const
{
	return messageHistory.size();
}



const boost::shared_ptr<YOGMessage> YOGClientChatChannel::getMessage(Uint32 n) const
{
	return messageHistory[n].get<0>();
}



boost::posix_time::ptime YOGClientChatChannel::getMessageTime(Uint32 n) const
{
	return messageHistory[n].get<1>();
}



void YOGClientChatChannel::sendMessage(boost::shared_ptr<YOGMessage> message)
{
	if(channelID != static_cast<Uint32>(-1))
	{
		messageHistory.push_back(boost::make_tuple(message, boost::posix_time::second_clock::local_time()));
		boost::shared_ptr<NetSendYOGMessage> netMessage(new NetSendYOGMessage(channelID, message));
		client->sendNetMessage(netMessage);
		sendToListeners(message);
	}
}



Uint32 YOGClientChatChannel::getChannelID() const
{
	return channelID;
}



void YOGClientChatChannel::setChannelID(Uint32 channel)
{
	client->removeYOGClientChatChannel(this);
	channelID = channel;
	client->addYOGClientChatChannel(this);
}



void YOGClientChatChannel::addListener(YOGClientChatListener* listener)
{
	listeners.push_back(listener);
}



void YOGClientChatChannel::removeListener(YOGClientChatListener* listener)
{
	listeners.remove(listener);
}



void YOGClientChatChannel::receiveMessage(boost::shared_ptr<YOGMessage> message)
{
	messageHistory.push_back(boost::make_tuple(message, boost::posix_time::second_clock::local_time()));
	sendToListeners(message);
}



void YOGClientChatChannel::sendToListeners(boost::shared_ptr<YOGMessage> message)
{
	for(std::list<YOGClientChatListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->receiveTextMessage(message);
	}
}


