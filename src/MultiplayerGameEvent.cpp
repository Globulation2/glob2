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

#include "MultiplayerGameEvent.h"
#include <sstream>
#include <typeinfo>

MGPlayerListChangedEvent::MGPlayerListChangedEvent()
{
}



Uint8 MGPlayerListChangedEvent::getEventType() const
{
	return MGEPlayerListChanged;
}



std::string MGPlayerListChangedEvent::format() const
{
	std::ostringstream s;
	s<<"MGPlayerListChangedEvent()";
	return s.str();
}



bool MGPlayerListChangedEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGPlayerListChangedEvent))
	{
		//const MGPlayerListChangedEvent& r = dynamic_cast<const MGPlayerListChangedEvent&>(rhs);
		return true;
	}
	return false;
}


MGReadyToStartEvent::MGReadyToStartEvent()
{
}



Uint8 MGReadyToStartEvent::getEventType() const
{
	return MGEReadyToStart;
}



std::string MGReadyToStartEvent::format() const
{
	std::ostringstream s;
	s<<"MGReadyToStartEvent()";
	return s.str();
}



bool MGReadyToStartEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGReadyToStartEvent))
	{
		//const MGReadyToStartEvent& r = dynamic_cast<const MGReadyToStartEvent&>(rhs);
		return true;
	}
	return false;
}


MGNotReadyToStartEvent::MGNotReadyToStartEvent()
{
}



Uint8 MGNotReadyToStartEvent::getEventType() const
{
	return MGENotReadyToStart;
}



std::string MGNotReadyToStartEvent::format() const
{
	std::ostringstream s;
	s<<"MGNotReadyToStartEvent()";
	return s.str();
}



bool MGNotReadyToStartEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGNotReadyToStartEvent))
	{
		//const MGNotReadyToStartEvent& r = dynamic_cast<const MGNotReadyToStartEvent&>(rhs);
		return true;
	}
	return false;
}


MGGameExitEvent::MGGameExitEvent()
{
}



Uint8 MGGameExitEvent::getEventType() const
{
	return MGEGameExit;
}



std::string MGGameExitEvent::format() const
{
	std::ostringstream s;
	s<<"MGGameExitEvent()";
	return s.str();
}



bool MGGameExitEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGGameExitEvent))
	{
		//const MGGameExitEvent& r = dynamic_cast<const MGGameExitEvent&>(rhs);
		return true;
	}
	return false;
}


MGGameEndedNormallyEvent::MGGameEndedNormallyEvent()
{
}



Uint8 MGGameEndedNormallyEvent::getEventType() const
{
	return MGEGameEndedNormally;
}



std::string MGGameEndedNormallyEvent::format() const
{
	std::ostringstream s;
	s<<"MGGameEndedNormallyEvent()";
	return s.str();
}



bool MGGameEndedNormallyEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGGameEndedNormallyEvent))
	{
		//const MGGameEndedNormallyEvent& r = dynamic_cast<const MGGameEndedNormallyEvent&>(rhs);
		return true;
	}
	return false;
}


MGGameRefusedEvent::MGGameRefusedEvent()
{
}



Uint8 MGGameRefusedEvent::getEventType() const
{
	return MGEGameRefused;
}



std::string MGGameRefusedEvent::format() const
{
	std::ostringstream s;
	s<<"MGGameRefusedEvent()";
	return s.str();
}



bool MGGameRefusedEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGGameRefusedEvent))
	{
		//const MGGameRefusedEvent& r = dynamic_cast<const MGGameRefusedEvent&>(rhs);
		return true;
	}
	return false;
}


MGKickedByHostEvent::MGKickedByHostEvent()
{
}



Uint8 MGKickedByHostEvent::getEventType() const
{
	return MGEKickedByHost;
}



std::string MGKickedByHostEvent::format() const
{
	std::ostringstream s;
	s<<"MGKickedByHostEvent()";
	return s.str();
}



bool MGKickedByHostEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGKickedByHostEvent))
	{
		//const MGKickedByHostEvent& r = dynamic_cast<const MGKickedByHostEvent&>(rhs);
		return true;
	}
	return false;
}


MGHostCancelledGameEvent::MGHostCancelledGameEvent()
{
}



Uint8 MGHostCancelledGameEvent::getEventType() const
{
	return MGEHostCancelledGame;
}



std::string MGHostCancelledGameEvent::format() const
{
	std::ostringstream s;
	s<<"MGHostCancelledGameEvent()";
	return s.str();
}



bool MGHostCancelledGameEvent::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGHostCancelledGameEvent))
	{
		//const MGHostCancelledGameEvent& r = dynamic_cast<const MGHostCancelledGameEvent&>(rhs);
		return true;
	}
	return false;
}


MGGameStarted::MGGameStarted()
{
}



Uint8 MGGameStarted::getEventType() const
{
	return MGEGameStarted;
}



std::string MGGameStarted::format() const
{
	std::ostringstream s;
	s<<"MGGameStarted()";
	return s.str();
}



bool MGGameStarted::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGGameStarted))
	{
		//const MGGameStarted& r = dynamic_cast<const MGGameStarted&>(rhs);
		return true;
	}
	return false;
}


MGServerDisconnected::MGServerDisconnected()
{
}



Uint8 MGServerDisconnected::getEventType() const
{
	return MGEServerDisconnected;
}



std::string MGServerDisconnected::format() const
{
	std::ostringstream s;
	s<<"MGServerDisconnected()";
	return s.str();
}



bool MGServerDisconnected::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGServerDisconnected))
	{
		//const MGServerDisconnected& r = dynamic_cast<const MGServerDisconnected&>(rhs);
		return true;
	}
	return false;
}


MGGameStartRefused::MGGameStartRefused()
{
}



Uint8 MGGameStartRefused::getEventType() const
{
	return MGEGameStartRefused;
}



std::string MGGameStartRefused::format() const
{
	std::ostringstream s;
	s<<"MGGameStartRefused()";
	return s.str();
}



bool MGGameStartRefused::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGGameStartRefused))
	{
		//const MGGameStartRefused& r = dynamic_cast<const MGGameStartRefused&>(rhs);
		return true;
	}
	return false;
}


MGGameHostJoinAccepted::MGGameHostJoinAccepted()
{
}



Uint8 MGGameHostJoinAccepted::getEventType() const
{
	return MGEGameHostJoinAccepted;
}



std::string MGGameHostJoinAccepted::format() const
{
	std::ostringstream s;
	s<<"MGGameHostJoinAccepted()";
	return s.str();
}



bool MGGameHostJoinAccepted::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGGameHostJoinAccepted))
	{
		//const MGGameHostJoinAccepted& r = dynamic_cast<const MGGameHostJoinAccepted&>(rhs);
		return true;
	}
	return false;
}


MGDownloadPercentUpdate::MGDownloadPercentUpdate(Uint8 percent)
	: percent(percent)
{
}



Uint8 MGDownloadPercentUpdate::getEventType() const
{
	return MGEDownloadPercentUpdate;
}



std::string MGDownloadPercentUpdate::format() const
{
	std::ostringstream s;
	s<<"MGDownloadPercentUpdate("<<"percent="<<percent<<"; "<<")";
	return s.str();
}



bool MGDownloadPercentUpdate::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGDownloadPercentUpdate))
	{
		const MGDownloadPercentUpdate& r = dynamic_cast<const MGDownloadPercentUpdate&>(rhs);
		if(r.percent == percent)
			return true;
	}
	return false;
}


Uint8 MGDownloadPercentUpdate::getPercentFinished() const
{
	return percent;
}



//code_append_marker
