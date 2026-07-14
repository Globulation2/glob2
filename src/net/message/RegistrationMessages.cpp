// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "RegistrationMessages.h"
#include <sstream>

using namespace GAGCore;

NetAttemptRegistration::NetAttemptRegistration()
{

}




NetAttemptRegistration::NetAttemptRegistration(const std::string& username, const std::string& password)
	: username(username), password(password)
{

}




Uint8 NetAttemptRegistration::getMessageType() const
{
	return MNetAttemptRegistration;
}



void NetAttemptRegistration::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAttemptRegistration");
	stream->writeText(username, "username");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}



void NetAttemptRegistration::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptRegistration");
	username=stream->readText("username");
	password=stream->readText("password");
	stream->readLeaveSection();
}



std::string NetAttemptRegistration::format() const
{
	std::ostringstream s;
	s<<"NetAttemptRegistration(username=\""<<username<<"\"; password=\""<<password<<"\")";
	return s.str();
}



bool NetAttemptRegistration::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAttemptRegistration))
	{
		const NetAttemptRegistration& r = dynamic_cast<const NetAttemptRegistration&>(rhs);
		if(username == r.username && password == r.password)
			return true;
	}
	return false;
}



std::string NetAttemptRegistration::getUsername() const
{
	return username;
}



std::string NetAttemptRegistration::getPassword() const
{
	return password;
}



NetAcceptRegistration::NetAcceptRegistration()
{

}



Uint8 NetAcceptRegistration::getMessageType() const
{
	return MNetAcceptRegistration;
}



void NetAcceptRegistration::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAcceptRegistration");
	stream->writeLeaveSection();
}



void NetAcceptRegistration::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAcceptRegistration");
	stream->readLeaveSection();
}



std::string NetAcceptRegistration::format() const
{
	std::ostringstream s;
	s<<"NetAcceptRegistration()";
	return s.str();
}



bool NetAcceptRegistration::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAcceptRegistration))
	{
//		const NetAcceptRegistration& r = dynamic_cast<const NetAcceptRegistration&>(rhs);
		return true;
	}
	return false;
}



NetRefuseRegistration::NetRefuseRegistration()
{
	reason = YOGLoginUnknown;
}



NetRefuseRegistration::NetRefuseRegistration(YOGLoginState reason)
	: reason(reason)
{

}



Uint8 NetRefuseRegistration::getMessageType() const
{
	return MNetRefuseRegistration;
}



void NetRefuseRegistration::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseRegistration");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}


void NetRefuseRegistration::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseRegistration");
	reason=static_cast<YOGLoginState>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRefuseRegistration::format() const
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
	s<<"NetRefuseRegistration(reason="<<sreason<<")";
	return s.str();
}



bool NetRefuseRegistration::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseRegistration))
	{
		const NetRefuseRegistration& r = dynamic_cast<const NetRefuseRegistration&>(rhs);
		if(reason == r.reason)
			return true;
	}
	return false;
}



YOGLoginState NetRefuseRegistration::getRefusalReason() const
{
	return reason;
}
