// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <SDL_stdinc.h>

/// Ally team numbers in GameHeader are 1-based (its constructor assigns
/// team i the value i+1), while the ally-team widget rows are 0-based and
/// only teamCount of them exist (one per team in the MapHeader).
/// A malformed or uninitialized header can carry values outside
/// [1, teamCount] -- notably 0, which would underflow to setIndex(-1),
/// or a value above teamCount -- and either throws inside
/// MultiTextButton::setIndex. Map any such value to a defined widget
/// state: the first entry (index 0). For well-formed values this is
/// exactly allyTeamNumber - 1; the inverse (widget index + 1) is applied
/// when the widgets are written back to the header.
inline int allyTeamNumberToWidgetIndex(Uint8 allyTeamNumber, int teamCount)
{
	const int index = static_cast<int>(allyTeamNumber) - 1;
	if (index < 0 || index >= teamCount)
		return 0;
	return index;
}
