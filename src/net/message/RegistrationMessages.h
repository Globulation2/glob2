// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"

/// Client -> server account registration request.
class NetAttemptRegistration : public NetMessage
{
public:
	NetAttemptRegistration();
	NetAttemptRegistration(const std::string& username, const std::string& password);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	std::string getUsername() const;
	std::string getPassword() const;
private:
	std::string username;
	std::string password;
};


/// Server -> client: registration accepted.
class NetAcceptRegistration : public NetMessage
{
public:
	NetAcceptRegistration();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};


/// Server -> client: registration denied, carrying the reason.
class NetRefuseRegistration : public NetMessage
{
public:
	NetRefuseRegistration();
	NetRefuseRegistration(YOGLoginState reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGLoginState getRefusalReason() const;
private:
	YOGLoginState reason;
};
