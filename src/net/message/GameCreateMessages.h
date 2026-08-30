// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"

/// Client -> server: create a new lobby game with the given display name.
class NetCreateGame : public NetMessage
{
public:
	NetCreateGame(const std::string& gameName);
	NetCreateGame();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	const std::string& getGameName() const;
private:
	std::string gameName;
};

/// Server -> creator: game created successfully. Carries the new game's chat
/// channel id, game id, the address of the assigned game-router, and the
/// fileID for the map distribution.
class NetCreateGameAccepted : public NetMessage
{
public:
	NetCreateGameAccepted();
	NetCreateGameAccepted(Uint32 chatChannel, Uint16 gameID, const std::string& routerIP, Uint16 fileID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint32 getChatChannel() const;
	Uint16 getGameID() const;
	const std::string getGameRouterIP() const;
	Uint16 getFileID() const;
private:
	Uint32 chatChannel;
	Uint16 gameID;
	std::string routerIP;
	Uint16 fileID;
};

/// Server -> creator: game creation refused, carrying the reason.
class NetCreateGameRefused : public NetMessage
{
public:
	NetCreateGameRefused();
	NetCreateGameRefused(YOGServerGameCreateRefusalReason reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGServerGameCreateRefusalReason getRefusalReason() const;
private:
	YOGServerGameCreateRefusalReason reason;
};

/// Client -> server: leave the currently-joined game.
class NetLeaveGame : public NetMessage
{
public:
	NetLeaveGame();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};
