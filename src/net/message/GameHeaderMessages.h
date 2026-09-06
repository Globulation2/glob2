// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "GameHeader.h"
#include "MapHeader.h"

/// Sends the map header (terrain dimensions, victory conditions, etc.) to the
/// server when creating or joining a game.
class NetSendMapHeader : public NetMessage
{
public:
	NetSendMapHeader();
	NetSendMapHeader(const MapHeader& mapHeader);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	const MapHeader& getMapHeader() const;
private:
	MapHeader mapHeader;
};

/// Sends the game header WITHOUT player info — player data is sent separately
/// via NetSendGamePlayerInfo so each piece can be updated independently.
class NetSendGameHeader : public NetMessage
{
public:
	NetSendGameHeader();
	NetSendGameHeader(const GameHeader& gameHeader);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	void downloadToGameHeader(GameHeader& header);
private:
	GameHeader gameHeader;
};

/// Sends the BasePlayer portion of GameHeader (the player slots and their
/// settings). Companion to NetSendGameHeader.
class NetSendGamePlayerInfo : public NetMessage
{
public:
	NetSendGamePlayerInfo();
	NetSendGamePlayerInfo(GameHeader& header);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	void downloadToGameHeader(GameHeader& header);
private:
	GameHeader gameHeader;
};
