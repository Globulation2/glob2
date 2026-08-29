// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGAfterJoinGameInformation.h"
#include "YOGConsts.h"

/// Client -> server: attempt to join a game by its game ID. Future versions
/// may add password-protected games, so a join attempt is not always granted.
class NetAttemptJoinGame : public NetMessage
{
public:
	NetAttemptJoinGame();
	NetAttemptJoinGame(Uint16 gameID);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getGameID() const;
private:
	Uint16 gameID;
};


/// Server -> client: join request accepted; the player is now in the game.
/// Carries the chat channel for the joined game.
class NetGameJoinAccepted : public NetMessage
{
public:
	NetGameJoinAccepted();
	NetGameJoinAccepted(Uint32 chatChannel);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint32 getChatChannel() const;
private:
	Uint32 chatChannel;
};


/// Server -> client: join attempt refused, carrying the reason.
class NetGameJoinRefused : public NetMessage
{
public:
	NetGameJoinRefused(YOGServerGameJoinRefusalReason reason);
	NetGameJoinRefused();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGServerGameJoinRefusalReason getRefusalReason() const;
private:
	YOGServerGameJoinRefusalReason reason;
};


/// Server -> game members: a new player just joined the game.
class NetPlayerJoinsGame : public NetMessage
{
public:
	NetPlayerJoinsGame();
	NetPlayerJoinsGame(YOGPlayerID playerID, std::string playerName);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGPlayerID getPlayerID() const;
	std::string getPlayerName() const;
private:
	YOGPlayerID playerID;
	std::string playerName;
};


/// Server -> joiner: post-join handoff information (game data not part of
/// the initial accept).
class NetSendAfterJoinGameInformation : public NetMessage
{
public:
	NetSendAfterJoinGameInformation();
	NetSendAfterJoinGameInformation(YOGAfterJoinGameInformation info);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGAfterJoinGameInformation getAfterJoinGameInformation() const;
private:
	YOGAfterJoinGameInformation info;
};
