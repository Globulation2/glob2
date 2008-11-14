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

#include "IRCThreadMessage.h"
#include <sstream>
#include <typeinfo>


ITConnect::ITConnect(std::string server, std::string nick, int serverPort)
	: server(server), nick(nick), serverPort(serverPort)
{
}



Uint8 ITConnect::getMessageType() const
{
	return ITMConnect;
}



std::string ITConnect::format() const
{
	std::ostringstream s;
	s<<"ITConnect("<<"server="<<server<<"; "<<"nick="<<nick<<"; "<<"serverPort="<<serverPort<<"; "<<")";
	return s.str();
}



bool ITConnect::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITConnect))
	{
		const ITConnect& r = dynamic_cast<const ITConnect&>(rhs);
		if(r.server == server && r.nick == nick && r.serverPort == serverPort)
			return true;
	}
	return false;
}


std::string ITConnect::getServer() const
{
	return server;
}



std::string ITConnect::getNick() const
{
	return nick;
}



int ITConnect::getServerPort() const
{
	return serverPort;
}



ITDisconnect::ITDisconnect()
{
}



Uint8 ITDisconnect::getMessageType() const
{
	return ITMDisconnect;
}



std::string ITDisconnect::format() const
{
	std::ostringstream s;
	s<<"ITDisconnect()";
	return s.str();
}



bool ITDisconnect::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITDisconnect))
	{
		//const ITDisconnect& r = dynamic_cast<const ITDisconnect&>(rhs);
		return true;
	}
	return false;
}


ITSendMessage::ITSendMessage(std::string text)
	: text(text)
{
}



Uint8 ITSendMessage::getMessageType() const
{
	return ITMSendMessage;
}



std::string ITSendMessage::format() const
{
	std::ostringstream s;
	s<<"ITSendMessage("<<"text="<<text<<"; "<<")";
	return s.str();
}



bool ITSendMessage::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITSendMessage))
	{
		const ITSendMessage& r = dynamic_cast<const ITSendMessage&>(rhs);
		if(r.text == text)
			return true;
	}
	return false;
}


std::string ITSendMessage::getText() const
{
	return text;
}



ITDisconnected::ITDisconnected()
{
}



Uint8 ITDisconnected::getMessageType() const
{
	return ITMDisconnected;
}



std::string ITDisconnected::format() const
{
	std::ostringstream s;
	s<<"ITDisconnected()";
	return s.str();
}



bool ITDisconnected::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITDisconnected))
	{
		//const ITDisconnected& r = dynamic_cast<const ITDisconnected&>(rhs);
		return true;
	}
	return false;
}


ITRecieveMessage::ITRecieveMessage(std::string message)
	: message(message)
{
}



Uint8 ITRecieveMessage::getMessageType() const
{
	return ITMRecieveMessage;
}



std::string ITRecieveMessage::format() const
{
	std::ostringstream s;
	s<<"ITRecieveMessage("<<"message="<<message<<"; "<<")";
	return s.str();
}



bool ITRecieveMessage::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITRecieveMessage))
	{
		const ITRecieveMessage& r = dynamic_cast<const ITRecieveMessage&>(rhs);
		if(r.message == message)
			return true;
	}
	return false;
}


std::string ITRecieveMessage::getMessage() const
{
	return message;
}



ITJoinChannel::ITJoinChannel(std::string channel)
	: channel(channel)
{
}



Uint8 ITJoinChannel::getMessageType() const
{
	return ITMJoinChannel;
}



std::string ITJoinChannel::format() const
{
	std::ostringstream s;
	s<<"ITJoinChannel("<<"channel="<<channel<<"; "<<")";
	return s.str();
}



bool ITJoinChannel::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITJoinChannel))
	{
		const ITJoinChannel& r = dynamic_cast<const ITJoinChannel&>(rhs);
		if(r.channel == channel)
			return true;
	}
	return false;
}


std::string ITJoinChannel::getChannel() const
{
	return channel;
}



ITExitThread::ITExitThread()
{
}



Uint8 ITExitThread::getMessageType() const
{
	return ITMExitThread;
}



std::string ITExitThread::format() const
{
	std::ostringstream s;
	s<<"ITExitThread()";
	return s.str();
}



bool ITExitThread::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITExitThread))
	{
		//const ITExitThread& r = dynamic_cast<const ITExitThread&>(rhs);
		return true;
	}
	return false;
}


ITUserListModified::ITUserListModified()
{
}



Uint8 ITUserListModified::getMessageType() const
{
	return ITMUserListModified;
}



std::string ITUserListModified::format() const
{
	std::ostringstream s;
	s<<"ITUserListModified()";
	return s.str();
}



bool ITUserListModified::operator==(const IRCThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(ITUserListModified))
	{
		const ITUserListModified& r = dynamic_cast<const ITUserListModified&>(rhs);
		if(users == r.users)
			return true;
	}
	return false;
}



void  ITUserListModified::addUser(const std::string& user)
{
	users.push_back(user);
}



std::vector<std::string>& ITUserListModified::getUsers()
{
	return users;
}



//code_append_marker
