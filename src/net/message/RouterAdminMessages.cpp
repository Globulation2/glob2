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




NetRouterAdministratorSendCommand::NetRouterAdministratorSendCommand()
	: command("")
{

}



NetRouterAdministratorSendCommand::NetRouterAdministratorSendCommand(std::string command)
	:command(command)
{
}



Uint8 NetRouterAdministratorSendCommand::getMessageType() const
{
	return MNetRouterAdministratorSendCommand;
}



void NetRouterAdministratorSendCommand::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorSendCommand");
	stream->writeText(command, "command");
	stream->writeLeaveSection();
}



void NetRouterAdministratorSendCommand::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorSendCommand");
	command = stream->readText("command");
	stream->readLeaveSection();
}



std::string NetRouterAdministratorSendCommand::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorSendCommand("<<"command="<<command<<"; "<<")";
	return s.str();
}



bool NetRouterAdministratorSendCommand::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorSendCommand))
	{
		const NetRouterAdministratorSendCommand& r = dynamic_cast<const NetRouterAdministratorSendCommand&>(rhs);
		if(r.command == command)
			return true;
	}
	return false;
}


std::string NetRouterAdministratorSendCommand::getCommand() const
{
	return command;
}




NetRouterAdministratorSendText::NetRouterAdministratorSendText()
	: text("")
{

}



NetRouterAdministratorSendText::NetRouterAdministratorSendText(std::string text)
	:text(text)
{
}



Uint8 NetRouterAdministratorSendText::getMessageType() const
{
	return MNetRouterAdministratorSendText;
}



void NetRouterAdministratorSendText::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorSendText");
	stream->writeText(text, "text");
	stream->writeLeaveSection();
}



void NetRouterAdministratorSendText::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorSendText");
	text = stream->readText("text");
	stream->readLeaveSection();
}



std::string NetRouterAdministratorSendText::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorSendText("<<"text="<<text<<"; "<<")";
	return s.str();
}



bool NetRouterAdministratorSendText::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorSendText))
	{
		const NetRouterAdministratorSendText& r = dynamic_cast<const NetRouterAdministratorSendText&>(rhs);
		if(r.text == text)
			return true;
	}
	return false;
}


std::string NetRouterAdministratorSendText::getText() const
{
	return text;
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
