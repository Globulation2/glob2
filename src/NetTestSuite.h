/*
 Copyright (C) 2007 Bradley Arsenault

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __NetTestSuite_h
#define __NetTestSuite_h

#include "NetMessage.h"
#include "NetListener.h"
#include "NetConnection.h"
#include "YOGGameInfo.h"
#include "YOGMessage.h"
#include <boost/shared_ptr.hpp>

using namespace boost;

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
	template<typename t> bool testSerialize(shared_ptr<t> message);

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

	///Tests NetAttemptRegistration
	int testNetAttemptRegistration();

	///Tests NetAcceptRegistration
	int testNetAcceptRegistration();

	///Tests NetRefuseRegistration
	int testNetRefuseRegistration();

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

#endif
