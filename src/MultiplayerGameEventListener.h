// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "MultiplayerGameEvent.h"
#include <memory>

/// This is a mix-in class. Classes that want to respond to 
/// MultiplayerGameEvents should derive from this class
class MultiplayerGameEventListener
{
public:
	virtual ~MultiplayerGameEventListener() {}

	///This responds to a Multiplayer Game event
	virtual void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event) = 0;
};


