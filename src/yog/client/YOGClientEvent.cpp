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

#include "YOGClientEvent.h"
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



bool YOGConnectedEvent::operator==(const YOGClientEvent& rhs) const
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



bool YOGConnectionLostEvent::operator==(const YOGClientEvent& rhs) const
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



bool YOGLoginAcceptedEvent::operator==(const YOGClientEvent& rhs) const
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



bool YOGLoginRefusedEvent::operator==(const YOGClientEvent& rhs) const
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

YOGPlayerBannedEvent::YOGPlayerBannedEvent()
{
}



Uint8 YOGPlayerBannedEvent::getEventType() const
{
	return YEPlayerBanned;
}



std::string YOGPlayerBannedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGPlayerBannedEvent()";
	return s.str();
}



bool YOGPlayerBannedEvent::operator==(const YOGClientEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGPlayerBannedEvent))
	{
		//const YOGPlayerBannedEvent& r = dynamic_cast<const YOGPlayerBannedEvent&>(rhs);
		return true;
	}
	return false;
}


YOGIPBannedEvent::YOGIPBannedEvent()
{
}



Uint8 YOGIPBannedEvent::getEventType() const
{
	return YEIPBanned;
}



std::string YOGIPBannedEvent::format() const
{
	std::ostringstream s;
	s<<"YOGIPBannedEvent()";
	return s.str();
}



bool YOGIPBannedEvent::operator==(const YOGClientEvent& rhs) const
{
	if(typeid(rhs)==typeid(YOGIPBannedEvent))
	{
		//const YOGIPBannedEvent& r = dynamic_cast<const YOGIPBannedEvent&>(rhs);
		return true;
	}
	return false;
}


//code_append_marker
