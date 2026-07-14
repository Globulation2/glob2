// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "LobbyMessages.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetUpdateGameList::NetUpdateGameList()
{

}



Uint8 NetUpdateGameList::getMessageType() const
{
	return MNetUpdateGameList;
}



void NetUpdateGameList::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetUpdateGameList");
	stream->writeEnterSection("removedGames");
	stream->writeUint8(removedGames.size(), "size");
	for(Uint16 i=0; i<removedGames.size(); ++i)
	{
		stream->writeUint16(removedGames[i], "removedGames[i]");
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("updatedGames");
	stream->writeUint8(updatedGames.size(), "size");
	for(Uint16 i=0; i<updatedGames.size(); ++i)
	{
		updatedGames[i].encodeData(stream);
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}



void NetUpdateGameList::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetUpdateGameList");

	stream->readEnterSection("removedGames");
	Uint8 size = stream->readUint8("size");
	removedGames.resize(size);
	for(Uint16 i=0; i<removedGames.size(); ++i)
	{
		removedGames[i]=stream->readUint16("removedGames[i]");
	}
	stream->readLeaveSection();

	stream->readEnterSection("updatedGames");
	size = stream->readUint8("size");
	updatedGames.resize(size);
	for(Uint16 i=0; i<updatedGames.size(); ++i)
	{
		updatedGames[i].decodeData(stream);
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
}



std::string NetUpdateGameList::format() const
{
	std::ostringstream s;
	s<<"NetUpdateGameList(removedGames "<<removedGames.size()<<", updatedGames "<<updatedGames.size()<<")";
	return s.str();
}



bool NetUpdateGameList::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetUpdateGameList))
	{
		const NetUpdateGameList& r = dynamic_cast<const NetUpdateGameList&>(rhs);
		if(r.removedGames == removedGames && r.updatedGames == updatedGames)
		{
			return true;
		}
	}
	return false;
}



NetUpdatePlayerList::NetUpdatePlayerList()
{

}



Uint8 NetUpdatePlayerList::getMessageType() const
{
	return MNetUpdatePlayerList;
}



void NetUpdatePlayerList::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetUpdatePlayerList");
	stream->writeEnterSection("removedPlayers");
	stream->writeUint8(removedPlayers.size(), "size");
	for(Uint16 i=0; i<removedPlayers.size(); ++i)
	{
		stream->writeUint16(removedPlayers[i], "removedPlayers[i]");
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("updatedPlayers");
	stream->writeUint8(updatedPlayers.size(), "size");
	for(Uint16 i=0; i<updatedPlayers.size(); ++i)
	{
		updatedPlayers[i].encodeData(stream);
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}



void NetUpdatePlayerList::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetUpdatePlayerList");

	stream->readEnterSection("removedPlayers");
	Uint8 size = stream->readUint8("size");
	removedPlayers.resize(size);
	for(Uint16 i=0; i<removedPlayers.size(); ++i)
	{
		removedPlayers[i]=stream->readUint16("removedPlayers[i]");
	}
	stream->readLeaveSection();

	stream->readEnterSection("updatedPlayers");
	size = stream->readUint8("size");
	updatedPlayers.resize(size);
	for(Uint16 i=0; i<updatedPlayers.size(); ++i)
	{
		updatedPlayers[i].decodeData(stream);
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
}



std::string NetUpdatePlayerList::format() const
{
	std::ostringstream s;
	s<<"NetUpdatePlayerList(updatedPlayers "<<updatedPlayers.size()<<", removedPlayers "<<removedPlayers.size()<<")";
	return s.str();
}



bool NetUpdatePlayerList::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetUpdatePlayerList))
	{
		const NetUpdatePlayerList& r = dynamic_cast<const NetUpdatePlayerList&>(rhs);
		if(updatedPlayers == r.updatedPlayers && removedPlayers == r.removedPlayers)
			return true;
	}
	return false;
}



NetSendYOGMessage::NetSendYOGMessage(Uint32 channel, std::shared_ptr<YOGMessage> message)
	: channel(channel), message(message)
{

}



NetSendYOGMessage::NetSendYOGMessage()
	: channel(0)
{

}



Uint8 NetSendYOGMessage::getMessageType() const
{
	return MNetSendYOGMessage;
}



void NetSendYOGMessage::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendYOGMessage");
	stream->writeUint32(channel, "channel");
	message->encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendYOGMessage::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendYOGMessage");
	channel = stream->readUint32("channel");
	message.reset(new YOGMessage);
	message->decodeData(stream);
	stream->readLeaveSection();
}



std::string NetSendYOGMessage::format() const
{
	std::ostringstream s;
	s<<"NetSendYOGMessage(channel="<<channel<<")";
	return s.str();
}



bool NetSendYOGMessage::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendYOGMessage))
	{
		const NetSendYOGMessage& r = dynamic_cast<const NetSendYOGMessage&>(rhs);
		if(channel != r.channel)
			return false;
		else if(!message && !r.message)
			return true;
		else if(!message && r.message)
			return false;
		else if(message && !r.message)
			return false;
		if((*message) == (*r.message))
			return true;
	}
	return false;
}



Uint32 NetSendYOGMessage::getChannel() const
{
	return channel;
}



std::shared_ptr<YOGMessage> NetSendYOGMessage::getMessage() const
{
	return message;
}



NetPlayerIsBanned::NetPlayerIsBanned()
{

}



Uint8 NetPlayerIsBanned::getMessageType() const
{
	return MNetPlayerIsBanned;
}



void NetPlayerIsBanned::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPlayerIsBanned");
	stream->writeLeaveSection();
}



void NetPlayerIsBanned::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPlayerIsBanned");
	stream->readLeaveSection();
}



std::string NetPlayerIsBanned::format() const
{
	std::ostringstream s;
	s<<"NetPlayerIsBanned()";
	return s.str();
}



bool NetPlayerIsBanned::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPlayerIsBanned))
	{
		//const NetPlayerIsBanned& r = dynamic_cast<const NetPlayerIsBanned&>(rhs);
		return true;
	}
	return false;
}



NetIPIsBanned::NetIPIsBanned()
{

}



Uint8 NetIPIsBanned::getMessageType() const
{
	return MNetIPIsBanned;
}



void NetIPIsBanned::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetIPIsBanned");
	stream->writeLeaveSection();
}



void NetIPIsBanned::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetIPIsBanned");
	stream->readLeaveSection();
}



std::string NetIPIsBanned::format() const
{
	std::ostringstream s;
	s<<"NetIPIsBanned()";
	return s.str();
}



bool NetIPIsBanned::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetIPIsBanned))
	{
		//const NetIPIsBanned& r = dynamic_cast<const NetIPIsBanned&>(rhs);
		return true;
	}
	return false;
}
