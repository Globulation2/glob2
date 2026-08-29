// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"

/// Sends local protocol-version information to the server, used during the
/// handshake before login.
class NetSendClientInformation : public NetMessage
{
public:
	NetSendClientInformation();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	Uint16 getNetVersion() const;
private:
	Uint16 netVersion;
};


/// Server -> client information sent on connect: login policy (anonymous /
/// password required), game policy, and the connection's playerID.
class NetSendServerInformation : public NetMessage
{
public:
	NetSendServerInformation(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy, YOGPlayerID playerID);
	NetSendServerInformation();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGLoginPolicy getLoginPolicy() const;
	YOGGamePolicy getGamePolicy() const;
	YOGPlayerID getPlayerID() const;
private:
	YOGLoginPolicy loginPolicy;
	YOGGamePolicy gamePolicy;
	YOGPlayerID playerID;
};


/// Client -> server login attempt with username and password.
class NetAttemptLogin : public NetMessage
{
public:
	NetAttemptLogin(const std::string& username, const std::string& password);
	NetAttemptLogin();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	const std::string& getUsername() const;
	const std::string& getPassword() const;
private:
	std::string username;
	std::string password;
};


/// Tells the client that login succeeded.
class NetLoginSuccessful : public NetMessage
{
public:
	NetLoginSuccessful();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};


/// Tells the client that login was refused, carrying the reason.
class NetRefuseLogin : public NetMessage
{
public:
	NetRefuseLogin();
	NetRefuseLogin(YOGLoginState reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGLoginState getRefusalReason() const;
private:
	YOGLoginState reason;
};


/// Notifies the peer that this side is disconnecting.
class NetDisconnect : public NetMessage
{
public:
	NetDisconnect();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};
