// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "NetReteamingInformation.h"
#include "YOGConsts.h"

/// Host -> server -> all: add an AI player of the given type to the game.
class NetAddAI : public NetMessage
{
public:
	NetAddAI();
	NetAddAI(Uint8 type);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint8 getType() const;
private:
	Uint8 type;
};

/// Host -> server -> all: remove the AI in the given player slot.
class NetRemoveAI : public NetMessage
{
public:
	NetRemoveAI();
	NetRemoveAI(Uint8 playerNum);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint8 getPlayerNumber() const;
private:
	Uint8 playerNum;
};

/// Reassigns a player to a different team.
class NetChangePlayersTeam : public NetMessage
{
public:
	NetChangePlayersTeam();
	NetChangePlayersTeam(Uint8 player, Uint8 team);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint8 getPlayer() const;
	Uint8 getTeam() const;
private:
	Uint8 player;
	Uint8 team;
};

/// Carries reteaming information used to balance teams in a rematched game.
class NetSendReteamingInformation : public NetMessage
{
public:
	NetSendReteamingInformation();
	NetSendReteamingInformation(NetReteamingInformation reteamingInfo);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	NetReteamingInformation getReteamingInfo() const;
private:
	NetReteamingInformation reteamingInfo;
};

/// Reports the result of a finished game (used by ratings/stats tracking).
class NetSendGameResult : public NetMessage
{
public:
	NetSendGameResult();
	NetSendGameResult(YOGGameResult result);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGGameResult getGameResult() const;
private:
	YOGGameResult result;
};
