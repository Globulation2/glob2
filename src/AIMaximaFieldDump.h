// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AI_MAXIMA_FIELD_DUMP_H
#define AI_MAXIMA_FIELD_DUMP_H

#include "AIMaximaPlacement.h"

class Game;

/// Per-tile scoring surfaces written where they can be looked at against the
/// map that produced them. Placement reasons about geography -- threat,
/// protection, food opportunity -- and reading the scoring function tells you
/// what it intends, not what it does on a given map.
///
/// This lives outside the strategy consumers on purpose. Those files must not
/// read external configuration at all, so that everything shaping a decision
/// comes from the resolved strategy and a recorded game is reproducible from
/// it; the environment switches that turn diagnostics on belong here instead.
/// Nothing here writes to simulation state.
namespace AIMaximaFieldDump
{
/// Write the fields for `team` if enabled and due. Returns the tick written, or
/// `lastTick` unchanged when nothing was written.
int maybeWrite(const AIMaximaPlacement::WorldState &world, Game &game, int team, int tick,
			   int lastTick);
}

#endif
