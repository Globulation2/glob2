// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "MultiplayerGameEvent.h"
#include <sstream>
#include <typeinfo>

/// Defines the three trivial members of a payload-free MultiplayerGameEvent.
/// getEventType() returns the event's enum tag; format() renders the bare
/// class name (preserving the historical "ClassName()" text via the
/// stringized class name); operator== is a pure typeid match since there is
/// no payload to compare. Declared with DECLARE_EMPTY_MULTIPLAYER_GAME_EVENT
/// in the header. The (class, enum tag) pairs below are the single source of
/// truth for the dispatch in MultiplayerGameScreen::handleMultiplayerGameEvent.
#define DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(ClassName, EnumTag) \
	ClassName::ClassName() \
	{ \
	} \
	Uint8 ClassName::getEventType() const \
	{ \
		return EnumTag; \
	} \
	std::string ClassName::format() const \
	{ \
		std::ostringstream s; \
		s << #ClassName "()"; \
		return s.str(); \
	} \
	bool ClassName::operator==(const MultiplayerGameEvent& rhs) const \
	{ \
		return typeid(rhs) == typeid(ClassName); \
	}

DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGPlayerListChangedEvent,  MGEPlayerListChanged)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGReadyToStartEvent,       MGEReadyToStart)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGNotReadyToStartEvent,    MGENotReadyToStart)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameExitEvent,           MGEGameExit)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameEndedNormallyEvent,  MGEGameEndedNormally)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameRefusedEvent,        MGEGameRefused)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGKickedByHostEvent,       MGEKickedByHost)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGHostCancelledGameEvent,  MGEHostCancelledGame)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameStarted,             MGEGameStarted)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGServerDisconnected,      MGEServerDisconnected)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameStartRefused,        MGEGameStartRefused)
DEFINE_EMPTY_MULTIPLAYER_GAME_EVENT(MGGameHostJoinAccepted,    MGEGameHostJoinAccepted)


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



MGPlayerReadyStatusChanged::MGPlayerReadyStatusChanged(Uint16 playerID)
	: playerID(playerID)
{
}



Uint8 MGPlayerReadyStatusChanged::getEventType() const
{
	return MGEPlayerReadyStatusChanged;
}



std::string MGPlayerReadyStatusChanged::format() const
{
	std::ostringstream s;
	s<<"MGPlayerReadyStatusChanged("<<"playerID="<<playerID<<"; "<<")";
	return s.str();
}



bool MGPlayerReadyStatusChanged::operator==(const MultiplayerGameEvent& rhs) const
{
	if(typeid(rhs)==typeid(MGPlayerReadyStatusChanged))
	{
		const MGPlayerReadyStatusChanged& r = dynamic_cast<const MGPlayerReadyStatusChanged&>(rhs);
		if(r.playerID == playerID)
			return true;
	}
	return false;
}


Uint16 MGPlayerReadyStatusChanged::getPlayerID() const
{
	return playerID;
}



//code_append_marker
