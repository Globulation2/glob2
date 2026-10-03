// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

/// Client-side handle to a simulation entity. A gid alone is not stable: the
/// simulation recycles slots (lowest free id), so a selection that outlives
/// its entity could silently move to a newcomer. `generation` is the entity's
/// `scriptIdentity`, which Game::allocateScriptIdentity bumps for every new
/// occupant of a slot and which saves preserve. Resolve a ref through
/// Game::resolveBuilding / Game::resolveUnit every time it is used; a null
/// result means the entity is gone (or was replaced).
///
/// Refs are plain values: the GUI may hold and copy them freely without
/// reading simulation memory, which is what lets the selection survive the
/// move of the simulation to its own thread.
struct BuildingRef
{
	Uint16 gid = 0xFFFF;   //!< NOGBID when empty.
	Uint32 generation = 0; //!< Building::scriptIdentity at selection time.

	bool empty() const { return gid == 0xFFFF; }
	bool operator==(const BuildingRef &o) const { return gid == o.gid && generation == o.generation; }
	bool operator!=(const BuildingRef &o) const { return !(*this == o); }
};

struct UnitRef
{
	Uint16 gid = 0xFFFF;   //!< NOGUID when empty.
	Uint32 generation = 0; //!< Unit::scriptIdentity at selection time.

	bool empty() const { return gid == 0xFFFF; }
	bool operator==(const UnitRef &o) const { return gid == o.gid && generation == o.generation; }
	bool operator!=(const UnitRef &o) const { return !(*this == o); }
};
