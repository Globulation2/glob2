/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
#include <boost/shared_ptr.hpp>

using namespace boost;

///This is a basic unit test system for the low level net classes,
///NetConnection, NetListener, NetMessage, and YOGGameInfo
///When run, it is assumed that the host allows the program to listen on
//the port 30, and that it can connect to itself via localhost
class NetTestSuite
{
public:
	///Constructor takes no arguments. 
	NetTestSuite();

	///This generic test tests the serialization of a provided object
	///by serializing it, deserializing it, and testing for equality
	template<typename t> bool testMessage(shared_ptr<t> message);

	///Tests that the initial states of two messages are equal
	template<typename t> bool testInitialMessageState();

	///Tests the various NetMessage classes. This makes sure
	///that the classes decode back to exactly what they where
	///encoded to.
	int testNetMessages();

	///Tests the YOGGameInfo class and its serialization
	int testYOGGameInfo();

	///This tests NetListener and NetConnection in tandem.
	int testListenerConnection();

	///Runs all of the tests. Outputs errors and failed tests to the console.
	///Returns true if all tests passed, false otherwise.
	bool runAllTests();
};


#endif
