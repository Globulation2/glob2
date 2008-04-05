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

#include "P2PConnectionEvent.h"
#include "NetMessage.h"
#include <sstream>


P2PRecievedMessage::P2PRecievedMessage(boost::shared_ptr<NetMessage> message)
	: message(message)
{
}



Uint8 P2PRecievedMessage::getEventType() const
{
	return P2PERecievedMessage;
}



std::string P2PRecievedMessage::format() const
{
	std::ostringstream s;
	s<<"P2PRecievedMessage("<<"message->getType()="<<message->getMessageType()<<"; "<<")";
	return s.str();
}



bool P2PRecievedMessage::operator==(const P2PConnectionEvent& rhs) const
{
	if(typeid(rhs)==typeid(P2PRecievedMessage))
	{
		const P2PRecievedMessage& r = dynamic_cast<const P2PRecievedMessage&>(rhs);
		if(r.message == message)
			return true;
	}
	return false;
}


boost::shared_ptr<NetMessage> P2PRecievedMessage::getMessage() const
{
	return message;
}



//code_append_marker
