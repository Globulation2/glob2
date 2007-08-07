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

#include "YOGEvent.h"
#include <typeinfo>
#include <sstream>


YOGConnectedEvent::YOGConnectedEvent()
{
}



Uint8 YOGConnectedEvent::getEventType() const
{
	return YEConnected;
}



std::string YOGConnectedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGConnectedEvent()";
	return s.str();
}



bool YOGConnectedEvent::operator==(const YOGEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGConnectedEvent))
	{
		//const YOGConnectedEvent& r = dynamic_cast<const YOGConnectedEvent&>(rhs);
		return true;
	}
	return false;
}


YOGConnectionLostEvent::YOGConnectionLostEvent()
{
}



Uint8 YOGConnectionLostEvent::getEventType() const
{
	return YEConnectionLost;
}



std::string YOGConnectionLostEvent::format() const
{
	std::ostringstream s;
	s<<"YOGConnectionLostEvent()";
	return s.str();
}



bool YOGConnectionLostEvent::operator==(const YOGEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGConnectionLostEvent))
	{
		//const YOGConnectionLostEvent& r = dynamic_cast<const YOGConnectionLostEvent&>(rhs);
		return true;
	}
	return false;
}


YOGLoginAcceptedEvent::YOGLoginAcceptedEvent()
{
}



Uint8 YOGLoginAcceptedEvent::getEventType() const
{
	return YELoginAccepted;
}



std::string YOGLoginAcceptedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGLoginAcceptedEvent()";
	return s.str();
}



bool YOGLoginAcceptedEvent::operator==(const YOGEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGLoginAcceptedEvent))
	{
		//const YOGLoginAcceptedEvent& r = dynamic_cast<const YOGLoginAcceptedEvent&>(rhs);
		return true;
	}
	return false;
}


YOGLoginRefusedEvent::YOGLoginRefusedEvent(YOGLoginState reason)
	: reason(reason)
{
}



Uint8 YOGLoginRefusedEvent::getEventType() const
{
	return YELoginRefused;
}



std::string YOGLoginRefusedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGLoginRefusedEvent("<<"reason="<<reason<<"; "<<")";
	return s.str();
}



bool YOGLoginRefusedEvent::operator==(const YOGEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGLoginRefusedEvent))
	{
		const YOGLoginRefusedEvent& r = dynamic_cast<const YOGLoginRefusedEvent&>(rhs);
		if(r.reason == reason)
			return true;
	}
	return false;
}


YOGLoginState YOGLoginRefusedEvent::getReason() const
{
	return reason;
}



YOGPlayerListUpdatedEvent::YOGPlayerListUpdatedEvent()
{
}



Uint8 YOGPlayerListUpdatedEvent::getEventType() const
{
	return YEPlayerListUpdated;
}



std::string YOGPlayerListUpdatedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGPlayerListUpdatedEvent()";
	return s.str();
}



bool YOGPlayerListUpdatedEvent::operator==(const YOGEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGPlayerListUpdatedEvent))
	{
		//const YOGPlayerListUpdatedEvent& r = dynamic_cast<const YOGPlayerListUpdatedEvent&>(rhs);
		return true;
	}
	return false;
}


YOGGameListUpdatedEvent::YOGGameListUpdatedEvent()
{
}



Uint8 YOGGameListUpdatedEvent::getEventType() const
{
	return YEGameListUpdated;
}



std::string YOGGameListUpdatedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGGameListUpdatedEvent()";
	return s.str();
}



bool YOGGameListUpdatedEvent::operator==(const YOGEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGGameListUpdatedEvent))
	{
		//const YOGGameListUpdatedEvent& r = dynamic_cast<const YOGGameListUpdatedEvent&>(rhs);
		return true;
	}
	return false;
}


//code_append_marker
