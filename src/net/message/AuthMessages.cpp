// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "AuthMessages.h"
#include <iostream>
#include <sstream>
#include "Version.h"

using namespace GAGCore;

NetSendClientInformation::NetSendClientInformation()
{
	netVersion=NET_PROTOCOL_VERSION;
}



Uint8 NetSendClientInformation::getMessageType() const
{
	return MNetSendClientInformation;
}



void NetSendClientInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendClientInformation");
	stream->writeUint16(netVersion, "netVersion ");
	stream->writeLeaveSection();
}



void NetSendClientInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendClientInformation");
	netVersion=stream->readUint16("netVersion");
	stream->readLeaveSection();
}



std::string NetSendClientInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendClientInformation(netVersion="<<netVersion<<")";
	return s.str();
}



bool NetSendClientInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendClientInformation))
	{
		const NetSendClientInformation& r = dynamic_cast<const NetSendClientInformation&>(rhs);
		if(r.netVersion == netVersion)
		{
			return true;
		}
	}
	return false;
}


Uint16 NetSendClientInformation::getNetVersion() const
{
	return netVersion;
}



NetSendServerInformation::NetSendServerInformation(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy, YOGPlayerID playerID)
	: loginPolicy(loginPolicy), gamePolicy(gamePolicy), playerID(playerID)
{

}



NetSendServerInformation::NetSendServerInformation()
	: loginPolicy(YOGRequirePassword), gamePolicy(YOGSingleGame), playerID(0)
{

}



Uint8 NetSendServerInformation::getMessageType() const
{
	return MNetSendServerInformation;
}



void NetSendServerInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendServerInformation");
	stream->writeUint8(loginPolicy, "loginPolicy ");
	stream->writeUint8(gamePolicy, "gamePolicy ");
	stream->writeUint16(playerID, "playerID ");
	stream->writeLeaveSection();
}


void NetSendServerInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendServerInformation");
	loginPolicy=static_cast<YOGLoginPolicy>(stream->readUint8("loginPolicy"));
	gamePolicy=static_cast<YOGGamePolicy>(stream->readUint8("gamePolicy"));
	playerID=stream->readUint16("playerID");
	stream->readLeaveSection();
}



std::string NetSendServerInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendServerInformation(";
	if(loginPolicy == YOGRequirePassword)
		s<<"loginPolicy=YOGRequirePassword; ";
	else if(loginPolicy == YOGAnonymousLogin)
		s<<"loginPolicy=YOGAnonymousLogin; ";

	if(gamePolicy == YOGSingleGame)
		s<<"gamePolicy=YOGSingleGame; ";
	else if(gamePolicy == YOGMultipleGames)
		s<<"gamePolicy=YOGMultipleGames; ";

	s<<"playerID="<<playerID<<"; ";
	s<<")";
	return s.str();
}



bool NetSendServerInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendServerInformation))
	{
		const NetSendServerInformation& r = dynamic_cast<const NetSendServerInformation&>(rhs);
		if(r.loginPolicy == loginPolicy && r.gamePolicy == gamePolicy)
		{
			return true;
		}
	}
	return false;
}



YOGLoginPolicy NetSendServerInformation::getLoginPolicy() const
{
	return loginPolicy;
}



YOGGamePolicy NetSendServerInformation::getGamePolicy() const
{
	return gamePolicy;
}



YOGPlayerID NetSendServerInformation::getPlayerID() const
{
	return playerID;
}



NetAttemptLogin::NetAttemptLogin(const std::string& username, const std::string& password)
	: username(username), password(password)
{

}



NetAttemptLogin::NetAttemptLogin()
{

}



Uint8 NetAttemptLogin::getMessageType() const
{
	return MNetAttemptLogin;
}



void NetAttemptLogin::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAttemptLogin");
	stream->writeText(username, "username");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}



void NetAttemptLogin::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptLogin");
	username=stream->readText("username");
	password=stream->readText("password");
	stream->readLeaveSection();
}



std::string NetAttemptLogin::format() const
{
	std::ostringstream s;
	s<<"NetAttemptLogin("<<"username=\""<<username<<"\"; password=\""<<password<<"\")";
	return s.str();
}



bool NetAttemptLogin::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAttemptLogin))
	{
		const NetAttemptLogin& r = dynamic_cast<const NetAttemptLogin&>(rhs);
		if(r.username == username && r.password==password)
		{
			return true;
		}
	}
	return false;
}



const std::string& NetAttemptLogin::getUsername() const
{
	return username;
}



const std::string& NetAttemptLogin::getPassword() const
{
	return password;
}



NetLoginSuccessful::NetLoginSuccessful()
{

}



Uint8 NetLoginSuccessful::getMessageType() const
{
	return MNetLoginSuccessful;
}



void NetLoginSuccessful::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetLoginSuccessful");
	stream->writeLeaveSection();
}



void NetLoginSuccessful::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptLogin");
	stream->readLeaveSection();

}



std::string NetLoginSuccessful::format() const
{
	std::ostringstream s;
	s<<"NetLoginSuccessful()";
	return s.str();
}



bool NetLoginSuccessful::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetLoginSuccessful))
	{
//		const NetLoginSuccessful& r = dynamic_cast<const NetLoginSuccessful&>(rhs);
		return true;
	}
	return false;
}


NetRefuseLogin::NetRefuseLogin()
	: reason(YOGLoginSuccessful)
{

}



NetRefuseLogin::NetRefuseLogin(YOGLoginState reason)
	: reason(reason)
{

}



Uint8 NetRefuseLogin::getMessageType() const
{
	return MNetRefuseLogin;
}



void NetRefuseLogin::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseLogin");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetRefuseLogin::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseLogin");
	reason=static_cast<YOGLoginState>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRefuseLogin::format() const
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
	s<<"NetRefuseLogin(reason="<<sreason<<")";
	return s.str();
}



bool NetRefuseLogin::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseLogin))
	{
		const NetRefuseLogin& r = dynamic_cast<const NetRefuseLogin&>(rhs);
		if(r.reason == reason)
		{
			return true;
		}
	}
	return false;
}



YOGLoginState NetRefuseLogin::getRefusalReason() const
{
	return reason;
}



NetDisconnect::NetDisconnect()
{

}



Uint8 NetDisconnect::getMessageType() const
{
	return MNetDisconnect;
}



void NetDisconnect::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetDisconnect");
	stream->writeLeaveSection();
}


void NetDisconnect::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetDisconnect");
	stream->readLeaveSection();
}



std::string NetDisconnect::format() const
{
	std::ostringstream s;
	s<<"NetDisconnect()";
	return s.str();
}



bool NetDisconnect::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetDisconnect))
	{
//		const NetDisconnect& r = dynamic_cast<const NetDisconnect&>(rhs);
		return true;
	}
	return false;
}
