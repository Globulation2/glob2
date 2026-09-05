// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"

class NetRegistrationRequest : public NetMessage
{
public:
	NetRegistrationRequest();
	NetRegistrationRequest(const std::string& username, const std::string& password);

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

class NetRegistrationAccepted : public NetMessage
{
public:
	NetRegistrationAccepted();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};

class NetRegistrationRefused : public NetMessage
{
public:
	NetRegistrationRefused();
	NetRegistrationRefused(YOGLoginState reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGLoginState getRefusalReason() const;
private:
	YOGLoginState reason;
};
