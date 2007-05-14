/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "YOGMessage.h"
#include "SDL_net.h"

YOGMessage::YOGMessage()
{

}



YOGMessage::YOGMessage(const std::string& message, const std::string& sender, YOGMessageType type)
	: messageType(type), message(message),  sender(sender)
{

}



void YOGMessage::setMessage(const std::string& newMessage)
{
	message = newMessage;
}


	
std::string YOGMessage::getMessage() const
{
	return message;
}



void YOGMessage::setSender(const std::string& newSender)
{
	sender = newSender;
}



std::string YOGMessage::getSender() const
{
	return sender;
}



void YOGMessage::setMessageType(YOGMessageType type)
{
	messageType = type;
}


	
YOGMessageType YOGMessage::getMessageType() const
{
	return messageType;
}



Uint8* YOGMessage::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	Uint32 pos = 0;

	//Write the messageType
	data[pos] = messageType;
	pos+=1;

	//Write the sender
	data[pos] = sender.size();
	pos+=1;
	std::copy(sender.begin(), sender.end(), data+pos);
	pos+=sender.size();

	//Write message
	SDLNet_Write16(message.size(), data+pos);
	pos+=2;
	std::copy(message.begin(), message.end(), data+pos);
	pos+=message.size();
	return data;
}


	
Uint16 YOGMessage::getDataLength() const
{
	return 4 + sender.size() + message.size();
}



bool YOGMessage::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 pos = 0;

	//Read in the messageType
	messageType = static_cast<YOGMessageType>(data[pos]);
	pos+=1;

	//Read in the sender
	Uint16 size = data[pos];
	pos+=1;
	for(int i=0; i<size; ++i)
	{
		message+=static_cast<char>(data[pos]);
		pos+=1;
	}

	//Read in the message
	size = SDLNet_Read16(data+pos);
	pos+=2;
	for(int i=0; i<size; ++i)
	{
		message+=static_cast<char>(data[pos]);
		pos+=1;
	}
}


	
bool YOGMessage::operator==(const YOGMessage& rhs) const
{
	if(message == rhs.message && messageType == rhs.messageType)
	{
		return true;
	}
	return false;
}



bool YOGMessage::operator!=(const YOGMessage& rhs) const
{
	if(message != rhs.message || messageType != rhs.messageType)
	{
		return true;
	}
	return false;
}


