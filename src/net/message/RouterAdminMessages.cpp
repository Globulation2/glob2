// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "RouterAdminMessages.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetRouterAdministratorLogin::NetRouterAdministratorLogin()
	: password()
{

}

NetRouterAdministratorLogin::NetRouterAdministratorLogin(std::string password)
	:password(password)
{
}

Uint8 NetRouterAdministratorLogin::getMessageType() const
{
	return MNetRouterAdministratorLogin;
}

void NetRouterAdministratorLogin::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorLogin");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}

void NetRouterAdministratorLogin::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorLogin");
	password = stream->readText("password");
	stream->readLeaveSection();
}

std::string NetRouterAdministratorLogin::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorLogin("<<"password="<<password<<"; "<<")";
	return s.str();
}

bool NetRouterAdministratorLogin::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorLogin))
	{
		const NetRouterAdministratorLogin& r = dynamic_cast<const NetRouterAdministratorLogin&>(rhs);
		if(r.password == password)
			return true;
	}
	return false;
}

std::string NetRouterAdministratorLogin::getPassword() const
{
	return password;
}

NetRouterAdministratorCommandRequest::NetRouterAdministratorCommandRequest()
	: command("")
{

}

NetRouterAdministratorCommandRequest::NetRouterAdministratorCommandRequest(std::string command)
	:command(command)
{
}

Uint8 NetRouterAdministratorCommandRequest::getMessageType() const
{
	return MNetRouterAdministratorCommandRequest;
}

void NetRouterAdministratorCommandRequest::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorCommandRequest");
	stream->writeText(command, "command");
	stream->writeLeaveSection();
}

void NetRouterAdministratorCommandRequest::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorCommandRequest");
	command = stream->readText("command");
	stream->readLeaveSection();
}

std::string NetRouterAdministratorCommandRequest::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorCommandRequest("<<"command="<<command<<"; "<<")";
	return s.str();
}

bool NetRouterAdministratorCommandRequest::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorCommandRequest))
	{
		const NetRouterAdministratorCommandRequest& r = dynamic_cast<const NetRouterAdministratorCommandRequest&>(rhs);
		if(r.command == command)
			return true;
	}
	return false;
}

std::string NetRouterAdministratorCommandRequest::getCommand() const
{
	return command;
}

NetRouterAdministratorCommandResponse::NetRouterAdministratorCommandResponse()
	: response("")
{

}

NetRouterAdministratorCommandResponse::NetRouterAdministratorCommandResponse(std::string response)
	:response(response)
{
}

Uint8 NetRouterAdministratorCommandResponse::getMessageType() const
{
	return MNetRouterAdministratorCommandResponse;
}

void NetRouterAdministratorCommandResponse::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorCommandResponse");
	stream->writeText(response, "response");
	stream->writeLeaveSection();
}

void NetRouterAdministratorCommandResponse::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorCommandResponse");
	response = stream->readText("response");
	stream->readLeaveSection();
}

std::string NetRouterAdministratorCommandResponse::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorCommandResponse("<<"response="<<response<<"; "<<")";
	return s.str();
}

bool NetRouterAdministratorCommandResponse::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorCommandResponse))
	{
		const NetRouterAdministratorCommandResponse& r = dynamic_cast<const NetRouterAdministratorCommandResponse&>(rhs);
		if(r.response == response)
			return true;
	}
	return false;
}

std::string NetRouterAdministratorCommandResponse::getResponse() const
{
	return response;
}

NetRouterAdministratorLoginAccepted::NetRouterAdministratorLoginAccepted()
{

}

Uint8 NetRouterAdministratorLoginAccepted::getMessageType() const
{
	return MNetRouterAdministratorLoginAccepted;
}

void NetRouterAdministratorLoginAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorLoginAccepted");
	stream->writeLeaveSection();
}

void NetRouterAdministratorLoginAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorLoginAccepted");
	stream->readLeaveSection();
}

std::string NetRouterAdministratorLoginAccepted::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorLoginAccepted()";
	return s.str();
}

bool NetRouterAdministratorLoginAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorLoginAccepted))
	{
		//const NetRouterAdministratorLoginAccepted& r = dynamic_cast<const NetRouterAdministratorLoginAccepted&>(rhs);
		return true;
	}
	return false;
}

NetRouterAdministratorLoginRefused::NetRouterAdministratorLoginRefused()
	: reason(YOGRouterLoginUnknown)
{

}

NetRouterAdministratorLoginRefused::NetRouterAdministratorLoginRefused(YOGRouterAdministratorLoginRefusalReason reason)
	:reason(reason)
{
}

Uint8 NetRouterAdministratorLoginRefused::getMessageType() const
{
	return MNetRouterAdministratorLoginRefused;
}

void NetRouterAdministratorLoginRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorLoginRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}

void NetRouterAdministratorLoginRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorLoginRefused");
	reason = static_cast<YOGRouterAdministratorLoginRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}

std::string NetRouterAdministratorLoginRefused::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorLoginRefused("<<"reason="<<reason<<"; "<<")";
	return s.str();
}

bool NetRouterAdministratorLoginRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorLoginRefused))
	{
		const NetRouterAdministratorLoginRefused& r = dynamic_cast<const NetRouterAdministratorLoginRefused&>(rhs);
		if(r.reason == reason)
			return true;
	}
	return false;
}

YOGRouterAdministratorLoginRefusalReason NetRouterAdministratorLoginRefused::getReason() const
{
	return reason;
}
