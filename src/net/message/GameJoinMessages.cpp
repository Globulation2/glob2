// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameJoinMessages.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetAttemptJoinGame::NetAttemptJoinGame()
{
	gameID = 0;
}



NetAttemptJoinGame::NetAttemptJoinGame(Uint16 gameID)
	: gameID(gameID)
{

}



Uint8 NetAttemptJoinGame::getMessageType() const
{
	return MNetAttemptJoinGame;
}



void NetAttemptJoinGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAttemptJoinGame");
	stream->writeUint16(gameID, "gameID");
	stream->writeLeaveSection();
}



void NetAttemptJoinGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptJoinGame");
	gameID=stream->readUint16("gameID");
	stream->readLeaveSection();
}



std::string NetAttemptJoinGame::format() const
{
	std::ostringstream s;
	s<<"NetAttemptJoinGame(gameID="<<gameID<<")";
	return s.str();
}



bool NetAttemptJoinGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAttemptJoinGame))
	{
		const NetAttemptJoinGame& r = dynamic_cast<const NetAttemptJoinGame&>(rhs);
		if(r.gameID == gameID)
			return true;
	}
	return false;
}



Uint16 NetAttemptJoinGame::getGameID() const
{
	return gameID;
}



NetGameJoinAccepted::NetGameJoinAccepted()
{
	chatChannel = 0;
}



NetGameJoinAccepted::NetGameJoinAccepted(Uint32 chatChannel)
	: chatChannel(chatChannel)
{

}



Uint8 NetGameJoinAccepted::getMessageType() const
{
	return MNetGameJoinAccepted;
}



void NetGameJoinAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetGameJoinAccepted");
	stream->writeUint32(chatChannel, "chatChannel");
	stream->writeLeaveSection();
}



void NetGameJoinAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetGameJoinAccepted");
	chatChannel = stream->readUint32("chatChannel");
	stream->readLeaveSection();
}



std::string NetGameJoinAccepted::format() const
{
	std::ostringstream s;
	s<<"NetGameJoinAccepted(chatChannel="<<chatChannel<<")";
	return s.str();
}



bool NetGameJoinAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetGameJoinAccepted))
	{
		const NetGameJoinAccepted& r = dynamic_cast<const NetGameJoinAccepted&>(rhs);
		if(r.chatChannel != chatChannel)
		{
			return false;
		}
		return true;
	}
	return false;
}



Uint32 NetGameJoinAccepted::getChatChannel() const
{
	return chatChannel;
}



NetGameJoinRefused::NetGameJoinRefused()
{
	reason = YOGJoinRefusalUnknown;
}



NetGameJoinRefused::NetGameJoinRefused(YOGServerGameJoinRefusalReason reason)
	: reason(reason)
{

}



Uint8 NetGameJoinRefused::getMessageType() const
{
	return MNetGameJoinRefused;
}



void NetGameJoinRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetGameJoinRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetGameJoinRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetGameJoinRefused");
	reason=static_cast<YOGServerGameJoinRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetGameJoinRefused::format() const
{
	std::ostringstream s;
	std::string sreason;
	if(reason == YOGJoinRefusalUnknown)
		sreason="YOGJoinRefusalUnknown";
	s<<"NetGameJoinRefused(reason="<<sreason<<")";
	return s.str();
}



bool NetGameJoinRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetGameJoinRefused))
	{
		const NetGameJoinRefused& r = dynamic_cast<const NetGameJoinRefused&>(rhs);
		if(r.reason == reason)
			return true;
	}
	return false;
}




YOGServerGameJoinRefusalReason NetGameJoinRefused::getRefusalReason() const
{
	return reason;
}



NetPlayerJoinsGame::NetPlayerJoinsGame()
	: playerID(0), playerName("")
{

}



NetPlayerJoinsGame::NetPlayerJoinsGame(Uint16 playerID, std::string playerName)
	:playerID(playerID), playerName(playerName)
{
}



Uint8 NetPlayerJoinsGame::getMessageType() const
{
	return MNetPlayerJoinsGame;
}



void NetPlayerJoinsGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPlayerJoinsGame");
	stream->writeUint16(playerID, "playerID");
	stream->writeText(playerName, "playerName");
	stream->writeLeaveSection();
}



void NetPlayerJoinsGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPlayerJoinsGame");
	playerID = stream->readUint16("playerID");
	playerName = stream->readText("playerName");
	stream->readLeaveSection();
}



std::string NetPlayerJoinsGame::format() const
{
	std::ostringstream s;
	s<<"NetPlayerJoinsGame("<<"playerID="<<playerID<<"; "<<"playerName="<<playerName<<"; "<<")";
	return s.str();
}



bool NetPlayerJoinsGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPlayerJoinsGame))
	{
		const NetPlayerJoinsGame& r = dynamic_cast<const NetPlayerJoinsGame&>(rhs);
		if(r.playerID == playerID && r.playerName == playerName)
			return true;
	}
	return false;
}


Uint16 NetPlayerJoinsGame::getPlayerID() const
{
	return playerID;
}



std::string NetPlayerJoinsGame::getPlayerName() const
{
	return playerName;
}



NetSendAfterJoinGameInformation::NetSendAfterJoinGameInformation()
	: info()
{

}



NetSendAfterJoinGameInformation::NetSendAfterJoinGameInformation(YOGAfterJoinGameInformation info)
	:info(info)
{
}



Uint8 NetSendAfterJoinGameInformation::getMessageType() const
{
	return MNetSendAfterJoinGameInformation;
}



void NetSendAfterJoinGameInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendAfterJoinGameInformation");
	info.encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendAfterJoinGameInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendAfterJoinGameInformation");
	info.decodeData(stream);
	stream->readLeaveSection();
}



std::string NetSendAfterJoinGameInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendAfterJoinGameInformation("<<"="<<"; "<<")";
	return s.str();
}



bool NetSendAfterJoinGameInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendAfterJoinGameInformation))
	{
		const NetSendAfterJoinGameInformation& r = dynamic_cast<const NetSendAfterJoinGameInformation&>(rhs);
		if(r.info == info)
			return true;
	}
	return false;
}


YOGAfterJoinGameInformation NetSendAfterJoinGameInformation::getAfterJoinGameInformation() const
{
	return info;
}
