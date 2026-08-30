// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "RegistrationMessages.h"
#include <sstream>

using namespace GAGCore;

NetRegistrationRequest::NetRegistrationRequest()
{

}

NetRegistrationRequest::NetRegistrationRequest(const std::string& username, const std::string& password)
	: username(username), password(password)
{

}

Uint8 NetRegistrationRequest::getMessageType() const
{
	return MNetRegistrationRequest;
}

void NetRegistrationRequest::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRegistrationRequest");
	stream->writeText(username, "username");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}

void NetRegistrationRequest::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRegistrationRequest");
	username=stream->readText("username");
	password=stream->readText("password");
	stream->readLeaveSection();
}

std::string NetRegistrationRequest::format() const
{
	std::ostringstream s;
	s<<"NetRegistrationRequest(username=\""<<username<<"\"; password=\""<<password<<"\")";
	return s.str();
}

bool NetRegistrationRequest::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRegistrationRequest))
	{
		const NetRegistrationRequest& r = dynamic_cast<const NetRegistrationRequest&>(rhs);
		if(username == r.username && password == r.password)
			return true;
	}
	return false;
}

std::string NetRegistrationRequest::getUsername() const
{
	return username;
}

std::string NetRegistrationRequest::getPassword() const
{
	return password;
}

NetRegistrationAccepted::NetRegistrationAccepted()
{

}

Uint8 NetRegistrationAccepted::getMessageType() const
{
	return MNetRegistrationAccepted;
}

void NetRegistrationAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRegistrationAccepted");
	stream->writeLeaveSection();
}

void NetRegistrationAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRegistrationAccepted");
	stream->readLeaveSection();
}

std::string NetRegistrationAccepted::format() const
{
	std::ostringstream s;
	s<<"NetRegistrationAccepted()";
	return s.str();
}

bool NetRegistrationAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRegistrationAccepted))
	{
//		const NetRegistrationAccepted& r = dynamic_cast<const NetRegistrationAccepted&>(rhs);
		return true;
	}
	return false;
}

NetRegistrationRefused::NetRegistrationRefused()
{
	reason = YOGLoginUnknown;
}

NetRegistrationRefused::NetRegistrationRefused(YOGLoginState reason)
	: reason(reason)
{

}

Uint8 NetRegistrationRefused::getMessageType() const
{
	return MNetRegistrationRefused;
}

void NetRegistrationRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRegistrationRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}

void NetRegistrationRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRegistrationRefused");
	reason=static_cast<YOGLoginState>(stream->readUint8("reason"));
	stream->readLeaveSection();
}

std::string NetRegistrationRefused::format() const
{
	std::ostringstream s;
	std::string sreason;
	if(reason == YOGLoginSuccessful)
		sreason="YOGLoginSuccessful";
	if(reason == YOGLoginUnknown)
		sreason="YOGLoginUnknown";
	if(reason == YOGPasswordIncorrect)
		sreason="YOGPasswordIncorrect";
	if(reason == YOGUsernameAlreadyUsed)
		sreason="YOGUsernameAlreadyUsed";
	if(reason == YOGUserNotRegistered)
		sreason="YOGUserNotRegistered";
	s<<"NetRegistrationRefused(reason="<<sreason<<")";
	return s.str();
}

bool NetRegistrationRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRegistrationRefused))
	{
		const NetRegistrationRefused& r = dynamic_cast<const NetRegistrationRefused&>(rhs);
		if(reason == r.reason)
			return true;
	}
	return false;
}

YOGLoginState NetRegistrationRefused::getRefusalReason() const
{
	return reason;
}
