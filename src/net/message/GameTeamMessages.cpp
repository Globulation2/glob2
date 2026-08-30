// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameTeamMessages.h"
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetRemoveAI::NetRemoveAI()
	: playerNum(0)
{

}

NetRemoveAI::NetRemoveAI(Uint8 playerNum)
	:playerNum(playerNum)
{
}

Uint8 NetRemoveAI::getMessageType() const
{
	return MNetRemoveAI;
}

void NetRemoveAI::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRemoveAI");
	stream->writeUint8(playerNum, "playerNum");
	stream->writeLeaveSection();
}

void NetRemoveAI::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRemoveAI");
	playerNum = stream->readUint8("playerNum");
	stream->readLeaveSection();
}

std::string NetRemoveAI::format() const
{
	std::ostringstream s;
	s<<"NetRemoveAI("<<"playerNum="<<playerNum<<"; "<<")";
	return s.str();
}

bool NetRemoveAI::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRemoveAI))
	{
		const NetRemoveAI& r = dynamic_cast<const NetRemoveAI&>(rhs);
		if(r.playerNum == playerNum)
			return true;
	}
	return false;
}

Uint8 NetRemoveAI::getPlayerNumber() const
{
	return playerNum;
}

NetChangePlayersTeam::NetChangePlayersTeam()
	: player(0), team(0)
{

}

NetChangePlayersTeam::NetChangePlayersTeam(Uint8 player, Uint8 team)
	:player(player), team(team)
{
}

Uint8 NetChangePlayersTeam::getMessageType() const
{
	return MNetChangePlayersTeam;
}

void NetChangePlayersTeam::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetChangePlayersTeam");
	stream->writeUint8(player, "player");
	stream->writeUint8(team, "team");
	stream->writeLeaveSection();
}

void NetChangePlayersTeam::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetChangePlayersTeam");
	player = stream->readUint8("player");
	team = stream->readUint8("team");
	stream->readLeaveSection();
}

std::string NetChangePlayersTeam::format() const
{
	std::ostringstream s;
	s<<"NetChangePlayersTeam("<<"player="<<player<<"; "<<"team="<<team<<"; "<<")";
	return s.str();
}

bool NetChangePlayersTeam::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetChangePlayersTeam))
	{
		const NetChangePlayersTeam& r = dynamic_cast<const NetChangePlayersTeam&>(rhs);
		if(r.player == player && r.team == team)
			return true;
	}
	return false;
}

Uint8 NetChangePlayersTeam::getPlayer() const
{
	return player;
}

Uint8 NetChangePlayersTeam::getTeam() const
{
	return team;
}

NetAddAI::NetAddAI()
	: type(0)
{

}

NetAddAI::NetAddAI(Uint8 type)
	:type(type)
{
}

Uint8 NetAddAI::getMessageType() const
{
	return MNetAddAI;
}

void NetAddAI::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAddAI");
	stream->writeUint8(type, "type");
	stream->writeLeaveSection();
}

void NetAddAI::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAddAI");
	type = stream->readUint8("type");
	stream->readLeaveSection();
}

std::string NetAddAI::format() const
{
	std::ostringstream s;
	s<<"NetAddAI("<<"type="<<type<<"; "<<")";
	return s.str();
}

bool NetAddAI::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAddAI))
	{
		const NetAddAI& r = dynamic_cast<const NetAddAI&>(rhs);
		if(r.type == type)
			return true;
	}
	return false;
}

Uint8 NetAddAI::getType() const
{
	return type;
}

NetSendReteamingInformation::NetSendReteamingInformation()
{

}

NetSendReteamingInformation::NetSendReteamingInformation(NetReteamingInformation reteamingInfo)
	:reteamingInfo(reteamingInfo)
{
}

Uint8 NetSendReteamingInformation::getMessageType() const
{
	return MNetSendReteamingInformation;
}

void NetSendReteamingInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendReteamingInformation");
	reteamingInfo.encodeData(stream);
	stream->writeLeaveSection();
}

void NetSendReteamingInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendReteamingInformation");
	reteamingInfo.decodeData(stream);
	stream->readLeaveSection();
}

std::string NetSendReteamingInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendReteamingInformation()";
	return s.str();
}

bool NetSendReteamingInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendReteamingInformation))
	{
		const NetSendReteamingInformation& r = dynamic_cast<const NetSendReteamingInformation&>(rhs);
		if(r.reteamingInfo == reteamingInfo)
			return true;
	}
	return false;
}

NetReteamingInformation NetSendReteamingInformation::getReteamingInfo() const
{
	return reteamingInfo;
}

NetSendGameResult::NetSendGameResult()
	: result(YOGGameResultUnknown)
{

}

NetSendGameResult::NetSendGameResult(YOGGameResult result)
	:result(result)
{
}

Uint8 NetSendGameResult::getMessageType() const
{
	return MNetSendGameResult;
}

void NetSendGameResult::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendGameResult");
	stream->writeUint8(static_cast<Uint8>(result), "result");
	stream->writeLeaveSection();
}

void NetSendGameResult::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendGameResult");
	result = static_cast<YOGGameResult>(stream->readUint8("result"));
	stream->readLeaveSection();
}

std::string NetSendGameResult::format() const
{
	std::ostringstream s;
	s<<"NetSendGameResult("<<"result="<<result<<"; "<<")";
	return s.str();
}

bool NetSendGameResult::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendGameResult))
	{
		const NetSendGameResult& r = dynamic_cast<const NetSendGameResult&>(rhs);
		if(r.result == result)
			return true;
	}
	return false;
}

YOGGameResult NetSendGameResult::getGameResult() const
{
	return result;
}
