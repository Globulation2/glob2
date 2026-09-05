// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "AuthMessages.h"
#include "OrderMessages.h"
#include "RegistrationMessages.h"
#include "NetListener.h"
#include "NetConnection.h"
#include "YOGGameInfo.h"
#include "YOGMessage.h"
#include <memory>

///This is a basic test system for the low level net classes,
///NetConnection, NetListener, NetMessage, YOGGameInfo and YOGMessage
///When run, it is assumed that the host allows the program to listen on
///the port 30, and that it can connect to itself via localhost

// TODO: leo wandersleb 2010-04-11: This is not really a test-suite. It would
// need to be split up into a UnitTestSuite to test the code and an actual test
// for network and backend or whatever is needed to run yog.
class NetTestSuite
{
public:
	///Constructor takes no arguments. 
	NetTestSuite();

	///This generic test tests the serialization of a provided object
	///by serializing it, deserializing it, and testing for equality
	template<typename t> bool testSerialize(std::shared_ptr<t> message);

	///Tests that the initial states of two messages are equal
	template<typename t> bool testInitial();

	///Tests the various NetMessage classes. This makes sure
	///that the classes decode back to exactly what they where
	///encoded to.
	int testNetMessages();

	///Tests NetSendOrder
	int testNetSendOrder();

	///Tests NetSendClientInformation
	int testNetSendClientInformation();

	///Tests NetSendServerInformation
	int testNetSendServerInformation();

	///Tests NetAttemptLogin
	int testNetAttemptLogin();

	///Tests NetLoginSuccessful
	int testNetLoginSuccessful();

	///Tests NetRefuseLogin
	int testNetRefuseLogin();

	///Tests NetDisconnect
	int testNetDisconnect();

	///Tests NetRegistrationRequest
	int testNetRegistrationRequest();

	///Tests NetRegistrationAccepted
	int testNetRegistrationAccepted();

	///Tests NetRegistrationRefused
	int testNetRegistrationRefused();

	///Tests the YOGGameInfo class and its serialization
	int testYOGGameInfo();

	///Tests the YOGMessage class and its serialization
	int testYOGMessage();

	///Tests the YOGPlayerSessionInfo class and its serialization
	int testYOGPlayerSessionInfo();

	///Tests the NetReteamingInformation class and its serialization
	int testNetReteamingInformation();

	///This tests NetListener and NetConnection in tandem.
	int testListenerConnection();

	///Runs all of the tests. Outputs errors and failed tests to the console.
	///Returns true if all tests passed, false otherwise.
	bool runAllTests();
};

