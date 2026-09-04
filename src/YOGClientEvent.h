// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

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
	YEPlayerBanned,
	YEIPBanned,
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




///YOGPlayerBannedEvent
class YOGPlayerBannedEvent : public YOGClientEvent
{
public:
	///Creates a YOGPlayerBannedEvent event
	YOGPlayerBannedEvent();

	///Returns YEPlayerBanned
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGClientEvent& rhs) const;
};




///YOGIPBannedEvent
class YOGIPBannedEvent : public YOGClientEvent
{
public:
	///Creates a YOGIPBannedEvent event
	YOGIPBannedEvent();

	///Returns YEIPBanned
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two YOGEvent
	bool operator==(const YOGClientEvent& rhs) const;
};



//event_append_marker

#endif
