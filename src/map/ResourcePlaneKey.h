// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"
#include "Team.h"
#include "UnitConsts.h"
#include <cstdint>

// Published material planes (per team, material slot, swim class, natural or
// market goals) are addressed by one dense key on the live Map and in every
// snapshot, so a capture visits only live planes and a consumer looks one up
// without hashing.
namespace MapState
{
constexpr int PlaneTeams = Team::MAX_COUNT;
constexpr unsigned PlaneCount = unsigned(PlaneTeams) * MaterialSlotCount * SWIM_CLASS_COUNT * 2;
static_assert(PlaneCount < 65535, "plane keys and registry indices are 16-bit");
struct PlaneId { int team = 0, resource = 0, swim = 0; bool market = false; };
constexpr bool validPlane(int team, int resource, int swim)
{ return team >= 0 && team < PlaneTeams && resource >= 0 && resource < int(MaterialSlotCount) && swim >= 0 && swim < SWIM_CLASS_COUNT; }
constexpr Uint16 planeKey(int team, int resource, int swim, bool market)
{ return Uint16(((unsigned(team) * MaterialSlotCount + unsigned(resource)) * SWIM_CLASS_COUNT + unsigned(swim)) * 2 + (market ? 1 : 0)); }
constexpr PlaneId decodePlaneKey(Uint16 key)
{
	PlaneId id;
	id.market = key & 1; key >>= 1;
	id.swim = key % SWIM_CLASS_COUNT; key /= SWIM_CLASS_COUNT;
	id.resource = key % MaterialSlotCount; id.team = key / MaterialSlotCount;
	return id;
}
// One live plane: its key, a generation that increases on every publication
// of that key (never reset while the map lives), and the Map slot holding it.
struct PublishedPlane { Uint16 key = 0; Uint64 generation = 0; Uint16* const* slot = nullptr; };
} // namespace MapState
