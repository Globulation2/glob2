// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

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
