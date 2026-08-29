// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"

/// Server -> game members: the game has started.
class NetStartGame : public NetMessage
{
public:
	NetStartGame();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};


/// Host -> server -> target: kick a player from the game with a reason.
class NetKickPlayer : public NetMessage
{
public:
	NetKickPlayer();
	NetKickPlayer(YOGPlayerID playerID, YOGKickReason reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGPlayerID getPlayerID() const;
	YOGKickReason getReason();
private:
	YOGPlayerID playerID;
	YOGKickReason reason;
};


/// Player -> host: this player's launch checklist has cleared. The host waits
/// for all players to be ready before issuing a NetStartGame.
class NetReadyToLaunch : public NetMessage
{
public:
	NetReadyToLaunch();
	NetReadyToLaunch(YOGPlayerID playerID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGPlayerID getPlayerID() const;
private:
	YOGPlayerID playerID;
};


/// Player -> host: previous "ready" was retracted (e.g. host changed map).
class NetNotReadyToLaunch : public NetMessage
{
public:
	NetNotReadyToLaunch();
	NetNotReadyToLaunch(YOGPlayerID playerID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGPlayerID getPlayerID() const;
private:
	YOGPlayerID playerID;
};


/// Host -> server: ask the server to start the game.
class NetRequestGameStart : public NetMessage
{
public:
	NetRequestGameStart();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};


/// Server -> host: refused to start the game (e.g. not all players ready).
class NetRefuseGameStart : public NetMessage
{
public:
	NetRefuseGameStart();
	NetRefuseGameStart(YOGServerGameStartRefusalReason refusalReason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGServerGameStartRefusalReason getRefusalReason() const;
private:
	YOGServerGameStartRefusalReason refusalReason;
};
