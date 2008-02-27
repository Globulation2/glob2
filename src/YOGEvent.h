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

#ifndef __YOGEvent_h
#define __YOGEvent_h

#include <string>
#include "SDL_net.h"
#include "YOGConsts.h"

enum YOGEventType
{
	YEConnected,
	YEConnectionLost,
	YELoginAccepted,
	YELoginRefused,
	YEPlayerListUpdated,
	//type_append_marker
};


///This represents an event recieved from  YOGClient
///These are merely data classes, and not much more
class YOGEvent
{
public:
	virtual ~YOGEvent() {}

	///Returns the event type
	virtual Uint8 getEventType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two YOGEvent
	virtual bool operator==(const YOGEvent& rhs) const = 0;
};




///YOGConnectedEvent
class YOGConnectedEvent : public YOGEvent
{
public:
	///Creates a YOGConnectedEvent event
	YOGConnectedEvent();

	///Returns YEConnected
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGEvent& rhs) const;
};




///YOGConnectionLostEvent
class YOGConnectionLostEvent : public YOGEvent
{
public:
	///Creates a YOGConnectionLostEvent event
	YOGConnectionLostEvent();

	///Returns YEConnectionLost
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGEvent& rhs) const;
};




///YOGLoginAcceptedEvent
class YOGLoginAcceptedEvent : public YOGEvent
{
public:
	///Creates a YOGLoginAcceptedEvent event
	YOGLoginAcceptedEvent();

	///Returns YELoginAccepted
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGEvent& rhs) const;
};




///YOGLoginRefusedEvent
class YOGLoginRefusedEvent : public YOGEvent
{
public:
	///Creates a YOGLoginRefusedEvent event
	YOGLoginRefusedEvent(YOGLoginState reason);

	///Returns YELoginRefused
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGEvent& rhs) const;

	///Retrieves reason
	YOGLoginState getReason() const;
private:
	YOGLoginState reason;
};




///YOGPlayerListUpdatedEvent
class YOGPlayerListUpdatedEvent : public YOGEvent
{
public:
	///Creates a YOGPlayerListUpdatedEvent event
	YOGPlayerListUpdatedEvent();

	///Returns YEPlayerListUpdated
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGEvent& rhs) const;
};



//event_append_marker

#endif
