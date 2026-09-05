// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GameMusicController.h"

void GameMusicController::reset()
{
	warTimeoutTicks = 0;
	buildingTimeoutTicks = 0;
}

std::optional<MusicTrack> GameMusicController::tick(const GameMusicEvents& events)
{
	std::optional<MusicTrack> nextTrack;

	// Something bad happened.
	if (events.unitUnderAttack || events.unitLostConversion || events.buildingUnderAttack)
	{
		warTimeoutTicks = EVENT_TIMEOUT_TICKS;
		nextTrack = MusicTrack::WarEvent;
	}

	// Something good happened.
	if (events.unitGainedConversion || events.buildingCompleted)
	{
		buildingTimeoutTicks = EVENT_TIMEOUT_TICKS;
		nextTrack = MusicTrack::BuildingEvent;
	}

	// Either timer just hit "one tick from zero" — fall back to the in-game
	// default. Checked BEFORE the decrement so the transition fires the tick
	// the timer would otherwise reach zero on. Matches the original
	// musicStep ordering, where the equality check ran ahead of the decay.
	if (buildingTimeoutTicks == 1 || warTimeoutTicks == 1)
	{
		nextTrack = MusicTrack::InGameDefault;
	}

	// Decay both timers.
	if (warTimeoutTicks > 0)
		--warTimeoutTicks;
	if (buildingTimeoutTicks > 0)
		--buildingTimeoutTicks;

	return nextTrack;
}
