// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "NetMessage.h"
#include "NetMessageType.h"
#include "YOGConsts.h"

/// Admin -> router: authenticate as a router administrator using a password.
class NetRouterAdministratorLogin : public NetMessage
{
public:
	NetRouterAdministratorLogin();
	NetRouterAdministratorLogin(std::string password);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	std::string getPassword() const;
private:
	std::string password;
};


/// Admin -> router: send an administrative command (kick, ban, status, etc).
class NetRouterAdministratorSendCommand : public NetMessage
{
public:
	NetRouterAdministratorSendCommand();
	NetRouterAdministratorSendCommand(std::string command);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	std::string getCommand() const;
private:
	std::string command;
};


/// Router -> admin: textual response to an administrative command.
class NetRouterAdministratorSendText : public NetMessage
{
public:
	NetRouterAdministratorSendText();
	NetRouterAdministratorSendText(std::string text);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	std::string getText() const;
private:
	std::string text;
};


/// Router -> admin: login accepted.
class NetRouterAdministratorLoginAccepted : public NetMessage
{
public:
	NetRouterAdministratorLoginAccepted();

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;
};


/// Router -> admin: login refused, carrying the reason.
class NetRouterAdministratorLoginRefused : public NetMessage
{
public:
	NetRouterAdministratorLoginRefused();
	NetRouterAdministratorLoginRefused(YOGRouterAdministratorLoginRefusalReason reason);

	Uint8 getMessageType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream);
	std::string format() const;
	bool operator==(const NetMessage& rhs) const;

	YOGRouterAdministratorLoginRefusalReason getReason() const;
private:
	YOGRouterAdministratorLoginRefusalReason reason;
};
