// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "assert.h"
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



