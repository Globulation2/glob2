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

#include "assert.h"
#include <SDL2/SDL_net.h>
#include "Stream.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGMessage.h"

YOGMessage::YOGMessage()
{
	messageType = YOGNormalMessage;
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



void YOGMessage::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGMessage");
	stream->writeUint16(messageType, "messageType");
	stream->writeText(sender, "sender");
	stream->writeText(message, "message");
	stream->writeLeaveSection();
}




void YOGMessage::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGMessage");
	messageType = static_cast<YOGMessageType>(stream->readUint16("messageType"));
	sender = stream->readText("sender");
	message = stream->readText("message");
	stream->readLeaveSection();
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



std::string YOGMessage::formatForReading() const
{
	std::string smessage;
	switch(getMessageType())
	{
		case YOGNormalMessage:
			smessage+="<";
			smessage+=getSender();
			smessage+="> ";
			smessage+=getMessage();
		break;
		case YOGPrivateMessage:
			smessage+="<";
			smessage+=GAGCore::Toolkit::getStringTable()->getString("[from:]");
			smessage+=getSender();
			smessage+="> ";
			smessage+=getMessage();
		break;
		case YOGAdministratorMessage:
			smessage+="[";
			smessage+=getSender();
			smessage+="] ";
			smessage+=getMessage();
		break;
		case YOGServerGameMessage:
			smessage+="<";
			smessage+=getSender();
			smessage+="> ";
			smessage+=getMessage();
		break;
		default:
			assert(false);
		break;
	}
	return smessage;
}



