// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"

/// Game-router -> matchmaker: announce that this router is ready to host games.
class NetRegisterRouter : public NetMessage
{
public:
	NetRegisterRouter();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

/// Matchmaker -> router: registration acknowledged.
class NetAcknowledgeRouter : public NetMessage
{
public:
	NetAcknowledgeRouter();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

/// Matchmaker -> router: assign a specific gameID to this router connection.
class NetSetGameInRouter : public NetMessage
{
public:
	NetSetGameInRouter();
	NetSetGameInRouter(Uint16 gameID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getGameID() const;
private:
	Uint16 gameID;
};
