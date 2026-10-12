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
constexpr unsigned LegacyPlaneCount = unsigned(PlaneTeams) * MaterialSlotCount * LEGACY_SWIM_CLASS_COUNT * 2;
constexpr unsigned PlaneCount = LegacyPlaneCount + unsigned(PlaneTeams) * MaterialSlotCount * 2;
static_assert(PlaneCount < 65535, "plane keys and registry indices are 16-bit");
struct PlaneId { int team = 0, resource = 0, swim = 0; bool market = false; };
constexpr bool validPlane(int team, int resource, int swim)
{ return team >= 0 && team < PlaneTeams && resource >= 0 && resource < int(MaterialSlotCount) && swim >= 0 && swim < SWIM_CLASS_COUNT; }
constexpr Uint16 planeKey(int team, int resource, int swim, bool market)
{
	const unsigned source = unsigned(team) * MaterialSlotCount + unsigned(resource);
	return Uint16((swim == WATER_ONLY_CLASS ? LegacyPlaneCount + source * 2 : source * LEGACY_SWIM_CLASS_COUNT * 2 + unsigned(swim) * 2) + (market ? 1 : 0));
}
constexpr PlaneId decodePlaneKey(Uint16 key)
{
	PlaneId id;
	id.market = key & 1;
	if (key >= LegacyPlaneCount) { key = (key - LegacyPlaneCount) >> 1; id.swim = WATER_ONLY_CLASS; }
	else { key >>= 1; id.swim = key % LEGACY_SWIM_CLASS_COUNT; key /= LEGACY_SWIM_CLASS_COUNT; }
	id.resource = key % MaterialSlotCount; id.team = key / MaterialSlotCount;
	return id;
}
// Keep legacy routing-cache keys unchanged; the extra class occupies a separate
// namespace so checksum and eviction state remain identical for default games.
constexpr Uint64 materialFieldKey(int excluded, int team, int resource, int swim, unsigned modes)
{
	const Uint64 source=(Uint64(excluded+1)*PlaneTeams+team)*MaterialCount+resource;
	return swim == WATER_ONLY_CLASS ? (Uint64(1)<<63) | (source*4+modes)
		: (source*LEGACY_SWIM_CLASS_COUNT+swim)*4+modes;
}
// One live plane: its key, a generation that increases on every publication
// of that key (never reset while the map lives), and the Map slot holding it.
struct PublishedPlane { Uint16 key = 0; Uint64 generation = 0; Uint16* const* slot = nullptr; };
} // namespace MapState
