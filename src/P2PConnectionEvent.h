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

#ifndef P2PConnectionEvent_h
#define P2PConnectionEvent_h

#include "SDL_net.h"
#include "boost/shared_ptr.hpp"

class NetMessage;

enum P2PConnectionEventType
{
	P2PERecievedMessage,
	//type_append_marker
};


///This represents a generic p2p event
class P2PConnectionEvent
{
public:
	///Destructor
	virtual ~P2PConnectionEvent() {}

	///Returns the event type
	virtual Uint8 getEventType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two P2PConnectionEvent
	virtual bool operator==(const P2PConnectionEvent& rhs) const = 0;
};


///P2PRecievedMessage
class P2PRecievedMessage : public P2PConnectionEvent
{
public:
	///Creates a P2PRecievedMessage event
	P2PRecievedMessage(boost::shared_ptr<NetMessage> message);

	///Returns P2PERecievedMessage
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two P2PConnectionEvent
	bool operator==(const P2PConnectionEvent& rhs) const;

	///Retrieves message
	boost::shared_ptr<NetMessage> getMessage() const;
private:
	boost::shared_ptr<NetMessage> message;
};



//event_append_marker

#endif
