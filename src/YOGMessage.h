// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGMessage_h
#define __YOGMessage_h

#include <string>
#include "YOGConsts.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class generically represents a message sent in YOG. This kind of message
///can be for any purpose, including administrator messages, private messages,
///in-game messages, excetra
class YOGMessage
{
public:
	///Construct an empty YOGMessage
	YOGMessage();
	
	///Construct a YOGMessage
	YOGMessage(const std::string& message, const std::string& sender, YOGMessageType type);

	///Sets the body of the message
	void setMessage(const std::string& message);
	
	///Returns the message
	std::string getMessage() const;

	///Sets the user the message was sent from
	void setSender(const std::string& user);
	
	///Returns the sender
	std::string getSender() const;

	///Sets the message type
	void setMessageType(YOGMessageType type);
	
	///Returns the message type
	YOGMessageType getMessageType() const;

	///Encodes this YOGMessage into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGMessage from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGMessage
	bool operator==(const YOGMessage& rhs) const;
	bool operator!=(const YOGMessage& rhs) const;

	///Formats this YOG Message in a user friendly way
	std::string formatForReading() const;
private:
	YOGMessageType messageType;
	std::string message;
	std::string sender;
};



#endif
