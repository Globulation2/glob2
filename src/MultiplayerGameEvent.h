// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

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
	MGEDownloadPercentUpdate,
	MGEPlayerReadyStatusChanged,
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


/// Declares a payload-free MultiplayerGameEvent subclass. The class carries no
/// data, so getEventType() (the enum tag), format() (the class-name literal)
/// and operator== (a typeid match) are fully determined by the class name and
/// its one enum tag — see DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT in the .cpp for
/// the (class, tag) table. Payload-carrying events are declared explicitly.
#define DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(ClassName) \
	class ClassName : public MultiplayerGameEvent \
	{ \
	public: \
		ClassName(); \
		Uint8 getEventType() const; \
		std::string format() const; \
		bool operator==(const MultiplayerGameEvent& rhs) const; \
	};

DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGPlayerListChangedEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGReadyToStartEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGNotReadyToStartEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameExitEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameEndedNormallyEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameRefusedEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGKickedByHostEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGHostCancelledGameEvent)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameStarted)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGServerDisconnected)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameStartRefused)
DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameHostJoinAccepted)




///MGDownloadPercentUpdate
class MGDownloadPercentUpdate : public MultiplayerGameEvent
{
public:
	///Creates a MGDownloadPercentUpdate event
	MGDownloadPercentUpdate(Uint8 percent);

	///Returns MGEDownloadPercentUpdate
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;

	///Retrieves percent
	Uint8 getPercentFinished() const;
private:
	Uint8 percent;
};




///MGPlayerReadyStatusChanged
class MGPlayerReadyStatusChanged : public MultiplayerGameEvent
{
public:
	///Creates a MGPlayerReadyStatusChanged event
	MGPlayerReadyStatusChanged(Uint16 playerID);

	///Returns MGEPlayerReadyStatusChanged
	Uint8 getEventType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two MultiplayerGameEvent
	bool operator==(const MultiplayerGameEvent& rhs) const;

	///Retrieves playerID
	Uint16 getPlayerID() const;
private:
	Uint16 playerID;
};



//event_append_marker

