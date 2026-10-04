// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

// Identifies one of the music tracks loaded by SoundMixer at game start.
//
// The numeric values are the indices used by SoundMixer::loadTrack /
// setNextTrack, so the order of entries here must match the load order in
// GlobalContainer::loadClient (intro/menu) and Engine::run (in-game tracks).
// The Count sentinel exists so callers can range-check.
enum class MusicTrack : unsigned
{
	Intro = 0,
	Menu = 1,
	InGameDefault = 2,
	BuildingEvent = 3,
	WarEvent = 4,
	Count = 5,
};
