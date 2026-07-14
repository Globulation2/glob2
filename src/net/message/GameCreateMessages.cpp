// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameCreateMessages.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetCreateGame::NetCreateGame()
{

}



NetCreateGame::NetCreateGame(const std::string& gameName)
	: gameName(gameName)
{

}




Uint8 NetCreateGame::getMessageType() const
{
	return MNetCreateGame;
}



void NetCreateGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGame");
	stream->writeText(gameName, "gameName");
	stream->writeLeaveSection();
}



void NetCreateGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGame");
	gameName=stream->readText("gameName");
	stream->readLeaveSection();
}



std::string NetCreateGame::format() const
{
	std::ostringstream s;
	s<<"NetCreateGame(gameName=\""<<gameName<<"\")";
	return s.str();
}



bool NetCreateGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGame))
	{
		const NetCreateGame& r = dynamic_cast<const NetCreateGame&>(rhs);
		if(r.gameName == gameName)
			return true;
	}
	return false;
}



const std::string& NetCreateGame::getGameName() const
{
	return gameName;
}



NetCreateGameAccepted::NetCreateGameAccepted()
{
	chatChannel = 0;
	gameID = 0;
	routerIP = "";
	fileID = 0;
}


NetCreateGameAccepted::NetCreateGameAccepted(Uint32 chatChannel, Uint16 gameID, const std::string& routerIP, Uint16 fileID)
	: chatChannel(chatChannel), gameID(gameID), routerIP(routerIP), fileID(fileID)
{

}



Uint8 NetCreateGameAccepted::getMessageType() const
{
	return MNetCreateGameAccepted;
}



void NetCreateGameAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGameAccepted");
	stream->writeUint32(chatChannel, "chatChannel");
	stream->writeUint16(gameID, "gameID");
	stream->writeText(routerIP, "routerIP");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetCreateGameAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGameAccepted");
	chatChannel = stream->readUint32("chatChannel");
	gameID = stream->readUint16("gameID");
	routerIP = stream->readText("routerIP");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetCreateGameAccepted::format() const
{
	std::ostringstream s;
	s<<"NetCreateGameAccepted(chatChannel="<<chatChannel<<",gameID="<<gameID<<",routerIP="<<routerIP<<",fileID="<<fileID<<")";
	return s.str();
}



bool NetCreateGameAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGameAccepted))
	{
		const NetCreateGameAccepted& r = dynamic_cast<const NetCreateGameAccepted&>(rhs);
		if(chatChannel != r.chatChannel || gameID != r.gameID || routerIP != r.routerIP || fileID != r.fileID)
		{
			return false;
		}
		return true;
	}
	return false;
}



Uint32 NetCreateGameAccepted::getChatChannel() const
{
	return chatChannel;
}



Uint16 NetCreateGameAccepted::getGameID() const
{
	return gameID;
}



const std::string NetCreateGameAccepted::getGameRouterIP() const
{
	return routerIP;
}



Uint16 NetCreateGameAccepted::getFileID() const
{
	return fileID;
}



NetCreateGameRefused::NetCreateGameRefused()
{
	reason = YOGCreateRefusalUnknown;
}



NetCreateGameRefused::NetCreateGameRefused(YOGServerGameCreateRefusalReason reason)
	: reason(reason)
{

}



Uint8 NetCreateGameRefused::getMessageType() const
{
	return MNetCreateGameRefused;
}



void NetCreateGameRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGameRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetCreateGameRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGameRefused");
	reason = static_cast<YOGServerGameCreateRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetCreateGameRefused::format() const
{
	std::ostringstream s;
	s<<"NetCreateGameRefused(reason="<<reason<<")";
	return s.str();
}



bool NetCreateGameRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGameRefused))
	{
		const NetCreateGameRefused& r = dynamic_cast<const NetCreateGameRefused&>(rhs);
		if(reason == r.reason)
			return true;
	}
	return false;
}


YOGServerGameCreateRefusalReason NetCreateGameRefused::getRefusalReason() const
{
	return reason;
}



NetLeaveGame::NetLeaveGame()
{

}



Uint8 NetLeaveGame::getMessageType() const
{
	return MNetLeaveGame;
}



void NetLeaveGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetLeaveGame");
	stream->writeLeaveSection();
}



void NetLeaveGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetLeaveGame");
	stream->readLeaveSection();
}



std::string NetLeaveGame::format() const
{
	std::ostringstream s;
	s<<"NetLeaveGame()";
	return s.str();
}



bool NetLeaveGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetLeaveGame))
	{
		//const NetLeaveGame& r = dynamic_cast<const NetLeaveGame&>(rhs);
		return true;
	}
	return false;
}
