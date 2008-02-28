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

#ifndef __YOGClientEvent_h
#define __YOGClientEvent_h

#include <string>
#include "SDL_net.h"
#include "YOGConsts.h"

enum YOGClientEventType
{
	YEConnected,
	YEConnectionLost,
	YELoginAccepted,
	YELoginRefused,
	//type_append_marker
};


///This represents an event recieved from  YOGClient
///These are merely data classes, and not much more
class YOGClientEvent
{
public:
	virtual ~YOGClientEvent() {}

	///Returns the event type
	virtual Uint8 getEventType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two YOGClientEvent
	virtual bool operator==(const YOGClientEvent& rhs) const = 0;
};




///YOGConnectedEvent
class YOGConnectedEvent : public YOGClientEvent
{
public:
	///Creates a YOGConnectedEvent event
	YOGConnectedEvent();

	///Returns YEConnected
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGClientEvent
	bool operator==(const YOGClientEvent& rhs) const;
};




///YOGConnectionLostEvent
class YOGConnectionLostEvent : public YOGClientEvent
{
public:
	///Creates a YOGConnectionLostEvent event
	YOGConnectionLostEvent();

	///Returns YEConnectionLost
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGClientEvent
	bool operator==(const YOGClientEvent& rhs) const;
};




///YOGLoginAcceptedEvent
class YOGLoginAcceptedEvent : public YOGClientEvent
{
public:
	///Creates a YOGLoginAcceptedEvent event
	YOGLoginAcceptedEvent();

	///Returns YELoginAccepted
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGClientEvent
	bool operator==(const YOGClientEvent& rhs) const;
};




///YOGLoginRefusedEvent
class YOGLoginRefusedEvent : public YOGClientEvent
{
public:
	///Creates a YOGLoginRefusedEvent event
	YOGLoginRefusedEvent(YOGLoginState reason);

	///Returns YELoginRefused
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGClientEvent
	bool operator==(const YOGClientEvent& rhs) const;

	///Retrieves reason
	YOGLoginState getReason() const;
private:
	YOGLoginState reason;
};



//event_append_marker

#endif
