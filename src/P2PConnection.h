/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef P2PConnection_h
#define P2PConnection_h

#include "P2PInformation.h"
#include "P2PPlayerInformation.h"
#include "boost/shared_ptr.hpp"
#include "NetListener.h"
#include <vector>

class YOGClient;
class NetMessage;

///A P2P connection
class P2PConnection
{
public:
	///Creates the P2P connection. P2P connections go through the YOGClient in order to communicate with the P2P manager
	P2PConnection(YOGClient* client);

	///Recieves an incoming message from the P2P manager
	void recieveMessage(boost::shared_ptr<NetMessage> message);

	///Sends a message up the p2p connection
	void sendMessage(boost::shared_ptr<NetMessage> message);
	
	///Returns the port this p2p connection is listening for connections on. If this failed to open a port
	///for listening, the port returned is 0
	int getPort();

	///This resets the p2p connection
	void reset();
	
	///This updates the p2p connection, call this regularly
	void update();

private:
	///This function updates the P2PInformation
	void updateP2PInformation(const P2PInformation& newGroup);

	YOGClient* client;
	P2PInformation group;
	NetListener listener;
	int localPort;
	std::vector<boost::shared_ptr<NetConnection> > outgoing;
	enum OutgoingConnectionState
	{
		ReadyToTry,
		Attempting,
		Connected,
		Local
	};
	std::vector<OutgoingConnectionState > outgoingStates;
	
	
	std::vector<boost::shared_ptr<NetConnection> > incoming;
	boost::shared_ptr<NetConnection> localIncoming;
};

#endif

