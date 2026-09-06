// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <optional>

#include "MusicTrack.h"

//! Events observed from the local team this tick that may trigger a music
//! transition. The booleans correspond 1:1 to the GameEventType values
//! GameMusicController cares about; bundling them at the call site keeps the
//! controller free of any Team / Game dependency, which is what lets it run
//! in unit tests without SDL or libgag.
struct GameMusicEvents
{
	bool unitUnderAttack = false;
	bool unitLostConversion = false;
	bool unitGainedConversion = false;
	bool buildingUnderAttack = false;
	bool buildingCompleted = false;
};

//! Pure state machine that selects in-game music based on recent team events.
//!
//! Two timers count down at the simulation tick rate (40 ms). A "bad" event
//! (unit/building under attack, unit lost to conversion) sets the war timer
//! and queues the war track. A "good" event (building completed, unit
//! converted to us) sets the building timer and queues the building track.
//! When either timer is about to expire (reaches 1 before this tick's
//! decrement), the controller queues a return to the in-game default track.
//!
//! All timer state lives on the instance — no function-local statics — so
//! that resetting between games is just constructing a fresh controller (or
//! calling reset()). The previous implementation used file-scope statics in
//! GameGUI::musicStep and leaked timer state from one game into the next.
class GameMusicController
{
public:
	//! Wall-clock duration of the post-event "stay on the event track"
	//! window, in 40 ms simulation ticks (220 * 40 ms = 8.8 s).
	static constexpr unsigned EVENT_TIMEOUT_TICKS = 220;

	//! Reset both timers to 0. Called by GameGUI::init() at the start of
	//! every loaded game so state from a previous game cannot leak in.
	void reset();

	//! Advance one simulation tick. Returns the track that should be
	//! requested from SoundMixer this tick, or std::nullopt if nothing
	//! changes. When multiple branches fire in the same tick (e.g. an
	//! event AND a timer expiring), the later branch wins, mirroring the
	//! original musicStep which emitted multiple setNextTrack calls per
	//! tick and let the last one stick.
	std::optional<MusicTrack> tick(const GameMusicEvents& events);

	// Accessors for testing.
	unsigned getWarTimeoutTicks() const { return warTimeoutTicks; }
	unsigned getBuildingTimeoutTicks() const { return buildingTimeoutTicks; }

private:
	unsigned warTimeoutTicks = 0;
	unsigned buildingTimeoutTicks = 0;
};
