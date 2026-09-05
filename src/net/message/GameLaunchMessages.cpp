// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameLaunchMessages.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetStartGame::NetStartGame()
{

}

Uint8 NetStartGame::getMessageType() const
{
	return MNetStartGame;
}

void NetStartGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetStartGame");
	stream->writeLeaveSection();
}

void NetStartGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetStartGame");
	stream->readLeaveSection();
}

std::string NetStartGame::format() const
{
	std::ostringstream s;
	s<<"NetStartGame()";
	return s.str();
}

bool NetStartGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetStartGame))
	{
		//const NetStartGame& r = dynamic_cast<const NetStartGame&>(rhs);
		return true;
	}
	return false;
}

NetKickPlayer::NetKickPlayer()
	: playerID(0), reason(YOGUnknownKickReason)
{
}

NetKickPlayer::NetKickPlayer(YOGPlayerID playerID, YOGKickReason reason)
	: playerID(playerID), reason(reason)
{
}

Uint8 NetKickPlayer::getMessageType() const
{
	return MNetKickPlayer;
}

void NetKickPlayer::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetKickPlayer");
	stream->writeUint16(playerID, "playerID");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}

void NetKickPlayer::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetKickPlayer");
	playerID = stream->readUint16("playerID");
	reason = static_cast<YOGKickReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}

std::string NetKickPlayer::format() const
{
	std::ostringstream s;
	s<<"NetKickPlayer(playerID="<<playerID<<"; reason="<<reason<<")";
	return s.str();
}

bool NetKickPlayer::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetKickPlayer))
	{
		const NetKickPlayer& r = dynamic_cast<const NetKickPlayer&>(rhs);
		if(r.playerID == playerID && r.reason == reason)
			return true;
	}
	return false;
}

YOGPlayerID NetKickPlayer::getPlayerID() const
{
	return playerID;
}

YOGKickReason NetKickPlayer::getReason()
{
	return reason;
}

NetReadyToLaunch::NetReadyToLaunch()
	: playerID(0)
{

}

NetReadyToLaunch::NetReadyToLaunch(YOGPlayerID playerID)
	: playerID(playerID)
{
}

Uint8 NetReadyToLaunch::getMessageType() const
{
	return MNetReadyToLaunch;
}

void NetReadyToLaunch::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetReadyToLaunch");
	stream->writeUint16(playerID, "playerID");
	stream->writeLeaveSection();
}

void NetReadyToLaunch::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetReadyToLaunch");
	playerID = stream->readUint16("playerID");
	stream->readLeaveSection();
}

std::string NetReadyToLaunch::format() const
{
	std::ostringstream s;
	s<<"NetReadyToLaunch("<<"playerID="<<playerID<<"; "<<")";
	return s.str();
}

bool NetReadyToLaunch::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetReadyToLaunch))
	{
		const NetReadyToLaunch& r = dynamic_cast<const NetReadyToLaunch&>(rhs);
		if(r.playerID == playerID)
			return true;
	}
	return false;
}

YOGPlayerID NetReadyToLaunch::getPlayerID() const
{
	return playerID;
}

NetNotReadyToLaunch::NetNotReadyToLaunch()
	: playerID(0)
{

}

NetNotReadyToLaunch::NetNotReadyToLaunch(YOGPlayerID playerID)
	:playerID(playerID)
{
}

Uint8 NetNotReadyToLaunch::getMessageType() const
{
	return MNetNotReadyToLaunch;
}

void NetNotReadyToLaunch::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetNotReadyToLaunch");
	stream->writeUint16(playerID, "playerID");
	stream->writeLeaveSection();
}

void NetNotReadyToLaunch::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetNotReadyToLaunch");
	playerID = stream->readUint16("playerID");
	stream->readLeaveSection();
}

std::string NetNotReadyToLaunch::format() const
{
	std::ostringstream s;
	s<<"NetNotReadyToLaunch("<<"playerID="<<playerID<<"; "<<")";
	return s.str();
}

bool NetNotReadyToLaunch::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetNotReadyToLaunch))
	{
		const NetNotReadyToLaunch& r = dynamic_cast<const NetNotReadyToLaunch&>(rhs);
		if(r.playerID == playerID)
			return true;
	}
	return false;
}

YOGPlayerID NetNotReadyToLaunch::getPlayerID() const
{
	return playerID;
}

NetRequestGameStart::NetRequestGameStart()
{

}

Uint8 NetRequestGameStart::getMessageType() const
{
	return MNetRequestGameStart;
}

void NetRequestGameStart::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestGameStart");
	stream->writeLeaveSection();
}

void NetRequestGameStart::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestGameStart");
	stream->readLeaveSection();
}

std::string NetRequestGameStart::format() const
{
	std::ostringstream s;
	s<<"NetRequestGameStart()";
	return s.str();
}

bool NetRequestGameStart::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestGameStart))
	{
		//const NetRequestGameStart& r = dynamic_cast<const NetRequestGameStart&>(rhs);
		return true;
	}
	return false;
}

NetRefuseGameStart::NetRefuseGameStart()
	: refusalReason(YOGUnknownStartRefusalReason)
{

}

NetRefuseGameStart::NetRefuseGameStart(YOGServerGameStartRefusalReason refusalReason)
	:refusalReason(refusalReason)
{
}

Uint8 NetRefuseGameStart::getMessageType() const
{
	return MNetRefuseGameStart;
}

void NetRefuseGameStart::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseGameStart");
	stream->writeUint8(refusalReason, "refusalReason");
	stream->writeLeaveSection();
}

void NetRefuseGameStart::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseGameStart");
	refusalReason = static_cast<YOGServerGameStartRefusalReason>(stream->readUint8("refusalReason"));
	stream->readLeaveSection();
}

std::string NetRefuseGameStart::format() const
{
	std::ostringstream s;
	s<<"NetRefuseGameStart("<<"refusalReason="<<refusalReason<<"; "<<")";
	return s.str();
}

bool NetRefuseGameStart::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseGameStart))
	{
		const NetRefuseGameStart& r = dynamic_cast<const NetRefuseGameStart&>(rhs);
		if(r.refusalReason == refusalReason)
			return true;
	}
	return false;
}

YOGServerGameStartRefusalReason NetRefuseGameStart::getRefusalReason() const
{
	return refusalReason;
}
