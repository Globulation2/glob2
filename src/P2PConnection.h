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

private:
	YOGClient* client;
	P2PInformation group;
};

#endif

