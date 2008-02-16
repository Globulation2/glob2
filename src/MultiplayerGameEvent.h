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

#ifndef __MultiplayerGameEvent_h
#define __MultiplayerGameEvent_h

#include <string>
#include "SDL_net.h"

enum MultiplayerGameEventType
{
	MGEPlayerListChanged,
	MGEReadyToStart,
	MGENotReadyToStart,
	MGEGameExit,
	MGEGameEndedNormally,
	MGEGameRefused,
	MGEKickedByHost,
	MGEHostCancelledGame,
	MGEGameStarted,
	MGEServerDisconnected,
	MGEGameStartRefused,
	MGEGameHostJoinAccepted,
	//type_append_marker
};


///This represents an event recieved from MultiplayerGame
///These are merely data classes, and not much more
class MultiplayerGameEvent
{
public:
	virtual ~MultiplayerGameEvent() {}

	///Returns the event type
	virtual Uint8 getEventType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two MultiplayerGameEvent
	virtual bool operator==(const MultiplayerGameEvent& rhs) const = 0;
};




///MGPlayerListChangedEvent
class MGPlayerListChangedEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGPlayerListChangedEvent event
	MGPlayerListChangedEvent();

	///Returns MGEPlayerListChanged
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGReadyToStartEvent
class MGReadyToStartEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGReadyToStartEvent event
	MGReadyToStartEvent();

	///Returns MGEReadyToStart
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGNotReadyToStartEvent
class MGNotReadyToStartEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGNotReadyToStartEvent event
	MGNotReadyToStartEvent();

	///Returns MGENotReadyToStart
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};



///MGGameExitEvent
class MGGameExitEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGGameExitEvent event
	MGGameExitEvent();

	///Returns MGEGameExit
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGGameEndedNormallyEvent
class MGGameEndedNormallyEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGGameEndedNormallyEvent event
	MGGameEndedNormallyEvent();

	///Returns MGEGameEndedNormally
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGGameRefusedEvent
class MGGameRefusedEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGGameRefusedEvent event
	MGGameRefusedEvent();

	///Returns MGEGameRefused
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGKickedByHostEvent
class MGKickedByHostEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGKickedByHostEvent event
	MGKickedByHostEvent();

	///Returns MGEKickedByHost
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGHostCancelledGameEvent
class MGHostCancelledGameEvent : public MultiplayerGameEvent
{
public:
	///Creates a MGHostCancelledGameEvent event
	MGHostCancelledGameEvent();

	///Returns MGEHostCancelledGame
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGGameStarted
class MGGameStarted : public MultiplayerGameEvent
{
public:
	///Creates a MGGameStarted event
	MGGameStarted();

	///Returns MGEGameStarted
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGServerDisconnected
class MGServerDisconnected : public MultiplayerGameEvent
{
public:
	///Creates a MGServerDisconnected event
	MGServerDisconnected();

	///Returns MGEServerDisconnected
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGGameStartRefused
class MGGameStartRefused : public MultiplayerGameEvent
{
public:
	///Creates a MGGameStartRefused event
	MGGameStartRefused();

	///Returns MGEGameStartRefused
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};




///MGGameHostJoinAccepted
class MGGameHostJoinAccepted : public MultiplayerGameEvent
{
public:
	///Creates a MGGameHostJoinAccepted event
	MGGameHostJoinAccepted();

	///Returns MGEGameHostJoinAccepted
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;
};



//event_append_marker

#endif
